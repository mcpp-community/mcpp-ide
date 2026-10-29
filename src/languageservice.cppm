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

std::vector<std::filesystem::path> payload_command(const std::filesystem::path& payload) {
    std::error_code ec;
    std::ifstream in(payload / "payload.json", std::ios::binary);
    if (!in) return {};
    const Json manifest = Json::parse(in, nullptr, false);
    if (!manifest.is_object() || !manifest.contains("server") || !manifest["server"].is_object()) return {};
    const Json& server = manifest["server"];
    if (!server.contains("path") || !server["path"].is_string()) return {};
    std::vector<std::filesystem::path> command;
    if (server.contains("runner") && server["runner"].is_string()) command.push_back(payload / server["runner"].get<std::string>());
    command.push_back(payload / server["path"].get<std::string>());
    for (const auto& part : command)
        if (!std::filesystem::is_regular_file(part, ec)) return {};
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

}  // namespace mcppide::languageservice
