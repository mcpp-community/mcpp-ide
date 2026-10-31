// The language service beside this backend: mcppls. mcpp-ide registers no language service of its
// own (design 2026-10-28 §11.2); every editor feature of a C++ file is mcppls's, and the panels say
// which mcppls answers and whether it is new enough.
//
// THE HANDSHAKE IS A VERSION, READ FROM THE PROGRAM. `mcppls --version` prints the version (bare, or
// after the program's name). mcppls 0.0.18 is the first release that reads mcpp's result record
// (`target/.last-result.json`, mcpp docs/50 §5.2) and refreshes after any build -- one started here,
// in a terminal or by an agent -- so a build through mcpp-ide needs nothing forwarded to it. An older
// mcppls is reported with what to do, never degraded around in silence.
//
// WHAT mcppls SAYS ABOUT A WORKSPACE IS ITS STATUS. mcppls 0.0.20 and later answer `mcppls status --root
// <dir> --format json` (mcppls S3 section 8) without starting an engine: its capabilities, the
// workspace's state, its engine and kit, and mcppls's actions under each editor's own id. The editors
// show that one document. mcppls 0.0.18 and 0.0.19 have no `status`; for them the actions are the ones
// those releases offered (the frozen table) and the workspace is not known.
//
// A SERVER IS A COMMAND. mcppls's payload (the VSIX's and the CLion package's `payload/`, the one Zed's
// install puts in the user data directory) carries the server as one okb of both architectures that the
// platform's runner starts: payload.json's `server` names `runner` and `path` (`bin/okrun bin/mcppls.okb`),
// or a program alone. A candidate is a payload directory or a program, and what is asked for its version
// is the command it stands for.
export module mcppide.languageservice;

import std;
import nlohmann.json;
import mcpplibs.os;
import mcppide.mcpp;

export namespace mcppide::languageservice {

using Json = nlohmann::json;

// The first mcppls that reads the result record.
inline constexpr std::string_view kRequired = "0.0.18";
// The first mcppls that answers `mcppls status`.
inline constexpr std::string_view kStatusSince = "0.0.20";

enum class State { ok, outdated, missing, unreadable };
std::string_view state_name(State s);

struct Status {
    State state = State::missing;
    std::filesystem::path program;   // the mcppls that answered: the image or the program (empty when none was found)
    std::vector<std::filesystem::path> command;   // what starts it: the runner and the image, or the program
    std::string source;              // "editor" (a path the adapter named) | "PATH" | "xlings" | "payload"
    std::string version;             // as it printed it
    std::string detail;              // why the version could not be read
};

// A place mcppls may be, as the command that starts it.
struct Candidate {
    std::vector<std::filesystem::path> command;
    std::string source;
};

// The version in `mcppls --version`'s output: the first word that starts with a digit.
std::string version_in(std::string_view output);

// The command payload.json's `server` names in a payload directory: the runner and the image, or the
// program. Empty when the directory holds no payload or what it names is not there.
std::vector<std::filesystem::path> payload_command(const std::filesystem::path& payload);

// The command a path the adapter named stands for: a payload directory's server, or the program itself.
std::vector<std::filesystem::path> command_of(const std::filesystem::path& named);

// The places to look, in order: what the adapter knows (the mcppls extension's or plugin's own
// payload, or a program), PATH, xlings, and the payload Zed's install puts in the user data directory.
std::vector<Candidate> candidates(const std::vector<std::string>& fromEditor);

// Finds mcppls among the candidates and reads its version.
Status probe(const std::vector<std::string>& fromEditor);

// mcppls's account of a workspace: `mcppls status`'s document, or the frozen table for an mcppls
// before it.
struct Report {
    std::string source = "none";      // "status" | "frozen" | "none"
    Json capabilities = Json::array();
    Json actions = Json::array();     // [{id, title, vscode?, vscodeArguments?, jetbrains?, nvim?, command?}]
    Json workspace;                   // null when mcppls was not asked
    Json instances = Json::array();
    std::string detail;               // why `status` could not be read
};

// The actions mcppls 0.0.18 and 0.0.19 offered, under the ids those releases gave them.
const Json& frozen_actions();
// The report for the mcppls `s` found, about `root`: `status` when it has the command, else the frozen
// table. Bounded: a status that does not answer within five seconds is reported, not waited for.
Report report(const Status& s, const std::filesystem::path& root);
// The report `mcppls status --format json` printed `output` for (the tests).
Report parse_status(std::string_view output);
// The status for an mcppls at `program` whose `--version` printed `output` (the tests).
Status classify(const std::filesystem::path& program, std::string source, std::string_view output);

}  // namespace mcppide::languageservice

namespace mcppide::languageservice {

std::string_view state_name(State s) {
    switch (s) {
        case State::ok: return "ok";
        case State::outdated: return "outdated";
        case State::missing: return "missing";
        case State::unreadable: return "unreadable";
    }
    return "missing";
}

std::string version_in(std::string_view output) {
    std::string word;
    auto take = [&]() -> std::optional<std::string> {
        if (!word.empty() && std::isdigit(static_cast<unsigned char>(word.front()))) {
            while (!word.empty() && !std::isdigit(static_cast<unsigned char>(word.back()))) word.pop_back();
            return word;
        }
        word.clear();
        return std::nullopt;
    };
    for (char c : output) {
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            if (auto v = take()) return *v;
            continue;
        }
        word.push_back(c);
    }
    if (auto v = take()) return *v;
    return {};
}

// A file payload.json names, inside the payload: a relative path with no `..` part, as mcppls's own
// payload reader holds it. Empty when it would name something outside.
std::filesystem::path within(const std::filesystem::path& payload, const std::string& named) {
    const std::filesystem::path relative(named);
    if (relative.empty() || relative.has_root_path() || relative.has_root_name()) return {};
    for (const auto& part : relative)
        if (part == "..") return {};
    return payload / relative;
}

std::vector<std::filesystem::path> payload_command(const std::filesystem::path& payload) {
    std::error_code ec;
    std::ifstream in(payload / "payload.json", std::ios::binary);
    if (!in) return {};
    const Json manifest = Json::parse(in, nullptr, false);
    if (!manifest.is_object() || !manifest.contains("server") || !manifest["server"].is_object()) return {};
    const Json& server = manifest["server"];
    if (!server.contains("path") || !server["path"].is_string()) return {};
    std::vector<std::filesystem::path> command;
    if (server.contains("runner") && server["runner"].is_string()) command.push_back(within(payload, server["runner"].get<std::string>()));
    command.push_back(within(payload, server["path"].get<std::string>()));
    for (const auto& part : command)
        if (part.empty() || !std::filesystem::is_regular_file(part, ec)) return {};
    return command;
}

std::vector<std::filesystem::path> command_of(const std::filesystem::path& named) {
    std::error_code ec;
    if (std::filesystem::is_directory(named, ec)) return payload_command(named);
    if (std::filesystem::is_regular_file(named, ec)) return {named};
    return {};
}

std::vector<Candidate> candidates(const std::vector<std::string>& fromEditor) {
    std::vector<Candidate> out;
    for (const auto& p : fromEditor) if (!p.empty()) out.push_back({command_of(p), "editor"});
    if (auto found = mcpplibs::os::env::find_executable("mcppls")) out.push_back({{*found}, "PATH"});
    const auto exe = std::string("mcppls") + std::string(mcpplibs::os::conventions().exe_suffix);
    if (auto xh = mcpplibs::os::env::get("XLINGS_HOME"))
        out.push_back({command_of(std::filesystem::path(*xh) / "subos" / "current" / "bin" / exe), "xlings"});
    if (auto home = mcpplibs::os::dirs::home()) {
        out.push_back({command_of(*home / ".xlings" / "subos" / "current" / "bin" / exe), "xlings"});
        out.push_back({command_of(*home / ".xlings" / "subos" / "default" / "bin" / exe), "xlings"});
    }
    // The payload Zed's install puts at `<user data>/mcppls/payload`.
    if (auto data = mcpplibs::os::dirs::data()) out.push_back({payload_command(*data / "mcppls" / "payload"), "payload"});
    std::erase_if(out, [](const Candidate& c) { return c.command.empty(); });
    return out;
}

Status classify(const std::filesystem::path& program, std::string source, std::string_view output) {
    Status s;
    s.program = program;
    s.source = std::move(source);
    s.version = version_in(output);
    if (s.version.empty()) {
        s.state = State::unreadable;
        s.detail = std::format("`{} --version` printed no version", program.filename().string());
        return s;
    }
    s.state = mcpp::compare_versions(s.version, kRequired) >= 0 ? State::ok : State::outdated;
    return s;
}

// The first candidate is the one asked: the one the adapter named, else the first found.
Status probe(const std::vector<std::string>& fromEditor) {
    const auto found = candidates(fromEditor);
    if (found.empty()) return Status{};
    const auto& [command, source] = found.front();
    mcpplibs::os::process::spawn_options o;
    o.program = command.front();
    for (std::size_t i = 1; i < command.size(); ++i) o.args.push_back(command[i].string());
    o.args.push_back("--version");
    o.in = mcpplibs::os::process::stdio::null;
    auto r = mcpplibs::os::process::run(o);
    Status s;
    if (!r) {
        s.state = State::unreadable;
        s.program = command.back();
        s.source = source;
        s.detail = r.error().message();
    } else {
        s = classify(command.back(), source, r->out);
    }
    s.command = command;
    return s;
}

const Json& frozen_actions() {
    static const Json table = [] {
        auto row = [](std::string_view id, std::string_view title, Json editors) {
            Json r{{"id", id}, {"title", title}};
            r.update(editors);
            return r;
        };
        return Json::array({
            row("restart", "Restart the language server", {{"vscode", "mcppls.restartServer"}, {"nvim", "McpplsRestart"}}),
            row("reload", "Read the build description again", {{"vscode", "mcppls.reloadBuildDescription"}, {"nvim", "McpplsReload"}}),
            row("restartEngine", "Restart the engine", {{"vscode", "mcppls.restartClangd"}}),
            row("resetCache", "Reset the workspace cache", {{"vscode", "mcppls.resetWorkspaceCache"}, {"nvim", "McpplsResetCache"}}),
            row("showStatus", "Show the status", {{"nvim", "McpplsStatus"}}),
            row("showLog", "Show the log", {{"vscode", "mcppls.showLogs"}}),
            row("openLogDirectory", "Open the log directory", {{"vscode", "mcppls.revealCacheDirectory"}, {"vscodeArguments", Json::array({"logs"})}}),
            row("openCacheDirectory", "Open the workspace cache directory", {{"vscode", "mcppls.revealCacheDirectory"}}),
            row("collectReport", "Collect a report", {{"vscode", "mcppls.collectReport"}}),
            row("exportDiagnostics", "Export a diagnostic bundle", {{"vscode", "mcppls.exportDiagnosticBundle"}}),
            row("selectContext", "Select the analysis context", {{"vscode", "mcppls.selectContext"}}),
            row("moduleGraph", "Show the module graph", {{"vscode", "mcppls.showModuleGraph"}}),
            row("runBuildTool", "Run the build tool in a terminal", {{"vscode", "mcppls.runBuildToolInTerminal"}}),
            row("installTools", "Install the command line tools", {{"vscode", "mcppls.installCommandLineTools"}}),
            row("reviewRun", "Review the workspace's changes", {{"vscode", "mcppls.review.run"}}),
            row("reviewClear", "Clear the review", {{"vscode", "mcppls.review.clear"}}),
            row("turnOffInWorkspace", "Turn off in this workspace", {{"vscode", "mcppls.turnOffInWorkspace"}}),
            row("turnOnInWorkspace", "Turn on in this workspace", {{"vscode", "mcppls.turnOnInWorkspace"}}),
            // 0.0.19's CLion plugin; under 0.0.18 the action is not there, and the IDE finds no action by the id.
            row("turnOffOtherCppFeatures", "Turn off other C++ features",
                {{"vscode", "mcppls.turnOffOtherCppFeatures"}, {"jetbrains", "community.mcpp.mcppls.UseMcpplsOnly"}}),
            row("restoreOtherCppFeatures", "Restore other C++ features",
                {{"vscode", "mcppls.restoreOtherCppFeatures"}, {"jetbrains", "community.mcpp.mcppls.RestoreClionEngine"}}),
        });
    }();
    return table;
}

Report parse_status(std::string_view output) {
    Report r;
    const Json doc = Json::parse(output, nullptr, false);
    if (!doc.is_object() || doc.value("schema", 0) < 1) {
        r.source = "frozen";
        r.actions = frozen_actions();
        r.detail = "`mcppls status` printed no status document";
        return r;
    }
    r.source = "status";
    if (doc.contains("capabilities") && doc["capabilities"].is_array())
        for (const auto& c : doc["capabilities"]) if (c.is_string()) r.capabilities.push_back(c);
    if (doc.contains("actions") && doc["actions"].is_array())
        for (const auto& a : doc["actions"]) if (a.is_object() && a.contains("id") && a["id"].is_string()) r.actions.push_back(a);
    if (doc.contains("workspace") && doc["workspace"].is_object()) r.workspace = doc["workspace"];
    if (doc.contains("instances") && doc["instances"].is_array()) r.instances = doc["instances"];
    return r;
}

Report report(const Status& s, const std::filesystem::path& root) {
    if (s.state != State::ok || s.command.empty()) return Report{};
    if (mcpp::compare_versions(s.version, kStatusSince) < 0) {
        Report r;
        r.source = "frozen";
        r.actions = frozen_actions();
        return r;
    }
    mcpplibs::os::process::spawn_options o;
    o.program = s.command.front();
    for (std::size_t i = 1; i < s.command.size(); ++i) o.args.push_back(s.command[i].string());
    for (const char* a : {"status", "--format", "json", "--root"}) o.args.push_back(a);
    o.args.push_back(root.string());
    o.in = mcpplibs::os::process::stdio::null;
    mcpplibs::os::process::bounds limits;
    limits.hard = std::chrono::milliseconds(5000);
    limits.out_limit = std::size_t{1} << 20;
    limits.err_limit = std::size_t{64} << 10;
    auto run = mcpplibs::os::process::run(o, limits);
    if (!run) {
        Report r = parse_status("");
        r.detail = run.error().message();
        return r;
    }
    Report r = parse_status(run->out);
    if (run->timed_out) r.detail = "`mcppls status` did not answer within 5 s";
    return r;
}

}  // namespace mcppide::languageservice
