// mcpp, as this backend reaches it: where the program is, what it supports
// (`--protocol-version`), and the two ways a command is run -- to one envelope
// (a query) or to a stream of events (a build, a test).
//
// THE BACKEND READS ONLY MCPP'S CONTRACT (docs/50 of mcpp). It does not parse
// mcpp.toml, resolve a dependency or guess a path mcpp did not name: every
// fact here is a field of an envelope, an event or a record.
export module mcppide.mcpp;

import std;
import nlohmann.json;
import mcpplibs.os;

export namespace mcppide::mcpp {

using Json = nlohmann::json;

// The release whose contract this backend reads in full: the build stream,
// the result record, the launch descriptor, --deny and the JSON queries.
inline constexpr std::string_view kContractFloor = "2026.10.26.1";

struct Located {
    std::filesystem::path program;
    std::string source;   // "setting" | "PATH" | "xlings" | "home"
};

// Where mcpp is: the configured path, else PATH, else the places an xlings or
// a self-contained installation puts it.
std::expected<Located, std::string> locate(const std::string& configuredPath);

struct Protocol {
    std::string version;                                // "2026.10.26.2"
    int envelopeMin = 0, envelopeMax = 0;
    std::map<std::string, int> kinds;                   // kind -> version
    std::map<std::string, std::vector<std::string>> commandEffects;
    std::vector<std::string> features;                  // what a client gates on (mcpp docs/50 §4)
    bool contract = false;                              // the full control contract (>= kContractFloor)
};

// THE FEATURES OF AN mcpp THAT PREDATES `features` (design 2026-10-30 part 2 §4.3): a table frozen
// at the release that added the list, read for those releases only. An mcpp that states
// `features` is taken at its word.
std::vector<std::string> frozen_features(std::string_view version);

// What a project's mcpp is, asked in the project (design 2026-10-30 part 2 §4.2): `mcpp self env
// --format json` run in its root, through the program found on PATH (a version manager's shim
// chooses by the directory) or the one the settings name, with XLINGS_PROJECT_DIR removed.
struct ProjectMcpp {
    // ok: the project's mcpp answered. pinned-missing: the project pins a version that is not
    // installed, and the shim refused. failed: anything else. not-probed: an untrusted workspace,
    // whose pinned program is not run; the global mcpp's facts stand in.
    std::string state = "not-probed";
    std::string version;
    std::string program;                 // the file that ran (`self env`'s `program`)
    std::vector<std::string> features;
    bool featuresFrozen = false;         // from the frozen table, not from mcpp
    bool fromSetting = false;            // mcpp.path named the program
    std::string message;                 // why it is not ok, in the shim's or mcpp's words
    std::string install;                 // the command that installs the pinned version
    Json env = Json::object();           // `self env`'s data
};

// `mcpp --version` and `mcpp --protocol-version`.
// The environment overrides apply to these two runs as to every run of mcpp the backend makes.
using Environment = std::vector<std::pair<std::string, std::optional<std::string>>>;
std::expected<Protocol, std::string> handshake(const std::filesystem::path& program, const Environment& env = {});

// Compares dotted versions numerically ("2026.10.26.2" > "2026.10.26.1").
int compare_versions(std::string_view a, std::string_view b);

struct RunOptions {
    std::filesystem::path cwd;
    std::vector<std::string> args;
    // Effects the caller denies (`MCPP_DENY_EFFECTS`): an untrusted workspace.
    std::vector<std::string> deny;
    std::string client = "mcpp-ide";
    bool color = false;              // CLICOLOR_FORCE for narration a terminal renders
    std::vector<std::pair<std::string, std::optional<std::string>>> extraEnv;
};

class Client {
public:
    Client() = default;
    Client(std::filesystem::path program, Protocol protocol)
        : program_(std::move(program)), protocol_(std::move(protocol)) {}

    const std::filesystem::path& program() const { return program_; }
    const Protocol& protocol() const { return protocol_; }
    bool ready() const { return !program_.empty(); }
    bool supports_kind(std::string_view kind) const { return protocol_.kinds.contains(std::string(kind)); }

    mcpplibs::os::process::spawn_options spawn_options(const RunOptions& o) const;

    // Runs a command whose standard output is one envelope (docs/50 §2).
    struct EnvelopeResult {
        int exitCode = -1;
        Json envelope;              // null when standard output held no JSON
        std::string narration;      // standard error
        std::string output;         // standard output as it was printed
    };
    std::expected<EnvelopeResult, std::string> envelope(const RunOptions& o) const;

    // Runs a command whose standard output is NDJSON (`mcpp test --list --message-format json`):
    // every line that is a JSON document, in order.
    struct RecordsResult {
        int exitCode = -1;
        std::vector<Json> records;
        std::string narration;
    };
    std::expected<RecordsResult, std::string> records(const RunOptions& o) const;

    // Runs any command and returns what it printed, bounded: past `timeout` it is ended, and each
    // output keeps at most `outputLimit` bytes. What an editor's own parsers read (mcpp-vscode's
    // views): the backend is the one place mcpp runs, with the session's client and denied effects.
    struct ExecResult {
        int exitCode = -1;
        std::string out, err;
        bool timedOut = false, truncated = false;
    };
    std::expected<ExecResult, std::string> exec(const RunOptions& o, std::chrono::milliseconds timeout,
                                                std::size_t outputLimit) const;

    // What the project at `root` runs (ProjectMcpp).
    ProjectMcpp probe(const std::filesystem::path& root, const RunOptions& base) const;

private:
    std::filesystem::path program_;
    Protocol protocol_;
};

// The diagnostics of an envelope, normalised: severity, message, code, and
// path/range when the envelope located them.
Json diagnostics_of(const Json& envelope);

}  // namespace mcppide::mcpp

namespace mcppide::mcpp {

int compare_versions(std::string_view a, std::string_view b) {
    auto parts = [](std::string_view s) {
        std::vector<long> out;
        std::size_t pos = 0;
        while (pos < s.size()) {
            auto end = s.find('.', pos);
            if (end == std::string_view::npos) end = s.size();
            long v = 0;
            std::from_chars(s.data() + pos, s.data() + end, v);
            out.push_back(v);
            pos = end + 1;
        }
        return out;
    };
    const auto pa = parts(a), pb = parts(b);
    for (std::size_t i = 0; i < std::max(pa.size(), pb.size()); ++i) {
        const long x = i < pa.size() ? pa[i] : 0, y = i < pb.size() ? pb[i] : 0;
        if (x != y) return x < y ? -1 : 1;
    }
    return 0;
}

std::expected<Located, std::string> locate(const std::string& configuredPath) {
    std::error_code ec;
    if (!configuredPath.empty()) {
        std::filesystem::path p(configuredPath);
        if (std::filesystem::is_regular_file(p, ec)) return Located{p, "setting"};
        if (auto found = mcpplibs::os::env::find_executable(configuredPath)) return Located{*found, "setting"};
        return std::unexpected(std::format("the configured mcpp ({}) does not exist", configuredPath));
    }
    if (auto found = mcpplibs::os::env::find_executable("mcpp")) return Located{*found, "PATH"};
    // The places an xlings or a self-contained installation puts it, in the spelling of the system
    // this backend runs upon (an ok image's included: mcpplibs.os answers for the host).
    const auto exe = std::string("mcpp") + std::string(mcpplibs::os::conventions().exe_suffix);
    const auto home = mcpplibs::os::dirs::home().value_or(std::filesystem::path{});
    std::vector<std::pair<std::filesystem::path, std::string>> candidates;
    if (auto xh = mcpplibs::os::env::get("XLINGS_HOME"))
        candidates.push_back({std::filesystem::path(*xh) / "subos" / "current" / "bin" / exe, "xlings"});
    if (!home.empty()) {
        candidates.push_back({std::filesystem::path(home) / ".xlings" / "subos" / "current" / "bin" / exe, "xlings"});
        candidates.push_back({std::filesystem::path(home) / ".xlings" / "subos" / "default" / "bin" / exe, "xlings"});
        candidates.push_back({std::filesystem::path(home) / ".mcpp" / "bin" / exe, "home"});
    }
    for (const auto& [p, source] : candidates)
        if (std::filesystem::exists(p, ec)) return Located{p, source};
    return std::unexpected(std::string(
        "mcpp was not found: set mcpp.path, put mcpp on PATH, or install it (xlings install mcpp)"));
}

std::expected<Protocol, std::string> handshake(const std::filesystem::path& program, const Environment& env) {
    mcpplibs::os::process::spawn_options o;
    o.program = program;
    o.args = {"--version"};
    o.in = mcpplibs::os::process::stdio::null;
    o.env = env;
    o.env.push_back({"XLINGS_PROJECT_DIR", std::nullopt});   // mcpp docs/50: no other project's pin
    auto v = mcpplibs::os::process::run(o);
    if (!v) return std::unexpected(v.error().message());
    if (!v->status.success()) return std::unexpected(std::format("{} --version exited {}", program.string(), v->status.shell_code()));
    Protocol p;
    {
        // "mcpp 2026.10.26.2" -- the first word that starts with a digit.
        std::istringstream words(v->out);
        for (std::string w; words >> w;)
            if (!w.empty() && std::isdigit(static_cast<unsigned char>(w.front()))) { p.version = w; break; }
    }
    o.args = {"--protocol-version"};
    auto pv = mcpplibs::os::process::run(o);
    if (pv && pv->status.success()) {
        auto doc = Json::parse(pv->out, nullptr, false);
        if (!doc.is_discarded() && doc.is_object()) {
            if (doc.contains("envelope")) {
                p.envelopeMin = doc["envelope"].value("min", 0);
                p.envelopeMax = doc["envelope"].value("max", 0);
            }
            if (doc.contains("kinds") && doc["kinds"].is_object())
                for (auto it = doc["kinds"].begin(); it != doc["kinds"].end(); ++it)
                    if (it->is_number_integer()) p.kinds[it.key()] = it->get<int>();
            if (doc.contains("features") && doc["features"].is_array())
                for (const auto& f : doc["features"]) if (f.is_string()) p.features.push_back(f.get<std::string>());
            if (doc.contains("commands") && doc["commands"].is_object())
                for (auto it = doc["commands"].begin(); it != doc["commands"].end(); ++it) {
                    std::vector<std::string> effects;
                    if (it->contains("effects") && (*it)["effects"].is_array())
                        for (const auto& e : (*it)["effects"]) if (e.is_string()) effects.push_back(e.get<std::string>());
                    p.commandEffects[it.key()] = std::move(effects);
                }
        }
    }
    p.contract = !p.version.empty() && compare_versions(p.version, kContractFloor) >= 0 &&
                 p.kinds.contains("mcpp.build");
    if (p.features.empty()) p.features = frozen_features(p.version);
    return p;
}

std::vector<std::string> frozen_features(std::string_view version) {
    // 2026.10.30.1 introduced both meanings; 2026.10.31.1 added the list that names them.
    if (!version.empty() && compare_versions(version, "2026.10.30.1") >= 0) return {"target-sets", "pkg-format-list"};
    return {};
}

// A run of mcpp: its output on pipes, its input the null device (the backend's own streams carry
// the protocol), and a unit of its own, so a cancelled build ends with everything it started.
mcpplibs::os::process::spawn_options Client::spawn_options(const RunOptions& o) const {
    mcpplibs::os::process::spawn_options s;
    s.program = program_;
    s.args = o.args;
    s.cwd = o.cwd;
    s.in = mcpplibs::os::process::stdio::null;
    s.own_unit = true;
    s.env.push_back({"MCPP_CLIENT", o.client});
    // THE PROJECT'S DIRECTORY DECIDES WHICH mcpp RUNS (mcpp docs/50, "How a tool runs mcpp for a
    // project"): a shim reads XLINGS_PROJECT_DIR only where the directory has no pin, and an editor
    // started from another project's terminal hands it to everything it starts.
    s.env.push_back({"XLINGS_PROJECT_DIR", std::nullopt});
    if (!o.deny.empty()) {
        std::string joined;
        for (const auto& d : o.deny) { if (!joined.empty()) joined += ','; joined += d; }
        s.env.push_back({"MCPP_DENY_EFFECTS", joined});
    }
    if (o.color) {
        s.env.push_back({"CLICOLOR_FORCE", "1"});
        s.env.push_back({"NO_COLOR", std::nullopt});
        s.env.push_back({"MCPP_NO_COLOR", std::nullopt});
    } else {
        s.env.push_back({"NO_COLOR", "1"});
    }
    for (const auto& kv : o.extraEnv) s.env.push_back(kv);
    return s;
}

std::expected<Client::EnvelopeResult, std::string> Client::envelope(const RunOptions& o) const {
    auto r = mcpplibs::os::process::run(spawn_options(o));
    if (!r) return std::unexpected(r.error().message());
    EnvelopeResult out;
    out.exitCode = r->status.shell_code();
    out.narration = std::move(r->err);
    out.output = r->out;
    // One document, or NDJSON whose last line is the envelope.
    auto doc = Json::parse(r->out, nullptr, false);
    if (doc.is_discarded()) {
        auto end = r->out.find_last_not_of("\r\n ");
        if (end != std::string::npos) {
            auto start = r->out.rfind('\n', end);
            start = start == std::string::npos ? 0 : start + 1;
            doc = Json::parse(r->out.substr(start, end - start + 1), nullptr, false);
        }
    }
    out.envelope = doc.is_discarded() ? Json(nullptr) : std::move(doc);
    return out;
}

std::expected<Client::RecordsResult, std::string> Client::records(const RunOptions& o) const {
    auto r = mcpplibs::os::process::run(spawn_options(o));
    if (!r) return std::unexpected(r.error().message());
    RecordsResult out;
    out.exitCode = r->status.shell_code();
    out.narration = std::move(r->err);
    std::size_t pos = 0;
    while (pos < r->out.size()) {
        auto end = r->out.find('\n', pos);
        if (end == std::string::npos) end = r->out.size();
        auto line = std::string_view(r->out).substr(pos, end - pos);
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        if (!line.empty()) {
            auto doc = Json::parse(line, nullptr, false);
            if (!doc.is_discarded()) out.records.push_back(std::move(doc));
        }
        pos = end + 1;
    }
    return out;
}

std::expected<Client::ExecResult, std::string> Client::exec(const RunOptions& o, std::chrono::milliseconds timeout,
                                                           std::size_t outputLimit) const {
    mcpplibs::os::process::bounds limits;
    limits.hard = timeout;
    limits.out_limit = outputLimit;
    limits.err_limit = outputLimit;
    auto r = mcpplibs::os::process::run(spawn_options(o), limits);
    if (!r) return std::unexpected(r.error().message());
    return ExecResult{r->status.shell_code(), std::move(r->out), std::move(r->err), r->timed_out,
                      r->out_truncated || r->err_truncated};
}

ProjectMcpp Client::probe(const std::filesystem::path& root, const RunOptions& base) const {
    ProjectMcpp out;
    out.program = program_.generic_string();
    RunOptions o = base;
    o.cwd = root;
    o.args = {"self", "env", "--format", "json"};
    auto r = envelope(o);
    if (!r) {
        out.state = "failed";
        out.message = r.error();
        return out;
    }
    const Json& e = r->envelope;
    if (e.is_object() && e.contains("mcpp") && e["mcpp"].is_object()) {
        out.state = "ok";
        out.version = e["mcpp"].value("version", "");
        if (e["mcpp"].contains("features") && e["mcpp"]["features"].is_array()) {
            for (const auto& f : e["mcpp"]["features"]) if (f.is_string()) out.features.push_back(f.get<std::string>());
        } else {
            out.features = frozen_features(out.version);
            out.featuresFrozen = true;
        }
        if (e.contains("data") && e["data"].is_object()) {
            out.env = e["data"];
            if (const auto program = out.env.value("program", ""); !program.empty()) out.program = program;
        }
        return out;
    }
    // No envelope: the shim refused (a pinned version that is not installed), or mcpp failed. The
    // shim's own words are the message; its install hint, when it gave one, is the action.
    out.message = r->narration.empty() ? r->output : r->narration;
    while (!out.message.empty() && (out.message.back() == '\n' || out.message.back() == '\r')) out.message.pop_back();
    const auto at = out.message.find("xlings install mcpp@");
    if (at != std::string::npos) {
        auto end = out.message.find_first_of(" \r\n'\"", at + 20);
        out.install = out.message.substr(at, end == std::string::npos ? std::string::npos : end - at);
        out.state = "pinned-missing";
    } else if (out.message.find("not installed") != std::string::npos || out.message.find("not found for 'mcpp'") != std::string::npos) {
        out.state = "pinned-missing";
    } else {
        out.state = "failed";
    }
    return out;
}

Json diagnostics_of(const Json& envelope) {
    Json out = Json::array();
    if (!envelope.is_object() || !envelope.contains("diagnostics") || !envelope["diagnostics"].is_array()) return out;
    for (const auto& d : envelope["diagnostics"]) {
        if (!d.is_object()) continue;
        Json n{{"severity", d.value("severity", "error")},
               {"message", d.value("message", "")},
               {"code", d.value("code", "")},
               {"source", "mcpp"}};
        if (d.contains("path") && d["path"].is_string()) n["path"] = d["path"];
        if (d.contains("range") && d["range"].is_object()) n["range"] = d["range"];
        out.push_back(std::move(n));
    }
    return out;
}

}  // namespace mcppide::mcpp
