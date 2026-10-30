// The session: one editor window's backend. It holds the mcpp it found, the
// projects of the window's folders, the jobs, and the watchers, and answers
// the methods of protocol/protocol.schema.json.
//
// THE ADAPTER DECIDES NOTHING. Which argv a build runs, how much confirmation a
// clean needs, what a cache panel shows, what a setting may hold, which
// commands an editor offers and what a test record means: answered here, so VS
// Code, CLion and Neovim behave alike and an agent reading the backend's log
// sees the same reasons a person sees in the editor.
//
// The editors (the protocol's `editor`): vscode (mcpp-vscode), jetbrains
// (mcpp-clion: the IntelliJ platform's id), nvim (mcpp-nvim), cli (an agent).
export module mcppide.server;

import std;
import nlohmann.json;
import mcppide.rpc;
import mcppide.mcpp;
import mcppide.jobs;
import mcppide.projects;
import mcppide.views;
import mcppide.policy;
import mcppide.settings;
import mcppide.i18n;
import mcppide.manifest;
import mcppide.spec;
import mcppide.languageservice;
import mcpplibs.os;

export namespace mcppide::server {

using Json = nlohmann::json;

inline constexpr std::string_view kVersion = "0.2.0";
inline constexpr int kProtocol = 1;

// The methods `install` registers, for the protocol schema's test: a method
// here and not in protocol/protocol.schema.json (or the other way) fails it.
inline constexpr std::array<std::string_view, 49> kMethods = {
    "initialize", "settings/update", "settings/registry", "settings/preset", "i18n/bundle",
    "selfCheck", "commands/list", "menu/quick", "languageService/status",
    "projects/list", "projects/forFile", "project/summary", "project/new", "result/last",
    "job/start", "job/cancel", "job/list", "run/launch", "debug/configuration", "test/list",
    "library/search", "library/indexes", "library/add", "library/updateIndex",
    "toolchains/list", "toolchains/install", "toolchains/setDefault", "targets/list",
    "cache/inventory", "cache/plan", "cache/apply", "graph/packages", "graph/modules",
    "pack/formats", "pack/start", "manifest/check", "toml/complete", "toml/hover", "toml/symbols",
    "buildscript/complete", "mcpp/exec", "project/files", "languageService/exec", "project/mcpp", "project/refresh",
    "capabilities/explain", "report/collect", "shutdown", "exit"};

// The notifications the backend sends.
inline constexpr std::array<std::string_view, 14> kNotifications = {
    "job/started", "job/output", "job/plan", "job/progress", "job/diagnostic", "job/artifact", "job/record",
    "job/finished", "test/result", "test/summary", "result/changed", "manifest/changed",
    "manifest/diagnostics", "project/mcppChanged"};

class Session {
public:
    explicit Session(rpc::Server& server);
    ~Session();
    void install();

    // The self-check report (also `mcpp-ide self-check`).
    Json self_check();

private:
    using Result = std::expected<Json, rpc::Error>;
    Result initialize(const Json& p);
    Result update_settings(const Json& p);
    Result job_start(const Json& p);
    Result run_launch(const Json& p, const rpc::Context& ctx);
    Result debug_configuration(const Json& p, const rpc::Context& ctx);
    Result library_search(const Json& p);
    Result cache_plan(const Json& p);
    Result cache_apply(const Json& p);
    Result graph(const Json& p, bool modules);
    Result project_summary(const Json& p);
    Result mcpp_exec(const Json& p);
    Result language_service_exec(const Json& p);
    Result project_new(const Json& p);
    Result manifest_check(const Json& p);
    Result language_service(const Json& p);
    Result targets(const Json& p);
    Result commands(const Json& p) const;
    Result quick_menu(const Json& p);
    Result test_list(const Json& p);
    Result project_mcpp(const Json& p);
    Result project_refresh(const Json& p);
    Result capabilities_explain(const Json& p);
    Result report_collect(const Json& p);

    // THE PROJECT'S mcpp (design 2026-10-30 part 2 §4.2): asked in the project, once, and asked again
    // when its manifest changes, when the editor regains focus, or when a job's envelope names
    // another version. Never more than two asked at once.
    mcpp::ProjectMcpp probe_of(const std::filesystem::path& root);
    Json probe_json(const std::filesystem::path& root, const mcpp::ProjectMcpp& probe) const;
    bool has_feature(const std::filesystem::path& root, std::string_view feature);
    // A line of the backend's log, at a level (0 error, 1 warning, 2 info, 3 debug): written to
    // standard error, which every editor keeps in its log, when mcpp.backend.logLevel lets it
    // through, and kept for the IDE report either way (§3.2).
    void log(int level, std::string_view text);

    std::expected<mcpp::Client::EnvelopeResult, rpc::Error> query(const std::filesystem::path& cwd,
                                                                    std::vector<std::string> args) const;
    std::expected<Json, rpc::Error> build_database(const std::filesystem::path& root);
    std::vector<std::string> deny() const;
    std::string client_label() const;
    std::string t(std::string_view key, const std::vector<std::string>& args = {}) const;
    Json setting(std::string_view key) const;
    // `--target <t>` for a request: its own `target`, else `mcpp.build.target`; nothing for the host.
    std::vector<std::string> target_args(const Json& p) const;
    std::filesystem::path any_root(const Json& p) const;
    void locate_mcpp();
    void on_changed(const std::filesystem::path& root, std::string_view what, const Json& detail);
    void on_record(std::string_view jobId, const Json& record);

    rpc::Server& rpc_;
    mutable std::mutex mutex_;
    mcpp::Client client_;
    std::optional<mcpp::Located> located_;
    std::string mcppError_;
    Json settings_ = Json::object();
    bool trusted_ = true;
    mutable std::mutex localeMutex_;
    std::string locale_ = "en";
    std::string clientName_ = "mcpp-ide";
    std::string editor_ = "vscode";
    std::vector<std::string> mcpplsCandidates_;
    std::vector<std::filesystem::path> folders_;
    std::unique_ptr<jobs::Jobs> jobs_;
    std::unique_ptr<projects::Watcher> watcher_;
    // `mcpp search` takes a few hundred milliseconds and answers the same
    // catalogue for minutes: kept until an index changes or the user asks.
    Json searchCache_;
    std::chrono::steady_clock::time_point searchAt_{};
    std::map<std::filesystem::path, Json> buildDatabases_;
    // The project and kind of each job, for the notifications its records become.
    std::map<std::string, std::pair<std::filesystem::path, std::string>, std::less<>> jobOf_;
    // The configuration (and platforms) the last plan of each project named.
    std::map<std::filesystem::path, Json> plans_;
    // What `mcpp pack` accepts, read from its help once per mcpp (SPEC-014: `--pkg-format` names the
    // package format since the platforms release; an earlier mcpp knows it as `--format`).
    struct PackOptions {
        std::filesystem::path program;
        std::string pkgFormatFlag = "--format";
        bool linkage = false;
    };
    std::optional<PackOptions> packOptions_;
    PackOptions pack_options(const std::filesystem::path& cwd);
    std::map<std::filesystem::path, mcpp::ProjectMcpp> probes_;
    std::counting_semaphore<2> probeSlots_{2};
    // The backend's log level (mcpp.backend.logLevel): what reaches standard error.
    std::deque<std::string> recentLog_;
};

}  // namespace mcppide::server

namespace mcppide::server {

namespace {
rpc::Error invalid(std::string message) { return rpc::Error{rpc::INVALID_PARAMS, std::move(message)}; }
std::filesystem::path root_of(const Json& p) {
    return p.contains("root") && p["root"].is_string() ? std::filesystem::path(p["root"].get<std::string>()) : std::filesystem::path{};
}
Json project_json(const projects::Project& p) {
    return Json{{"root", p.root.generic_string()}, {"workspaceRoot", p.workspaceRoot.generic_string()},
                {"name", p.root.filename().string()}, {"isWorkspaceRoot", p.isWorkspaceRoot}};
}
std::vector<std::string> strings_of(const Json& v) {
    std::vector<std::string> out;
    if (v.is_array()) for (const auto& x : v) if (x.is_string()) out.push_back(x.get<std::string>());
    return out;
}
}  // namespace

Session::Session(rpc::Server& server) : rpc_(server) {
    jobs_ = std::make_unique<jobs::Jobs>(mcpp::Client{}, jobs::Sink{
        .narration = [this](std::string_view id, std::string_view text) {
            rpc_.notify("job/output", Json{{"jobId", id}, {"text", text}});
        },
        .record = [this](std::string_view id, const Json& record) { on_record(id, record); },
        .finished = [this](std::string_view id, const Json& summary) {
            Json s = summary;
            s["jobId"] = id;
            // The envelope's own diagnostics and artifacts, as the adapters consume them.
            if (summary.contains("envelope")) {
                s["diagnostics"] = mcpp::diagnostics_of(summary["envelope"]);
                const auto& env = summary["envelope"];
                if (env.is_object() && env.contains("data") && env["data"].is_object() && env["data"].contains("artifacts"))
                    s["artifacts"] = env["data"]["artifacts"];
            }
            std::filesystem::path root;
            {
                std::lock_guard lock(mutex_);
                if (auto it = jobOf_.find(id); it != jobOf_.end()) {
                    s["root"] = it->second.first.generic_string();
                    s["kind"] = it->second.second;
                    root = it->second.first;
                    jobOf_.erase(it);
                }
            }
            rpc_.notify("job/finished", s);
            // Every envelope names the mcpp that wrote it: another version than the one probed (a
            // `xlings use`, a pin changed) is asked again, and the editor is told (§4.2).
            const auto& env = summary.contains("envelope") ? summary["envelope"] : Json(nullptr);
            if (!root.empty() && env.is_object() && env.contains("mcpp") && env["mcpp"].is_object()) {
                const auto seen = env["mcpp"].value("version", "");
                bool changed = false;
                {
                    std::lock_guard lock(mutex_);
                    if (auto it = probes_.find(root); it != probes_.end() && !seen.empty() && it->second.version != seen) {
                        probes_.erase(it);
                        changed = true;
                    }
                }
                if (changed) rpc_.notify("project/mcppChanged", Json{{"root", root.generic_string()}, {"version", seen}});
            }
        },
        .started = [this](std::string_view id, const Json& started) {
            {
                std::lock_guard lock(mutex_);
                jobOf_[std::string(id)] = {std::filesystem::path(started.value("root", "")), started.value("kind", "")};
            }
            Json s = started;
            s["jobId"] = id;
            rpc_.notify("job/started", s);
        },
    });
    watcher_ = std::make_unique<projects::Watcher>(
        [this](const std::filesystem::path& root, std::string_view what, const Json& detail) { on_changed(root, what, detail); });
}

Session::~Session() {
    watcher_.reset();
    jobs_.reset();
}

// The locale has a lock of its own: a message is often composed while `mutex_` is held.
std::string Session::t(std::string_view key, const std::vector<std::string>& args) const {
    std::string locale;
    {
        std::lock_guard lock(localeMutex_);
        locale = locale_;
    }
    return i18n::translate(key, locale, args);
}

Json Session::setting(std::string_view key) const {
    std::lock_guard lock(mutex_);
    if (auto it = settings_.find(std::string(key)); it != settings_.end()) return *it;
    if (auto* s = settings::find(key)) return settings::default_of(*s);
    return nullptr;
}

std::vector<std::string> Session::target_args(const Json& p) const {
    std::string target = p.contains("target") && p["target"].is_string() ? p["target"].get<std::string>() : std::string{};
    if (!p.contains("target")) {
        const auto chosen = setting("mcpp.build.target");
        if (chosen.is_string()) target = chosen.get<std::string>();
    }
    if (target.empty()) return {};
    return {"--target", target};
}

std::filesystem::path Session::any_root(const Json& p) const {
    if (auto r = root_of(p); !r.empty()) return r;
    std::lock_guard lock(mutex_);
    return folders_.empty() ? std::filesystem::current_path() : folders_.front();
}

std::vector<std::string> Session::deny() const {
    std::lock_guard lock(mutex_);
    if (trusted_) return {};
    return strings_of(settings_.value("mcpp.trust.denyEffects", Json::array()));
}

std::string Session::client_label() const {
    std::lock_guard lock(mutex_);
    return clientName_;
}

void Session::locate_mcpp() {
    std::string configured;
    {
        std::lock_guard lock(mutex_);
        configured = settings_.value("mcpp.path", "");
    }
    auto located = mcpp::locate(configured);
    std::lock_guard lock(mutex_);
    if (!located) {
        mcppError_ = located.error();
        located_.reset();
        client_ = mcpp::Client{};
        jobs_->set_client(client_);
        return;
    }
    auto protocol = mcpp::handshake(located->program);
    if (!protocol) {
        mcppError_ = protocol.error();
        located_.reset();
        client_ = mcpp::Client{};
    } else {
        mcppError_.clear();
        located_ = *located;
        client_ = mcpp::Client(located->program, *protocol);
    }
    probes_.clear();
    jobs_->set_client(client_);
    if (located && protocol)
        std::println(std::cerr, "[info] mcpp {} at {} ({})", protocol->version, located->program.generic_string(), located->source);
}

void Session::on_changed(const std::filesystem::path& root, std::string_view what, const Json& detail) {
    {
        std::lock_guard lock(mutex_);
        buildDatabases_.erase(root);
        if (what != "result") probes_.erase(root);   // a manifest may state another mcpp floor or pin
    }
    if (what == "result") {
        rpc_.notify("result/changed", Json{{"root", root.generic_string()}, {"result", detail}});
        return;
    }
    rpc_.notify("manifest/changed", Json{{"root", root.generic_string()}});
    // The manifest's problems, from mcpp, located (mcpp docs/50 §2).
    auto checked = manifest_check(Json{{"root", root.generic_string()}});
    if (checked) rpc_.notify("manifest/diagnostics", *checked);
}

// A record of a job's stream becomes the notification its meaning names: the plan (with the
// platforms mcpp reports, kept per project for the panels), progress, a located diagnostic, a test's
// result, the tests' summary. What has no meaning here goes out as it came (`job/record`).
void Session::on_record(std::string_view jobId, const Json& record) {
    auto n = views::stream_record(record);
    std::string root;
    {
        std::lock_guard lock(mutex_);
        if (auto it = jobOf_.find(jobId); it != jobOf_.end()) root = it->second.first.generic_string();
    }
    n["jobId"] = jobId;
    n["root"] = root;
    const auto type = n.value("type", "other");
    if (type == "plan") {
        {
            std::lock_guard lock(mutex_);
            plans_[std::filesystem::path(root)] = n;
        }
        rpc_.notify("job/plan", n);
    } else if (type == "progress") {
        rpc_.notify("job/progress", n);
    } else if (type == "diagnostic") {
        rpc_.notify("job/diagnostic", n);
    } else if (type == "artifact") {
        rpc_.notify("job/artifact", n);
    } else if (type == "test") {
        rpc_.notify("test/result", n);
    } else if (type == "summary") {
        rpc_.notify("test/summary", n);
    } else if (!(record.is_object() && record.contains("schemaVersion") && record.value("kind", "") != "mcpp.event")) {
        // The envelope that closes a stream reaches the adapter in `job/finished`.
        rpc_.notify("job/record", n);
    }
}

std::expected<mcpp::Client::EnvelopeResult, rpc::Error> Session::query(const std::filesystem::path& cwd,
                                                                       std::vector<std::string> args) const {
    mcpp::Client client;
    {
        std::lock_guard lock(mutex_);
        client = client_;
    }
    if (!client.ready()) return std::unexpected(rpc::Error{rpc::REQUEST_FAILED, mcppError_.empty() ? "mcpp is not available" : mcppError_});
    mcpp::RunOptions o;
    o.cwd = cwd;
    o.args = std::move(args);
    o.deny = deny();
    o.client = client_label();
    auto r = client.envelope(o);
    if (!r) return std::unexpected(rpc::Error{rpc::REQUEST_FAILED, r.error()});
    return *r;
}

// One command of mcpp, as an editor's own parsers read it (mcpp-vscode's views): what it printed and
// how it ended. The backend runs it as it runs every command -- the mcpp it located, the session's
// client, the effects an untrusted workspace denies -- bounded in time and in output.
std::expected<Json, rpc::Error> Session::mcpp_exec(const Json& p) {
    mcpp::Client client;
    {
        std::lock_guard lock(mutex_);
        client = client_;
    }
    if (!client.ready()) return std::unexpected(rpc::Error{rpc::REQUEST_FAILED, mcppError_.empty() ? "mcpp is not available" : mcppError_});
    if (!p.contains("args") || !p["args"].is_array()) return std::unexpected(rpc::Error{rpc::INVALID_PARAMS, "args: an array of strings"});
    mcpp::RunOptions o;
    for (const auto& a : p["args"]) {
        if (!a.is_string()) return std::unexpected(rpc::Error{rpc::INVALID_PARAMS, "args: an array of strings"});
        o.args.push_back(a.get<std::string>());
    }
    o.cwd = p.contains("cwd") && p["cwd"].is_string() ? std::filesystem::path(p["cwd"].get<std::string>()) : std::filesystem::path{};
    o.deny = deny();
    o.client = client_label();
    const auto timeoutMs = p.contains("timeoutMs") && p["timeoutMs"].is_number_integer() && p["timeoutMs"].get<std::int64_t>() > 0
                               ? p["timeoutMs"].get<std::int64_t>() : std::int64_t{24} * 3600 * 1000;
    const auto limit = p.contains("maxOutputBytes") && p["maxOutputBytes"].is_number_integer() && p["maxOutputBytes"].get<std::int64_t>() > 0
                           ? static_cast<std::size_t>(p["maxOutputBytes"].get<std::int64_t>()) : std::size_t{16} << 20;
    auto r = client.exec(o, std::chrono::milliseconds(timeoutMs), limit);
    if (!r) return std::unexpected(rpc::Error{rpc::REQUEST_FAILED, r.error()});
    return Json{{"exitCode", r->exitCode}, {"stdout", r->out}, {"stderr", r->err}, {"timedOut", r->timedOut},
                {"truncated", r->truncated}};
}

// One command of mcppls (`query module`, for the module graph), run as the command languageService/status
// found: a payload's runner and image, or a program. Bounded as mcpp/exec is.
std::expected<Json, rpc::Error> Session::language_service_exec(const Json& p) {
    if (!p.contains("args") || !p["args"].is_array()) return std::unexpected(rpc::Error{rpc::INVALID_PARAMS, "args: an array of strings"});
    std::vector<std::string> candidates;
    {
        std::lock_guard lock(mutex_);
        candidates = mcpplsCandidates_;
    }
    const auto found = languageservice::probe(candidates);
    if (found.command.empty())
        return std::unexpected(rpc::Error{rpc::REQUEST_FAILED, t("mcppls was not found: C++ files have no completion, navigation or diagnostics from mcpp's build. Install the C++ Modules language server ({0} or later).",
                                                                  {std::string(languageservice::kRequired)})});
    mcpplibs::os::process::spawn_options o;
    o.program = found.command.front();
    for (std::size_t i = 1; i < found.command.size(); ++i) o.args.push_back(found.command[i].string());
    for (const auto& a : p["args"]) {
        if (!a.is_string()) return std::unexpected(rpc::Error{rpc::INVALID_PARAMS, "args: an array of strings"});
        o.args.push_back(a.get<std::string>());
    }
    o.in = mcpplibs::os::process::stdio::null;
    if (p.contains("cwd") && p["cwd"].is_string()) o.cwd = p["cwd"].get<std::string>();
    mcpplibs::os::process::bounds limits;
    limits.hard = std::chrono::milliseconds(p.contains("timeoutMs") && p["timeoutMs"].is_number_integer() && p["timeoutMs"].get<std::int64_t>() > 0
                                                ? p["timeoutMs"].get<std::int64_t>() : std::int64_t{120000});
    const auto limit = p.contains("maxOutputBytes") && p["maxOutputBytes"].is_number_integer() && p["maxOutputBytes"].get<std::int64_t>() > 0
                           ? static_cast<std::size_t>(p["maxOutputBytes"].get<std::int64_t>()) : std::size_t{16} << 20;
    limits.out_limit = limit;
    limits.err_limit = limit;
    auto r = mcpplibs::os::process::run(o, limits);
    if (!r) return std::unexpected(rpc::Error{rpc::REQUEST_FAILED, r.error().message()});
    return Json{{"exitCode", r->status.shell_code()}, {"stdout", r->out}, {"stderr", r->err}, {"timedOut", r->timed_out},
                {"truncated", r->out_truncated || r->err_truncated}};
}

std::expected<Json, rpc::Error> Session::build_database(const std::filesystem::path& root) {
    {
        std::lock_guard lock(mutex_);
        if (auto it = buildDatabases_.find(root); it != buildDatabases_.end()) return it->second;
    }
    auto r = query(root, {"emit", "build-database", "--format", "json"});
    if (!r) return std::unexpected(r.error());
    if (r->envelope.is_null())
        return std::unexpected(rpc::Error{rpc::REQUEST_FAILED, t("mcpp emit build-database printed no document")});
    std::lock_guard lock(mutex_);
    buildDatabases_[root] = r->envelope;
    return r->envelope;
}

Json Session::self_check() {
    Json mcppPart = nullptr;
    std::string error;
    {
        std::lock_guard lock(mutex_);
        error = mcppError_;
        if (client_.ready()) {
            const auto& p = client_.protocol();
            mcppPart = Json{{"path", client_.program().generic_string()},
                            {"source", located_ ? located_->source : ""},
                            {"version", p.version},
                            {"contract", p.contract},
                            {"contractFloor", mcpp::kContractFloor},
                            {"kinds", p.kinds}};
        }
    }
    Json checks = Json::array();
    auto add = [&](std::string id, std::string status, std::string message) {
        checks.push_back(Json{{"id", id}, {"status", status}, {"message", message}});
    };
    if (mcppPart.is_null()) add("mcpp", "error", error.empty() ? t("mcpp was not found") : error);
    else {
        add("mcpp", "ok", t("mcpp {0} at {1}", {mcppPart["version"].get<std::string>(), mcppPart["path"].get<std::string>()}));
        if (!mcppPart["contract"].get<bool>())
            add("contract", "warning",
                t("mcpp {0} predates the control contract ({1}): builds run without events, records or launch descriptors",
                  {mcppPart["version"].get<std::string>(), std::string(mcpp::kContractFloor)}));
        else add("contract", "ok", t("the control contract is available"));
    }
    std::vector<std::string> candidates;
    {
        std::lock_guard lock(mutex_);
        candidates = mcpplsCandidates_;
    }
    auto ls = language_service(Json{{"candidates", candidates}});
    if (ls) add("languageService", (*ls)["state"] == "ok" ? "ok" : (*ls)["state"] == "missing" ? "error" : "warning",
                (*ls)["message"].get<std::string>());
    if (auto xlings = mcpplibs::os::env::find_executable("xlings")) add("xlings", "ok", t("xlings at {0}", {xlings->generic_string()}));
    else add("xlings", "info", t("xlings is not on PATH; mcpp uses the one it bundles"));
    {
        std::lock_guard lock(mutex_);
        add("trust", trusted_ ? "ok" : "info",
            trusted_ ? t("the workspace is trusted") : t("the workspace is not trusted: mcpp runs with effects denied"));
    }
    return Json{{"backend", Json{{"version", kVersion}, {"protocol", kProtocol}}},
                {"mcpp", mcppPart},
                {"languageService", ls ? *ls : Json(nullptr)},
                {"checks", checks}};
}

std::expected<Json, rpc::Error> Session::initialize(const Json& p) {
    {
        std::lock_guard lock(mutex_);
        if (p.contains("client") && p["client"].is_object()) {
            const auto name = p["client"].value("name", "editor");
            const auto version = p["client"].value("version", "");
            clientName_ = std::format("mcpp-ide/{} ({}{}{})", kVersion, name, version.empty() ? "" : " ", version);
        }
        editor_ = p.value("editor", "vscode");
        {
            std::lock_guard localeLock(localeMutex_);
            locale_ = i18n::normalize_locale(p.value("locale", "en"));
        }
        trusted_ = p.value("trusted", true);
        settings_ = settings::resolve(p.contains("settings") ? p["settings"] : Json::object())["values"];
        folders_.clear();
        for (const auto& f : strings_of(p.value("workspaceFolders", Json::array()))) folders_.emplace_back(f);
        if (p.contains("languageService") && p["languageService"].is_object())
            mcpplsCandidates_ = strings_of(p["languageService"].value("candidates", Json::array()));
    }
    locate_mcpp();
    Json found = Json::array();
    for (const auto& folder : folders_)
        for (const auto& proj : projects::discover(folder)) {
            watcher_->watch(proj.root);
            found.push_back(project_json(proj));
        }
    rpc_.set_initialized(true);
    auto check = self_check();
    return Json{{"server", Json{{"name", "mcpp-ide"}, {"version", kVersion}}},
                {"protocol", kProtocol},
                {"mcpp", check["mcpp"]},
                {"languageService", check["languageService"]},
                {"checks", check["checks"]},
                {"projects", found},
                {"capabilities", Json::array({"jobs", "results", "launch", "debug", "tests", "library", "toolchains",
                                              "targets", "platforms", "cache", "graphs", "pack", "settings", "i18n",
                                              "commands", "manifest", "toml", "buildscript", "languageService"})}};
}

std::expected<Json, rpc::Error> Session::update_settings(const Json& p) {
    Json resolved = settings::resolve(p.contains("settings") ? p["settings"] : Json::object());
    std::string before;
    {
        std::lock_guard lock(mutex_);
        before = settings_.value("mcpp.path", "");
        settings_ = resolved["values"];
        if (p.contains("trusted")) trusted_ = p["trusted"].get<bool>();
        if (p.contains("locale")) {
            std::lock_guard localeLock(localeMutex_);
            locale_ = i18n::normalize_locale(p["locale"].get<std::string>());
        }
    }
    if (before != resolved["values"].value("mcpp.path", "")) locate_mcpp();
    return resolved;
}

std::expected<Json, rpc::Error> Session::job_start(const Json& p) {
    const auto kindName = p.value("kind", "");
    auto kind = jobs::kind_from(kindName);
    if (!kind) return std::unexpected(invalid(std::format("unknown job kind {}", kindName)));
    const auto root = root_of(p);
    if (root.empty()) return std::unexpected(invalid("a job names its project's root"));
    {
        std::lock_guard lock(mutex_);
        if (!client_.ready()) return std::unexpected(rpc::Error{rpc::REQUEST_FAILED, mcppError_});
    }
    jobs::Request r;
    r.kind = *kind;
    r.root = root;
    r.deny = deny();
    r.client = client_label();
    r.supersede = p.value("supersede", false);
    r.color = p.value("color", true);
    const auto extra = strings_of(p.value("args", Json::array()));
    const auto profile = p.value("profile", setting("mcpp.build.profile").is_string() ? setting("mcpp.build.profile").get<std::string>() : "dev");
    const bool release = profile == "release";
    const auto target = target_args(p);
    auto append = [&](const std::vector<std::string>& v) { r.args.insert(r.args.end(), v.begin(), v.end()); };
    switch (*kind) {
        case jobs::Kind::build:
            r.args = {"build", "--message-format", "json"};
            if (release) r.args.push_back("--release");
            append(target);
            append(strings_of(setting("mcpp.task.buildArgs")));
            r.mode = jobs::Mode::ndjson;
            break;
        case jobs::Kind::test:
            r.args = {"test", "--message-format", "json"};
            if (release) r.args.push_back("--release");
            append(target);
            if (p.contains("filter") && p["filter"].is_string() && !p["filter"].get<std::string>().empty())
                r.args.push_back(p["filter"].get<std::string>());
            append(strings_of(setting("mcpp.task.testArgs")));
            r.mode = jobs::Mode::ndjson;
            break;
        case jobs::Kind::pack: {
            const auto options = pack_options(root);
            r.args = {"pack"};
            if (auto f = p.value("pkgFormat", ""); !f.empty()) { r.args.push_back(options.pkgFormatFlag); r.args.push_back(f); }
            if (auto m = p.value("mode", ""); !m.empty()) { r.args.push_back("--mode"); r.args.push_back(m); }
            if (auto l = p.value("linkage", ""); !l.empty() && options.linkage) { r.args.push_back("--linkage"); r.args.push_back(l); }
            if (release) r.args.push_back("--release");
            append(target);
            r.args.push_back("--message-format");
            r.args.push_back("json");
            r.mode = jobs::Mode::envelope;
            break;
        }
        case jobs::Kind::clean:
            return std::unexpected(invalid("a clean goes through cache/apply, which holds its confirmation"));
        case jobs::Kind::toolchain: {
            const auto spec = p.value("spec", "");
            if (spec.empty()) return std::unexpected(invalid("a toolchain job names a spec"));
            r.args = {"toolchain", "install", spec};
            if (p.contains("target") && p["target"].is_string() && !p["target"].get<std::string>().empty()) {
                r.args.push_back("--target");
                r.args.push_back(p["target"].get<std::string>());
            }
            break;
        }
        case jobs::Kind::add: {
            const auto dep = p.value("dependency", "");
            if (dep.empty()) return std::unexpected(invalid("`mcpp add` names a dependency"));
            r.args = {"add", dep};
            break;
        }
        case jobs::Kind::command:
            r.args = {};
            break;
    }
    for (const auto& a : extra) r.args.push_back(a);
    if (r.args.empty()) return std::unexpected(invalid("a command job names its arguments"));
    const auto id = jobs_->submit(std::move(r));
    watcher_->watch(root);
    return Json{{"jobId", id}};
}

std::expected<Json, rpc::Error> Session::run_launch(const Json& p, const rpc::Context& ctx) {
    const auto root = root_of(p);
    if (root.empty()) return std::unexpected(invalid("run/launch names its project's root"));
    std::vector<std::string> args{"run", "--print-launch"};
    const auto profile = p.value("profile", setting("mcpp.build.profile").is_string() ? setting("mcpp.build.profile").get<std::string>() : "dev");
    if (profile == "release") args.push_back("--release");
    for (const auto& a : target_args(p)) args.push_back(a);
    if (auto bin = p.value("bin", ""); !bin.empty()) args.push_back(bin);
    auto r = query(root, std::move(args));
    if (!r) return std::unexpected(r.error());
    if (ctx.cancelled()) return std::unexpected(rpc::Error{rpc::REQUEST_CANCELLED, "cancelled"});
    if (r->exitCode != 0 || !r->envelope.is_object() || r->envelope.value("kind", "") != "mcpp.launch")
        return std::unexpected(rpc::Error{rpc::REQUEST_FAILED, t("the program could not be built"),
                                          Json{{"exitCode", r->exitCode}, {"narration", r->narration},
                                               {"diagnostics", mcpp::diagnostics_of(r->envelope)}}});
    Json launch = r->envelope["data"];
    // A program in a terminal: the user's arguments after mcpp's own (a runner's, an image's).
    for (const auto& a : strings_of(p.value("args", Json::array()))) launch["argv"].push_back(a);
    return launch;
}

// A debug session for what `run/launch` describes, in the adapter's debugger's own configuration.
//
// A PROGRAM THAT RUNS THROUGH ANOTHER (`via`: an ok image under `okrun`, a target's runner) is
// debugged as that runner, with the image's symbols arriving through the GDB JIT interface: okrun
// registers each image it loads (`__jit_debug_register_code`), which gdb reads by default and lldb
// reads once its JIT loader is on. Breakpoints in the image's sources then bind when it is loaded.
std::expected<Json, rpc::Error> Session::debug_configuration(const Json& p, const rpc::Context& ctx) {
    auto launch = run_launch(p, ctx);
    if (!launch) return launch;
    const auto& l = *launch;
    std::string engine = p.value("engine", setting("mcpp.debug.engine").is_string() ? setting("mcpp.debug.engine").get<std::string>() : "auto");
    const auto available = strings_of(p.value("available", Json::array()));
    auto has = [&](std::string_view e) { return std::ranges::find(available, e) != available.end(); };
    if (engine == "auto") {
        if (has("lldb")) engine = "lldb";
        else if (has("cppdbg")) engine = "cppdbg";
        else if (has("lldb-dap")) engine = "lldb-dap";
        else engine = "cppdbg";
    }
    const auto argv = strings_of(l.value("argv", Json::array()));
    const std::string program = argv.empty() ? l.value("program", "") : argv.front();
    std::vector<std::string> args(argv.size() > 1 ? argv.begin() + 1 : argv.end(), argv.end());
    Json env = l.value("env", Json::object());
    const bool through = !l.value("via", Json(nullptr)).is_null();
    const auto jit = std::string("settings set plugin.jit-loader.gdb.enable on");
    Json cfg{{"name", std::format("mcpp: {}", std::filesystem::path(l.value("program", program)).filename().string())},
             {"request", "launch"}, {"program", program}, {"args", args}, {"cwd", l.value("cwd", root_of(p).generic_string())}};
    if (engine == "cppdbg") {
        cfg["type"] = "cppdbg";
        Json envList = Json::array();
        for (auto it = env.begin(); it != env.end(); ++it) envList.push_back(Json{{"name", it.key()}, {"value", it.value()}});
        cfg["environment"] = envList;
        cfg["MIMode"] = p.value("platform", "linux") == "macos" ? "lldb" : "gdb";
        cfg["externalConsole"] = false;
        if (through && cfg["MIMode"] == "lldb") cfg["setupCommands"] = Json::array({Json{{"text", jit}}});
    } else if (engine == "lldb") {
        cfg["type"] = "lldb";
        cfg["env"] = env;
        if (through) cfg["initCommands"] = Json::array({jit});
    } else if (engine == "lldb-dap") {
        cfg["type"] = "lldb-dap";
        Json envList = Json::array();
        for (auto it = env.begin(); it != env.end(); ++it) envList.push_back(std::format("{}={}", it.key(), it.value().get<std::string>()));
        cfg["env"] = envList;
        if (through) cfg["initCommands"] = Json::array({jit});
    } else if (engine == "gdb") {
        cfg["type"] = "gdb";
        cfg["env"] = env;
    } else {
        return std::unexpected(invalid(std::format("unknown debug engine {}", engine)));
    }
    if (through)
        cfg["mcppNote"] = t("this program runs through {0}; the debugger starts {0} and reads the program's symbols when it is loaded",
                            {l["via"].is_string() ? l["via"].get<std::string>() : program});
    return Json{{"engine", engine}, {"configuration", cfg}, {"launch", l}};
}

std::expected<Json, rpc::Error> Session::library_search(const Json& p) {
    const bool refresh = p.value("refresh", false);
    Json envelope;
    {
        std::lock_guard lock(mutex_);
        if (!refresh && !searchCache_.is_null() && std::chrono::steady_clock::now() - searchAt_ < std::chrono::minutes(10))
            envelope = searchCache_;
    }
    if (envelope.is_null()) {
        auto r = query(any_root(p), {"search", "--format", "json"});
        if (!r) return std::unexpected(r.error());
        if (r->envelope.is_null() || r->envelope.value("kind", "") != "mcpp.search")
            return std::unexpected(rpc::Error{rpc::REQUEST_FAILED, t("mcpp search printed no catalogue"),
                                              Json{{"narration", r->narration}}});
        std::lock_guard lock(mutex_);
        searchCache_ = r->envelope;
        searchAt_ = std::chrono::steady_clock::now();
        envelope = searchCache_;
    }
    if (p.contains("id")) {
        auto detail = views::library_detail(envelope, p["id"].get<std::string>());
        if (detail.is_null()) return std::unexpected(rpc::Error{rpc::REQUEST_FAILED, t("no such package")});
        return detail;
    }
    views::LibraryQuery q;
    q.keyword = p.value("keyword", "");
    q.index = p.value("index", "");
    q.sort = p.value("sort", "name");
    q.limit = p.value("limit", 500);
    return views::library(envelope, q);
}

namespace {
policy::CleanOptions clean_options(const Json& p, const Json& settings) {
    policy::CleanOptions o;
    o.staleDays = p.value("staleDays", settings.value("mcpp.cache.staleDays", 3));
    o.budgetGiB = p.value("budgetGiB", settings.value("mcpp.cache.gc.defaultBudgetGiB", 0.0));
    o.pruneAgeDays = p.value("pruneAgeDays", settings.value("mcpp.cache.pruneAgeDays", 30));
    return o;
}
}  // namespace

std::expected<Json, rpc::Error> Session::cache_plan(const Json& p) {
    Json settings;
    {
        std::lock_guard lock(mutex_);
        settings = settings_;
    }
    const auto o = clean_options(p, settings);
    auto plan = policy::plan(p.value("action", ""), o);
    if (!plan) return std::unexpected(invalid(std::format("unknown cache action {}", p.value("action", ""))));
    Json out = policy::to_json(*plan);
    out["title"] = t(plan->titleKey);
    out["detail"] = t(plan->detailKey, {std::to_string(plan->action == "stale" ? o.staleDays : o.pruneAgeDays)});
    {
        std::lock_guard lock(mutex_);
        out["refusedUntrusted"] = plan->requiresTrust && !trusted_;
    }
    if (plan->preview) {
        const auto root = any_root(p);
        if (plan->previewSource == "mcpp") {
            auto r = query(root, plan->previewArgv);
            if (r) out["preview"] = Json{{"source", "mcpp"}, {"text", r->narration}};
        } else {
            auto r = query(root, {"cache", "list", "--format", "json"});
            if (r) out["preview"] = views::cache_preview(r->envelope, *plan, o);
        }
    }
    return out;
}

std::expected<Json, rpc::Error> Session::cache_apply(const Json& p) {
    Json settings;
    {
        std::lock_guard lock(mutex_);
        settings = settings_;
    }
    auto plan = policy::plan(p.value("action", ""), clean_options(p, settings));
    if (!plan) return std::unexpected(invalid(std::format("unknown cache action {}", p.value("action", ""))));
    {
        std::lock_guard lock(mutex_);
        if (plan->requiresTrust && !trusted_)
            return std::unexpected(rpc::Error{rpc::REQUEST_FAILED, t("this action needs a trusted workspace")});
    }
    if (!policy::confirmed(*plan, p.value("confirmation", Json::object())))
        return std::unexpected(rpc::Error{rpc::REQUEST_FAILED, t("this action was not confirmed"), policy::to_json(*plan)});
    jobs::Request r;
    r.kind = plan->group == "clean" ? jobs::Kind::clean : jobs::Kind::command;
    r.root = any_root(p);
    r.args = plan->argv;
    r.client = client_label();
    return Json{{"jobId", jobs_->submit(std::move(r))}};
}

std::expected<Json, rpc::Error> Session::graph(const Json& p, bool modules) {
    const auto root = root_of(p);
    if (root.empty()) return std::unexpected(invalid("a graph names its project's root"));
    auto db = build_database(root);
    if (!db) return std::unexpected(db.error());
    return modules ? views::module_graph(*db) : views::package_graph(*db);
}

std::expected<Json, rpc::Error> Session::project_summary(const Json& p) {
    const auto root = root_of(p);
    if (root.empty()) return std::unexpected(invalid("project/summary names its project's root"));
    auto db = build_database(root);
    Json summary = views::project_summary(db ? *db : Json(nullptr), projects::last_result(root));
    if (!db) summary["error"] = db.error().message;
    summary["root"] = root.generic_string();
    summary["target"] = setting("mcpp.build.target");
    std::lock_guard lock(mutex_);
    if (auto it = plans_.find(root); it != plans_.end()) {
        summary["platform"] = (*it).second["platform"];
        summary["platformText"] = (*it).second["platformText"];
        summary["hostPlatform"] = (*it).second["hostPlatform"];
        summary["hostPlatformText"] = (*it).second["hostPlatformText"];
        summary["configuration"] = (*it).second["configuration"];
    }
    return summary;
}

std::expected<Json, rpc::Error> Session::project_new(const Json& p) {
    const auto parent = std::filesystem::path(p.value("parent", ""));
    const auto name = p.value("name", "");
    if (parent.empty() || name.empty()) return std::unexpected(invalid("project/new names a parent directory and a name"));
    if (name.find_first_of("/\\") != std::string::npos || name == "." || name == "..")
        return std::unexpected(invalid(t("a project's name is one directory name: {0}", {name})));
    std::vector<std::string> args{"new", name};
    if (auto tpl = p.value("template", ""); !tpl.empty()) { args.push_back("--template"); args.push_back(tpl); }
    auto started = job_start(Json{{"kind", "command"}, {"root", parent.generic_string()}, {"args", args}, {"color", false}});
    if (!started) return started;
    (*started)["root"] = (parent / name).generic_string();
    return started;
}

std::expected<Json, rpc::Error> Session::manifest_check(const Json& p) {
    const auto root = root_of(p);
    if (root.empty()) return std::unexpected(invalid("manifest/check names its project's root"));
    auto r = query(root, {"emit", "build-database", "--format", "json"});
    if (!r) return std::unexpected(r.error());
    Json diags = Json::array();
    for (auto d : mcpp::diagnostics_of(r->envelope)) {
        // Only what points into a manifest belongs on the manifest.
        const auto path = d.value("path", "");
        if (!path.empty() && !path.ends_with("mcpp.toml")) continue;
        if (path.empty()) d["path"] = (root / "mcpp.toml").generic_string();
        else if (!std::filesystem::path(path).is_absolute()) d["path"] = (root / path).generic_string();
        diags.push_back(std::move(d));
    }
    if (r->exitCode == 0) {
        std::lock_guard lock(mutex_);
        buildDatabases_[root] = r->envelope;
    }
    return Json{{"root", root.generic_string()}, {"diagnostics", diags}};
}

std::expected<Json, rpc::Error> Session::language_service(const Json& p) {
    std::vector<std::string> candidates = strings_of(p.value("candidates", Json::array()));
    {
        std::lock_guard lock(mutex_);
        if (candidates.empty()) candidates = mcpplsCandidates_;
        else mcpplsCandidates_ = candidates;
    }
    const auto s = languageservice::probe(candidates);
    const std::string required(languageservice::kRequired);
    Json command = Json::array();
    for (const auto& part : s.command) command.push_back(part.generic_string());
    Json out{{"state", languageservice::state_name(s.state)}, {"program", s.program.generic_string()},
             {"command", command}, {"source", s.source}, {"version", s.version}, {"required", required}};
    Json actions = Json::array();
    switch (s.state) {
        case languageservice::State::ok:
            out["message"] = t("mcppls {0} at {1}", {s.version, s.program.generic_string()});
            break;
        case languageservice::State::outdated:
            out["message"] = t("mcppls {0} predates mcpp's result record: a build does not refresh it. Update mcppls to {1} or later.",
                               {s.version, required});
            actions.push_back(Json{{"id", "update"}, {"title", t("Update mcppls")}});
            break;
        case languageservice::State::missing:
            out["message"] = t("mcppls was not found: C++ files have no completion, navigation or diagnostics from mcpp's build. Install the C++ Modules language server ({0} or later).",
                               {required});
            actions.push_back(Json{{"id", "install"}, {"title", t("Install mcppls")}});
            break;
        case languageservice::State::unreadable:
            out["message"] = t("mcppls at {0} did not report its version: {1}", {s.program.generic_string(), s.detail});
            actions.push_back(Json{{"id", "update"}, {"title", t("Update mcppls")}});
            break;
    }
    out["actions"] = actions;
    return out;
}

mcpp::ProjectMcpp Session::probe_of(const std::filesystem::path& rootIn) {
    const auto root = rootIn.lexically_normal();
    mcpp::Client client;
    bool trusted, fromSetting;
    {
        std::lock_guard lock(mutex_);
        if (auto it = probes_.find(root); it != probes_.end()) return it->second;
        client = client_;
        trusted = trusted_;
        fromSetting = located_ && located_->source == "setting";
    }
    mcpp::ProjectMcpp probe;
    if (!client.ready()) {
        probe.state = "failed";
        std::lock_guard lock(mutex_);
        probe.message = mcppError_.empty() ? t("mcpp is not available") : mcppError_;
        return probe;
    }
    if (!trusted) {
        // A pinned program is not run in an untrusted workspace: the global mcpp's facts stand in,
        // and are labelled as not asked in the project (§4.2).
        probe.state = "not-probed";
        probe.version = client.protocol().version;
        probe.program = client.program().generic_string();
        probe.features = client.protocol().features;
        probe.message = t("the workspace is not trusted: the project's mcpp is not asked");
    } else {
        mcpp::RunOptions o;
        o.deny = deny();
        o.client = client_label();
        probeSlots_.acquire();
        probe = client.probe(root, o);
        probeSlots_.release();
    }
    probe.fromSetting = fromSetting;
    log(probe.state == "ok" || probe.state == "not-probed" ? 2 : 1,
        std::format("project {}: mcpp {} ({}){}{}", root.generic_string(), probe.version.empty() ? "?" : probe.version,
                    probe.state, probe.program.empty() ? "" : " at " + probe.program,
                    probe.message.empty() ? "" : ": " + probe.message));
    std::lock_guard lock(mutex_);
    probes_[root] = probe;
    return probe;
}

bool Session::has_feature(const std::filesystem::path& root, std::string_view feature) {
    const auto probe = probe_of(root);
    return std::ranges::find(probe.features, feature) != probe.features.end();
}

void Session::log(int level, std::string_view text) {
    static constexpr std::string_view kNames[] = {"error", "warning", "info", "debug"};
    const auto configured = setting("mcpp.backend.logLevel");
    int limit = 2;
    if (configured.is_string())
        for (int i = 0; i < 4; ++i) if (configured.get<std::string>() == kNames[i]) limit = i;
    const auto line = std::format("[{}] {}", kNames[std::clamp(level, 0, 3)], text);
    {
        std::lock_guard lock(mutex_);
        recentLog_.push_back(line);
        while (recentLog_.size() > 200) recentLog_.pop_front();
    }
    if (level <= limit) std::println(std::cerr, "{}", line);
}

Json Session::probe_json(const std::filesystem::path& root, const mcpp::ProjectMcpp& probe) const {
    Json out{{"root", root.generic_string()},
             {"state", probe.state},
             {"version", probe.version},
             {"program", probe.program},
             {"features", probe.features},
             {"featuresFrozen", probe.featuresFrozen},
             {"fromSetting", probe.fromSetting}};
    if (!probe.message.empty()) out["message"] = probe.message;
    if (!probe.install.empty()) out["install"] = probe.install;
    for (const auto* key : {"mcppHome", "defaultToolchain", "xlings", "registry"})
        if (probe.env.contains(key)) out[key] = probe.env[key];
    return out;
}

// What the project's mcpp is (§4.4): the version and the program that ran in the project, whether
// mcpp.path decided it, what it states it can do, and the facts of `self env` the panels show.
std::expected<Json, rpc::Error> Session::project_mcpp(const Json& p) {
    const auto root = any_root(p).lexically_normal();
    return probe_json(root, probe_of(root));
}

// The editor regained focus, or asks again: every project is asked again on its next use.
std::expected<Json, rpc::Error> Session::project_refresh(const Json& p) {
    std::lock_guard lock(mutex_);
    if (const auto root = root_of(p); !root.empty()) probes_.erase(root.lexically_normal());
    else probes_.clear();
    return Json::object();
}

// WHY A FEATURE IS NOT OFFERED (§3.2): each feature the panels gate, whether it is available for
// the project, and if not which party lacks what -- the project's mcpp, mcppls -- and from which
// version. The panels' tooltips and selfCheck read this one answer.
std::expected<Json, rpc::Error> Session::capabilities_explain(const Json& p) {
    const auto root = any_root(p).lexically_normal();
    const auto probe = probe_of(root);
    struct Gate { const char* id; const char* feature; const char* since; };
    static constexpr Gate kGates[] = {
        {"targets.sets", "target-sets", "2026.10.30.1"},
        {"pack.formatList", "pkg-format-list", "2026.10.30.1"},
    };
    Json features = Json::array();
    for (const auto& g : kGates) {
        const bool on = std::ranges::find(probe.features, g.feature) != probe.features.end();
        Json f{{"id", g.id}, {"available", on}};
        if (!on)
            f["reason"] = Json{{"source", "mcpp"},
                               {"capability", g.feature},
                               {"need", g.since},
                               {"have", probe.version},
                               {"message", probe.state == "ok" || probe.state == "not-probed"
                                               ? t("the project's mcpp {0} does not state {1}; mcpp {2} does",
                                                   {probe.version, g.feature, g.since})
                                               : probe.message}};
        features.push_back(std::move(f));
    }
    return Json{{"root", root.generic_string()}, {"mcpp", probe_json(root, probe)}, {"features", features}};
}

// THE IDE REPORT (§3.2): what a bug report needs, with no secret in it -- the backend and its
// protocol, every project's mcpp, mcppls's status, the settings that are in effect (paths shown,
// nothing else), the recent jobs and the backend's recent log.
std::expected<Json, rpc::Error> Session::report_collect(const Json& p) {
    Json projects = Json::array();
    std::vector<std::filesystem::path> roots;
    {
        std::lock_guard lock(mutex_);
        for (const auto& [root, _] : probes_) roots.push_back(root);
    }
    if (const auto root = root_of(p); !root.empty() && std::ranges::find(roots, root.lexically_normal()) == roots.end())
        roots.push_back(root.lexically_normal());
    for (const auto& r : roots) projects.push_back(probe_json(r, probe_of(r)));
    std::vector<std::string> candidates;
    Json settings, log = Json::array();
    {
        std::lock_guard lock(mutex_);
        candidates = mcpplsCandidates_;
        settings = settings_;
        for (const auto& line : recentLog_) log.push_back(line);
    }
    auto ls = language_service(Json{{"candidates", candidates}});
    return Json{{"kind", "mcpp-ide.report"},
                {"version", 1},
                {"backend", Json{{"version", kVersion}, {"protocol", kProtocol}, {"windows", !std::string(mcpplibs::os::conventions().exe_suffix).empty()}}},
                {"selfCheck", self_check()},
                {"projects", projects},
                {"languageService", ls ? *ls : Json(nullptr)},
                {"settings", settings},
                {"jobs", jobs_->list()},
                {"log", log}};
}

std::expected<Json, rpc::Error> Session::targets(const Json& p) {
    auto r = query(any_root(p), {"toolchain", "list", "--format", "json"});
    if (!r) return std::unexpected(r.error());
    const auto chosen = p.contains("selected") && p["selected"].is_string() ? p["selected"].get<std::string>()
                      : setting("mcpp.build.target").is_string() ? setting("mcpp.build.target").get<std::string>() : std::string{};
    // Sets are named on the command line from mcpp 2026.10.30.1 on (SPEC-015).
    bool sets;
    {
        std::lock_guard lock(mutex_);
        sets = client_.ready();
    }
    // By what the project's mcpp states, not by its version (§4.3).
    sets = sets && has_feature(any_root(p), "target-sets");
    Json out = views::targets(r->envelope, chosen, sets);
    if (const auto root = root_of(p); !root.empty()) {
        std::lock_guard lock(mutex_);
        if (auto it = plans_.find(root); it != plans_.end()) out["lastPlan"] = it->second;
    }
    return out;
}

// The command table for one editor, in the user's language: what the adapter registers at run time
// beside the declarations generated from the same table (spec/commands.json), or, in Neovim, which
// declares nothing, all it offers. Each row names its subcommand, the name a command line types.
std::expected<Json, rpc::Error> Session::commands(const Json& p) const {
    std::string editor, locale;
    {
        std::lock_guard lock(mutex_);
        editor = p.value("editor", editor_);
    }
    {
        std::lock_guard lock(localeMutex_);
        locale = locale_;
    }
    const auto& table = spec::commands();
    Json groups = Json::object();
    for (const auto& g : table["groups"]) groups[g.value("id", "")] = spec::manifest_string(g.value("title", ""), locale);
    Json out = Json::array();
    for (const auto& c : table["commands"]) {
        if (!spec::offered_in(c, editor)) continue;
        Json row = c;
        const auto id = c.value("id", "");
        row["title"] = spec::manifest_string(std::format("command.{}.title", id), locale);
        row["subcommand"] = spec::subcommand_of(c);
        row["groupTitle"] = groups.value(c.value("group", ""), "");
        out.push_back(std::move(row));
    }
    return Json{{"editor", editor}, {"commands", out}};
}

// The quick menu: the commands marked for it, each with whether it can run now and why not.
std::expected<Json, rpc::Error> Session::quick_menu(const Json& p) {
    auto table = commands(p);
    if (!table) return table;
    const auto root = root_of(p);
    bool ready, trusted;
    {
        std::lock_guard lock(mutex_);
        ready = client_.ready();
        trusted = trusted_;
    }
    const bool running = [&] {
        for (const auto& j : jobs_->list()) if (j.value("root", "") == root.lexically_normal().generic_string()) return true;
        return false;
    }();
    Json items = Json::array();
    for (const auto& c : (*table)["commands"]) {
        if (!c.value("quickMenu", false)) continue;
        const auto when = c.value("when", "");
        std::string disabled;
        if (when.find("mcpp.inProject") != std::string::npos && root.empty()) disabled = t("No mcpp project in this workspace");
        else if (!ready && c["action"].value("kind", "") != "panel" && c["action"].value("kind", "") != "selfCheck" && c["action"].value("kind", "") != "report")
            disabled = t("mcpp is not available");
        Json item{{"command", c["id"]}, {"title", c["title"]}, {"group", c["groupTitle"]}, {"enabled", disabled.empty()}};
        if (!disabled.empty()) item["reason"] = disabled;
        if (c["action"].value("kind", "") == "job" && running) item["detail"] = t("runs after the job in progress");
        if (!trusted && c["action"].value("kind", "") == "job") item["detail"] = t("the workspace is not trusted: mcpp runs with effects denied");
        items.push_back(std::move(item));
    }
    return Json{{"items", items}};
}

// The tests of a project, without building them: `mcpp test --list --message-format json` (one
// record per test, with the file that holds its `main`), else the test targets of the build database.
std::expected<Json, rpc::Error> Session::test_list(const Json& p) {
    const auto root = root_of(p);
    if (root.empty()) return std::unexpected(invalid("test/list names its project's root"));
    mcpp::Client client;
    {
        std::lock_guard lock(mutex_);
        client = client_;
    }
    Json tests = Json::array();
    if (client.ready()) {
        mcpp::RunOptions o;
        o.cwd = root;
        o.args = {"test", "--list", "--message-format", "json"};
        for (const auto& a : target_args(p)) o.args.push_back(a);
        o.deny = deny();
        o.client = client_label();
        if (auto r = client.records(o); r && r->exitCode == 0)
            for (const auto& rec : r->records)
                if (rec.is_object() && rec.contains("test") && rec["test"].is_string())
                    tests.push_back(Json{{"test", rec["test"]}, {"member", rec.value("member", "")}, {"main", rec.value("main", "")}});
    }
    if (tests.empty()) {
        auto db = build_database(root);
        if (db) for (const auto& row : views::project_summary(*db, Json(nullptr))["tests"])
            tests.push_back(Json{{"test", row.value("name", "")}, {"member", ""}, {"main", ""}});
    }
    return Json{{"root", root.generic_string()}, {"tests", tests}};
}

Session::PackOptions Session::pack_options(const std::filesystem::path& cwd) {
    mcpp::Client client;
    {
        std::lock_guard lock(mutex_);
        client = client_;
        if (packOptions_ && packOptions_->program == client.program()) return *packOptions_;
    }
    PackOptions o;
    o.program = client.program();
    if (auto r = query(cwd, {"pack", "--help"})) {
        const auto help = r->output + r->narration;
        if (help.find("--pkg-format") != std::string::npos) o.pkgFormatFlag = "--pkg-format";
        o.linkage = help.find("--linkage") != std::string::npos;
    }
    std::lock_guard lock(mutex_);
    packOptions_ = o;
    return o;
}

void Session::install() {
    auto& s = rpc_;
    s.on("initialize", [this](const Json& p, const rpc::Context&) { return initialize(p); });
    s.on("settings/update", [this](const Json& p, const rpc::Context&) { return update_settings(p); });
    s.on("settings/registry", [this](const Json&, const rpc::Context&) -> Result {
        std::lock_guard lock(localeMutex_);
        return settings::registry(locale_);
    });
    s.on("settings/preset", [](const Json& p, const rpc::Context&) -> Result {
        auto preset = settings::preset(p.value("id", ""));
        if (!preset) return std::unexpected(invalid("no such preset"));
        return *preset;
    });
    s.on("i18n/bundle", [this](const Json& p, const rpc::Context&) -> Result {
        std::string locale;
        {
            std::lock_guard lock(localeMutex_);
            locale = locale_;
        }
        return i18n::bundle(p.value("locale", locale));
    });
    s.on("selfCheck", [this](const Json&, const rpc::Context&) -> Result { locate_mcpp(); return self_check(); });
    s.on("commands/list", [this](const Json& p, const rpc::Context&) { return commands(p); });
    s.on("menu/quick", [this](const Json& p, const rpc::Context&) { return quick_menu(p); });
    s.on("languageService/status", [this](const Json& p, const rpc::Context&) { return language_service(p); });
    s.on("projects/list", [this](const Json& p, const rpc::Context&) -> Result {
        Json out = Json::array();
        std::vector<std::filesystem::path> folders;
        {
            std::lock_guard lock(mutex_);
            folders = folders_;
        }
        for (const auto& f : strings_of(p.value("folders", Json::array()))) folders.emplace_back(f);
        for (const auto& folder : folders)
            for (const auto& proj : projects::discover(folder, p.value("depth", 4))) {
                watcher_->watch(proj.root);
                out.push_back(project_json(proj));
            }
        return out;
    });
    s.on("projects/forFile", [this](const Json& p, const rpc::Context&) -> Result {
        auto proj = projects::project_for(p.value("path", ""), p.value("boundary", ""));
        if (!proj) return Json(nullptr);
        watcher_->watch(proj->root);
        return project_json(*proj);
    });
    s.on("project/summary", [this](const Json& p, const rpc::Context&) { return project_summary(p); });
    s.on("mcpp/exec", [this](const Json& p, const rpc::Context&) { return mcpp_exec(p); });
    s.on("languageService/exec", [this](const Json& p, const rpc::Context&) { return language_service_exec(p); });
    s.on("project/files", [](const Json& p, const rpc::Context&) -> Result { return projects::files(root_of(p)); });
    s.on("project/new", [this](const Json& p, const rpc::Context&) { return project_new(p); });
    s.on("result/last", [](const Json& p, const rpc::Context&) -> Result { return projects::last_result(root_of(p)); });
    s.on("job/start", [this](const Json& p, const rpc::Context&) { return job_start(p); });
    s.on("job/cancel", [this](const Json& p, const rpc::Context&) -> Result {
        return Json{{"cancelled", jobs_->cancel(p.value("jobId", ""))}};
    });
    s.on("job/list", [this](const Json&, const rpc::Context&) -> Result { return jobs_->list(); });
    s.on("run/launch", [this](const Json& p, const rpc::Context& c) { return run_launch(p, c); });
    s.on("debug/configuration", [this](const Json& p, const rpc::Context& c) { return debug_configuration(p, c); });
    s.on("test/list", [this](const Json& p, const rpc::Context&) { return test_list(p); });
    s.on("project/mcpp", [this](const Json& p, const rpc::Context&) { return project_mcpp(p); });
    s.on("project/refresh", [this](const Json& p, const rpc::Context&) { return project_refresh(p); });
    s.on("capabilities/explain", [this](const Json& p, const rpc::Context&) { return capabilities_explain(p); });
    s.on("report/collect", [this](const Json& p, const rpc::Context&) { return report_collect(p); });
    s.on("library/search", [this](const Json& p, const rpc::Context&) { return library_search(p); });
    s.on("library/indexes", [this](const Json& p, const rpc::Context&) -> Result {
        auto r = query(any_root(p), {"index", "list", "--format", "json"});
        if (!r) return std::unexpected(r.error());
        return r->envelope.is_object() ? r->envelope["data"] : Json(nullptr);
    });
    s.on("library/add", [this](const Json& p, const rpc::Context&) -> Result {
        auto dep = p.value("dependency", "");
        if (p.contains("version") && p["version"].is_string() && !p["version"].get<std::string>().empty())
            dep += "@" + p["version"].get<std::string>();
        return job_start(Json{{"kind", "add"}, {"root", p.value("root", "")}, {"dependency", dep}});
    });
    s.on("library/updateIndex", [this](const Json& p, const rpc::Context&) -> Result {
        {
            std::lock_guard lock(mutex_);
            searchCache_ = nullptr;
        }
        return job_start(Json{{"kind", "command"}, {"root", any_root(p).generic_string()}, {"args", Json::array({"index", "update"})}});
    });
    s.on("toolchains/list", [this](const Json& p, const rpc::Context&) -> Result {
        auto r = query(any_root(p), {"toolchain", "list", "--format", "json"});
        if (!r) return std::unexpected(r.error());
        return views::toolchains(r->envelope);
    });
    s.on("toolchains/install", [this](const Json& p, const rpc::Context&) -> Result {
        Json job{{"kind", "toolchain"}, {"root", any_root(p).generic_string()}, {"spec", p.value("spec", "")}};
        if (p.contains("target")) job["target"] = p["target"];
        return job_start(job);
    });
    s.on("toolchains/setDefault", [this](const Json& p, const rpc::Context&) -> Result {
        auto spec = p.value("spec", "");
        if (spec.empty()) return std::unexpected(invalid("toolchains/setDefault names a spec"));
        auto r = query(any_root(p), {"toolchain", "default", spec});
        if (!r) return std::unexpected(r.error());
        if (r->exitCode != 0) return std::unexpected(rpc::Error{rpc::REQUEST_FAILED, r->narration});
        return Json{{"spec", spec}};
    });
    s.on("targets/list", [this](const Json& p, const rpc::Context&) { return targets(p); });
    s.on("cache/inventory", [this](const Json& p, const rpc::Context&) -> Result {
        auto r = query(any_root(p), {"cache", "list", "--format", "json"});
        if (!r) return std::unexpected(r.error());
        views::CacheOptions o;
        const auto topN = setting("mcpp.views.cache.topN");
        o.topN = p.value("topN", topN.is_number() ? topN.get<int>() : 5);
        return views::cache_inventory(r->envelope, o);
    });
    s.on("cache/plan", [this](const Json& p, const rpc::Context&) { return cache_plan(p); });
    s.on("cache/apply", [this](const Json& p, const rpc::Context&) { return cache_apply(p); });
    s.on("graph/packages", [this](const Json& p, const rpc::Context&) { return graph(p, false); });
    s.on("graph/modules", [this](const Json& p, const rpc::Context&) { return graph(p, true); });
    s.on("pack/formats", [this](const Json& p, const rpc::Context&) -> Result {
        auto r = query(any_root(p), {"pack", "--list-formats"});
        if (!r) return std::unexpected(r.error());
        Json out = r->envelope.is_object() && r->envelope.contains("data") ? r->envelope["data"] : Json::object();
        const auto options = pack_options(any_root(p));
        // The closures every mcpp since 2026.10.26.1 packs (SPEC-014), and the linkages where it
        // takes them apart from the closure.
        out["modes"] = Json::array({"vendored", "system", "self-contained", "static"});
        out["linkages"] = options.linkage ? Json::array({"static", "shared"}) : Json::array();
        return out;
    });
    s.on("pack/start", [this](const Json& p, const rpc::Context&) -> Result {
        Json job = p;
        job["kind"] = "pack";
        return job_start(job);
    });
    s.on("manifest/check", [this](const Json& p, const rpc::Context&) { return manifest_check(p); });
    s.on("toml/complete", [](const Json& p, const rpc::Context&) -> Result { return manifest::complete(p); });
    s.on("toml/hover", [](const Json& p, const rpc::Context&) -> Result { return manifest::hover(p); });
    s.on("toml/symbols", [](const Json& p, const rpc::Context&) -> Result { return manifest::symbols(p); });
    s.on("buildscript/complete", [](const Json& p, const rpc::Context&) -> Result { return manifest::buildscript_complete(p); });
}

}  // namespace mcppide::server
