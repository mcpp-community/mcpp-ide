// The session: one editor window's backend. It holds the mcpp it found, the
// projects of the window's folders, the jobs, and the watchers, and answers
// the methods of protocol/protocol.schema.json.
//
// THE ADAPTER DECIDES NOTHING. Which argv a build runs, how much confirmation a
// clean needs, what a cache panel shows, what a setting may hold: answered
// here, so VS Code and CLion behave alike and an agent reading the backend's
// log sees the same reasons a person sees in the editor.
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
import mcpplibs.os;

export namespace mcppide::server {

using Json = nlohmann::json;

inline constexpr std::string_view kVersion = "0.1.0";
inline constexpr int kProtocol = 1;

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
    Result manifest_check(const Json& p);

    std::expected<mcpp::Client::EnvelopeResult, rpc::Error> query(const std::filesystem::path& cwd,
                                                                    std::vector<std::string> args) const;
    std::expected<Json, rpc::Error> build_database(const std::filesystem::path& root);
    std::vector<std::string> deny() const;
    std::string client_label() const;
    std::string t(std::string_view key, const std::vector<std::string>& args = {}) const;
    void locate_mcpp();
    void on_changed(const std::filesystem::path& root, std::string_view what, const Json& detail);

    rpc::Server& rpc_;
    mutable std::mutex mutex_;
    mcpp::Client client_;
    std::optional<mcpp::Located> located_;
    std::string mcppError_;
    Json settings_ = Json::object();
    bool trusted_ = true;
    std::string locale_ = "en";
    std::string clientName_ = "mcpp-ide";
    std::vector<std::filesystem::path> folders_;
    std::unique_ptr<jobs::Jobs> jobs_;
    std::unique_ptr<projects::Watcher> watcher_;
    // `mcpp search` takes a few hundred milliseconds and answers the same
    // catalogue for minutes: kept until an index changes or the user asks.
    Json searchCache_;
    std::chrono::steady_clock::time_point searchAt_{};
    std::map<std::filesystem::path, Json> buildDatabases_;
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
        .record = [this](std::string_view id, const Json& record) {
            rpc_.notify("job/record", Json{{"jobId", id}, {"record", record}});
        },
        .finished = [this](std::string_view id, const Json& summary) {
            Json s = summary;
            s["jobId"] = id;
            rpc_.notify("job/finished", s);
        },
        .started = [this](std::string_view id, const Json& started) {
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

std::string Session::t(std::string_view key, const std::vector<std::string>& args) const {
    return i18n::translate(key, locale_, args);
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
    jobs_->set_client(client_);
}

void Session::on_changed(const std::filesystem::path& root, std::string_view what, const Json& detail) {
    {
        std::lock_guard lock(mutex_);
        buildDatabases_.erase(root);
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
    if (auto xlings = mcpplibs::os::env::find_executable("xlings")) add("xlings", "ok", t("xlings at {0}", {xlings->generic_string()}));
    else add("xlings", "info", t("xlings is not on PATH; mcpp uses the one it bundles"));
    {
        std::lock_guard lock(mutex_);
        add("trust", trusted_ ? "ok" : "info",
            trusted_ ? t("the workspace is trusted") : t("the workspace is not trusted: mcpp runs with effects denied"));
    }
    return Json{{"backend", Json{{"version", kVersion}, {"protocol", kProtocol}}}, {"mcpp", mcppPart}, {"checks", checks}};
}

std::expected<Json, rpc::Error> Session::initialize(const Json& p) {
    {
        std::lock_guard lock(mutex_);
        if (p.contains("client") && p["client"].is_object()) {
            const auto name = p["client"].value("name", "editor");
            const auto version = p["client"].value("version", "");
            clientName_ = std::format("mcpp-ide/{} ({}{}{})", kVersion, name, version.empty() ? "" : " ", version);
        }
        locale_ = i18n::normalize_locale(p.value("locale", "en"));
        trusted_ = p.value("trusted", true);
        settings_ = settings::resolve(p.contains("settings") ? p["settings"] : Json::object())["values"];
        folders_.clear();
        for (const auto& f : strings_of(p.value("workspaceFolders", Json::array()))) folders_.emplace_back(f);
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
                {"checks", check["checks"]},
                {"projects", found},
                {"capabilities", Json::array({"jobs", "results", "launch", "debug", "library", "toolchains", "cache",
                                              "graphs", "pack", "settings", "i18n", "manifest", "toml", "buildscript"})}};
}

std::expected<Json, rpc::Error> Session::update_settings(const Json& p) {
    Json resolved = settings::resolve(p.contains("settings") ? p["settings"] : Json::object());
    std::string before;
    {
        std::lock_guard lock(mutex_);
        before = settings_.value("mcpp.path", "");
        settings_ = resolved["values"];
        if (p.contains("trusted")) trusted_ = p["trusted"].get<bool>();
        if (p.contains("locale")) locale_ = i18n::normalize_locale(p["locale"].get<std::string>());
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
    const auto target = p.value("target", "");
    const bool release = p.value("profile", settings_.value("mcpp.build.profile", "dev")) == "release";
    switch (*kind) {
        case jobs::Kind::build:
            r.args = {"build", "--message-format", "json"};
            if (release) r.args.push_back("--release");
            if (!target.empty()) r.args.push_back(target);
            for (const auto& a : strings_of(settings_.value("mcpp.task.buildArgs", Json::array()))) r.args.push_back(a);
            r.mode = jobs::Mode::ndjson;
            break;
        case jobs::Kind::test:
            r.args = {"test", "--message-format", "json"};
            if (release) r.args.push_back("--release");
            if (p.contains("filter") && p["filter"].is_string() && !p["filter"].get<std::string>().empty())
                r.args.push_back(p["filter"].get<std::string>());
            for (const auto& a : strings_of(settings_.value("mcpp.task.testArgs", Json::array()))) r.args.push_back(a);
            r.mode = jobs::Mode::ndjson;
            break;
        case jobs::Kind::pack: {
            r.args = {"pack"};
            if (p.contains("format") && p["format"].is_string()) { r.args.push_back("--format"); r.args.push_back(p["format"]); }
            if (release) r.args.push_back("--release");
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
    if (p.value("profile", settings_.value("mcpp.build.profile", "dev")) == "release") args.push_back("--release");
    if (auto target = p.value("target", ""); !target.empty()) args.push_back(target);
    auto r = query(root, std::move(args));
    if (!r) return std::unexpected(r.error());
    if (ctx.cancelled()) return std::unexpected(rpc::Error{rpc::REQUEST_CANCELLED, "cancelled"});
    if (r->exitCode != 0 || !r->envelope.is_object() || r->envelope.value("kind", "") != "mcpp.launch")
        return std::unexpected(rpc::Error{rpc::REQUEST_FAILED, t("the program could not be built"),
                                          Json{{"exitCode", r->exitCode}, {"narration", r->narration},
                                               {"diagnostics", mcpp::diagnostics_of(r->envelope)}}});
    return r->envelope["data"];
}

std::expected<Json, rpc::Error> Session::debug_configuration(const Json& p, const rpc::Context& ctx) {
    auto launch = run_launch(p, ctx);
    if (!launch) return launch;
    const auto& l = *launch;
    std::string engine = p.value("engine", settings_.value("mcpp.debug.engine", "auto"));
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
    Json cfg{{"name", std::format("mcpp: {}", std::filesystem::path(l.value("program", program)).filename().string())},
             {"request", "launch"}, {"program", program}, {"args", args}, {"cwd", l.value("cwd", root_of(p).generic_string())}};
    if (engine == "cppdbg") {
        cfg["type"] = "cppdbg";
        Json envList = Json::array();
        for (auto it = env.begin(); it != env.end(); ++it) envList.push_back(Json{{"name", it.key()}, {"value", it.value()}});
        cfg["environment"] = envList;
        cfg["MIMode"] = p.value("platform", "linux") == "macos" ? "lldb" : "gdb";
        cfg["externalConsole"] = false;
    } else if (engine == "lldb") {
        cfg["type"] = "lldb";
        cfg["env"] = env;
    } else if (engine == "lldb-dap") {
        cfg["type"] = "lldb-dap";
        Json envList = Json::array();
        for (auto it = env.begin(); it != env.end(); ++it) envList.push_back(std::format("{}={}", it.key(), it.value().get<std::string>()));
        cfg["env"] = envList;
    } else if (engine == "gdb") {
        cfg["type"] = "gdb";
        cfg["env"] = env;
    } else {
        return std::unexpected(invalid(std::format("unknown debug engine {}", engine)));
    }
    if (!l.value("via", Json(nullptr)).is_null())
        cfg["mcppNote"] = t("this program runs through {0}; the debugger starts it directly",
                            {l["via"].get<std::string>()});
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
        auto cwd = folders_.empty() ? std::filesystem::current_path() : folders_.front();
        auto r = query(cwd, {"search", "--format", "json"});
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

std::expected<Json, rpc::Error> Session::cache_plan(const Json& p) {
    policy::CleanOptions o;
    o.staleDays = p.value("staleDays", settings_.value("mcpp.cache.staleDays", 3));
    o.budgetGiB = p.value("budgetGiB", settings_.value("mcpp.cache.gc.defaultBudgetGiB", 0.0));
    o.pruneAgeDays = p.value("pruneAgeDays", settings_.value("mcpp.cache.pruneAgeDays", 30));
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
        const auto root = root_of(p);
        if (plan->previewSource == "mcpp") {
            auto r = query(root.empty() ? std::filesystem::current_path() : root, plan->previewArgv);
            if (r) out["preview"] = Json{{"source", "mcpp"}, {"text", r->narration}};
        } else {
            auto r = query(root.empty() ? std::filesystem::current_path() : root, {"cache", "list", "--format", "json"});
            if (r) out["preview"] = views::cache_preview(r->envelope, *plan, o);
        }
    }
    return out;
}

std::expected<Json, rpc::Error> Session::cache_apply(const Json& p) {
    policy::CleanOptions o;
    o.staleDays = p.value("staleDays", settings_.value("mcpp.cache.staleDays", 3));
    o.budgetGiB = p.value("budgetGiB", settings_.value("mcpp.cache.gc.defaultBudgetGiB", 0.0));
    o.pruneAgeDays = p.value("pruneAgeDays", settings_.value("mcpp.cache.pruneAgeDays", 30));
    auto plan = policy::plan(p.value("action", ""), o);
    if (!plan) return std::unexpected(invalid(std::format("unknown cache action {}", p.value("action", ""))));
    {
        std::lock_guard lock(mutex_);
        if (plan->requiresTrust && !trusted_)
            return std::unexpected(rpc::Error{rpc::REQUEST_FAILED, t("this action needs a trusted workspace")});
    }
    if (!policy::confirmed(*plan, p.value("confirmation", Json::object())))
        return std::unexpected(rpc::Error{rpc::REQUEST_FAILED, t("this action was not confirmed"), policy::to_json(*plan)});
    auto root = root_of(p);
    if (root.empty()) root = folders_.empty() ? std::filesystem::current_path() : folders_.front();
    jobs::Request r;
    r.kind = plan->group == "clean" ? jobs::Kind::clean : jobs::Kind::command;
    r.root = root;
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
    return summary;
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

void Session::install() {
    auto& s = rpc_;
    s.on("initialize", [this](const Json& p, const rpc::Context&) { return initialize(p); });
    s.on("settings/update", [this](const Json& p, const rpc::Context&) { return update_settings(p); });
    s.on("settings/registry", [](const Json&, const rpc::Context&) -> Result { return settings::registry(); });
    s.on("settings/preset", [](const Json& p, const rpc::Context&) -> Result {
        auto preset = settings::preset(p.value("id", ""));
        if (!preset) return std::unexpected(invalid("no such preset"));
        return *preset;
    });
    s.on("i18n/bundle", [this](const Json& p, const rpc::Context&) -> Result {
        return i18n::bundle(p.value("locale", locale_));
    });
    s.on("selfCheck", [this](const Json&, const rpc::Context&) -> Result { locate_mcpp(); return self_check(); });
    s.on("projects/list", [this](const Json& p, const rpc::Context&) -> Result {
        Json out = Json::array();
        std::vector<std::filesystem::path> folders = folders_;
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
    s.on("result/last", [](const Json& p, const rpc::Context&) -> Result { return projects::last_result(root_of(p)); });
    s.on("job/start", [this](const Json& p, const rpc::Context&) { return job_start(p); });
    s.on("job/cancel", [this](const Json& p, const rpc::Context&) -> Result {
        return Json{{"cancelled", jobs_->cancel(p.value("jobId", ""))}};
    });
    s.on("job/list", [this](const Json&, const rpc::Context&) -> Result { return jobs_->list(); });
    s.on("run/launch", [this](const Json& p, const rpc::Context& c) { return run_launch(p, c); });
    s.on("debug/configuration", [this](const Json& p, const rpc::Context& c) { return debug_configuration(p, c); });
    s.on("library/search", [this](const Json& p, const rpc::Context&) { return library_search(p); });
    s.on("library/indexes", [this](const Json& p, const rpc::Context&) -> Result {
        auto r = query(root_of(p).empty() ? std::filesystem::current_path() : root_of(p), {"index", "list", "--format", "json"});
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
        auto root = root_of(p).empty() ? (folders_.empty() ? std::filesystem::current_path() : folders_.front()) : root_of(p);
        return job_start(Json{{"kind", "command"}, {"root", root.generic_string()}, {"args", Json::array({"index", "update"})}});
    });
    s.on("toolchains/list", [this](const Json& p, const rpc::Context&) -> Result {
        auto r = query(root_of(p).empty() ? std::filesystem::current_path() : root_of(p), {"toolchain", "list", "--format", "json"});
        if (!r) return std::unexpected(r.error());
        return views::toolchains(r->envelope);
    });
    s.on("toolchains/install", [this](const Json& p, const rpc::Context&) -> Result {
        auto root = root_of(p).empty() ? (folders_.empty() ? std::filesystem::current_path() : folders_.front()) : root_of(p);
        return job_start(Json{{"kind", "toolchain"}, {"root", root.generic_string()}, {"spec", p.value("spec", "")}});
    });
    s.on("toolchains/setDefault", [this](const Json& p, const rpc::Context&) -> Result {
        auto spec = p.value("spec", "");
        if (spec.empty()) return std::unexpected(invalid("toolchains/setDefault names a spec"));
        auto r = query(root_of(p).empty() ? std::filesystem::current_path() : root_of(p), {"toolchain", "default", spec});
        if (!r) return std::unexpected(r.error());
        if (r->exitCode != 0) return std::unexpected(rpc::Error{rpc::REQUEST_FAILED, r->narration});
        return Json{{"spec", spec}};
    });
    s.on("cache/inventory", [this](const Json& p, const rpc::Context&) -> Result {
        auto r = query(std::filesystem::current_path(), {"cache", "list", "--format", "json"});
        if (!r) return std::unexpected(r.error());
        views::CacheOptions o;
        o.topN = p.value("topN", settings_.value("mcpp.views.cache.topN", 5));
        return views::cache_inventory(r->envelope, o);
    });
    s.on("cache/plan", [this](const Json& p, const rpc::Context&) { return cache_plan(p); });
    s.on("cache/apply", [this](const Json& p, const rpc::Context&) { return cache_apply(p); });
    s.on("graph/packages", [this](const Json& p, const rpc::Context&) { return graph(p, false); });
    s.on("graph/modules", [this](const Json& p, const rpc::Context&) { return graph(p, true); });
    s.on("pack/formats", [this](const Json& p, const rpc::Context&) -> Result {
        auto r = query(root_of(p), {"pack", "--list-formats"});
        if (!r) return std::unexpected(r.error());
        return r->envelope.is_object() ? r->envelope["data"] : Json(nullptr);
    });
    s.on("manifest/check", [this](const Json& p, const rpc::Context&) { return manifest_check(p); });
    s.on("toml/complete", [](const Json& p, const rpc::Context&) -> Result { return manifest::complete(p); });
    s.on("toml/hover", [](const Json& p, const rpc::Context&) -> Result { return manifest::hover(p); });
    s.on("toml/symbols", [](const Json& p, const rpc::Context&) -> Result { return manifest::symbols(p); });
    s.on("buildscript/complete", [](const Json& p, const rpc::Context&) -> Result { return manifest::buildscript_complete(p); });
}

}  // namespace mcppide::server
