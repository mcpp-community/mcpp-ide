// The language service beside this backend: mcppls. mcpp-ide registers no language service of its
// own (design 2026-10-28 §11.2); every editor feature of a C++ file is mcppls's, and the panels say
// which mcppls answers and whether it is new enough.
//
// THE HANDSHAKE IS A VERSION, READ FROM THE PROGRAM. `mcppls --version` prints the version (bare, or
// after the program's name). mcppls 0.0.18 is the first release that reads mcpp's result record
// (`target/.last-result.json`, mcpp docs/50 §5.2) and refreshes after any build -- one started here,
// in a terminal or by an agent -- so a build through mcpp-ide needs nothing forwarded to it. An older
// mcppls is reported with what to do, never degraded around in silence.
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
    std::filesystem::path program;   // the mcppls that answered (empty when none was found)
    std::string source;              // "editor" (a path the adapter named) | "PATH" | "xlings" | "payload"
    std::string version;             // as it printed it
    std::string detail;              // why the version could not be read
};

// The version in `mcppls --version`'s output: the first word that starts with a digit.
std::string version_in(std::string_view output);

// The places to look, in order: what the adapter knows (the mcppls extension's or plugin's own
// server), PATH, xlings, and the payload the mcppls CLion plugin installs.
std::vector<std::pair<std::filesystem::path, std::string>> candidates(const std::vector<std::string>& fromEditor);

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

std::vector<std::pair<std::filesystem::path, std::string>> candidates(const std::vector<std::string>& fromEditor) {
    std::vector<std::pair<std::filesystem::path, std::string>> out;
    for (const auto& p : fromEditor) if (!p.empty()) out.push_back({std::filesystem::path(p), "editor"});
    if (auto found = mcpplibs::os::env::find_executable("mcppls")) out.push_back({*found, "PATH"});
    const auto exe = std::string("mcppls") + std::string(mcpplibs::os::conventions().exe_suffix);
    if (auto xh = mcpplibs::os::env::get("XLINGS_HOME"))
        out.push_back({std::filesystem::path(*xh) / "subos" / "current" / "bin" / exe, "xlings"});
    if (auto home = mcpplibs::os::dirs::home()) {
        out.push_back({*home / ".xlings" / "subos" / "current" / "bin" / exe, "xlings"});
        out.push_back({*home / ".xlings" / "subos" / "default" / "bin" / exe, "xlings"});
    }
    // The mcppls CLion plugin's `--install` puts the server at `<user data>/mcppls/payload`.
    if (auto data = mcpplibs::os::dirs::data()) out.push_back({*data / "mcppls" / "payload" / "bin" / exe, "payload"});
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

Status probe(const std::vector<std::string>& fromEditor) {
    std::error_code ec;
    for (const auto& [program, source] : candidates(fromEditor)) {
        if (!std::filesystem::is_regular_file(program, ec)) continue;
        mcpplibs::os::process::spawn_options o;
        o.program = program;
        o.args = {"--version"};
        o.in = mcpplibs::os::process::stdio::null;
        auto r = mcpplibs::os::process::run(o);
        if (!r) {
            Status s;
            s.state = State::unreadable;
            s.program = program;
            s.source = source;
            s.detail = r.error().message();
            return s;
        }
        return classify(program, source, r->out);
    }
    return Status{};
}

}  // namespace mcppide::languageservice
