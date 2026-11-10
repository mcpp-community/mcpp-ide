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
import mcppide.documents;
import mcppide.spec;
import mcppide.languageservice;
import mcppide.scan;
import mcppide.testing;
import mcppide.teststore;
import mcppide.testsession;
import mcpplibs.os;

export namespace mcppide::server {

using Json = nlohmann::json;

inline constexpr std::string_view kVersion = "1.4.0";
// THE SERIES (mcpp design 2026-11-07, D15): `X.Y` of `X.Y.Z`, shared by the backend and every editor
// plugin; components of one series work together. A plugin refuses a backend of another series,
// naming both versions. The backend is always `X.Y.0`: the VS Code and CLion packages carry it, so a
// change of the backend is a change of every package, and it begins a series (1.3.0). `Z` is an
// editor plugin's own patch.
inline constexpr std::string_view kSeries = "1.4";
static_assert(kVersion.starts_with(kSeries) && kVersion.size() == kSeries.size() + 2 && kVersion.ends_with(".0"));
inline constexpr int kProtocol = 2;

// The methods `install` registers, for the protocol schema's test: a method
// here and not in protocol/protocol.schema.json (or the other way) fails it. The session is an LSP
// server (protocol 2, design 2026-10-30 part 2 §8): LSP's own methods, and mcpp-ide's under
// `mcpp-ide/`.
inline constexpr std::array<std::string_view, 55> kMethods = {
    "initialize", "shutdown", "exit", "textDocument/completion", "textDocument/hover", "textDocument/definition",
    "textDocument/documentSymbol", "mcpp-ide/settings/update", "mcpp-ide/settings/registry",
    "mcpp-ide/settings/preset", "mcpp-ide/i18n/bundle", "mcpp-ide/selfCheck", "mcpp-ide/commands/list",
    "mcpp-ide/menu/quick", "mcpp-ide/languageService/status", "mcpp-ide/projects/list", "mcpp-ide/projects/forFile",
    "mcpp-ide/project/summary", "mcpp-ide/project/new", "mcpp-ide/result/last", "mcpp-ide/job/start",
    "mcpp-ide/job/cancel", "mcpp-ide/job/list", "mcpp-ide/run/launch", "mcpp-ide/debug/configuration",
    "mcpp-ide/test/list", "mcpp-ide/library/search", "mcpp-ide/library/indexes", "mcpp-ide/library/add",
    "mcpp-ide/library/updateIndex", "mcpp-ide/toolchains/list", "mcpp-ide/toolchains/install",
    "mcpp-ide/toolchains/setDefault", "mcpp-ide/targets/list", "mcpp-ide/cache/inventory", "mcpp-ide/cache/plan",
    "mcpp-ide/cache/apply", "mcpp-ide/graph/packages", "mcpp-ide/graph/modules", "mcpp-ide/pack/formats",
    "mcpp-ide/pack/start", "mcpp-ide/manifest/check", "mcpp-ide/mcpp/exec", "mcpp-ide/mcpp/availability", "mcpp-ide/project/files",
    "mcpp-ide/languageService/exec", "mcpp-ide/project/mcpp", "mcpp-ide/project/refresh",
    "mcpp-ide/capabilities/explain", "mcpp-ide/report/collect", "mcpp-ide/source/entries", "mcpp-ide/test/cases",
    "mcpp-ide/test/runCases", "mcpp-ide/test/history", "mcpp-ide/test/insights"};

// The notifications the backend reads.
inline constexpr std::array<std::string_view, 5> kReceived = {
    "initialized", "textDocument/didOpen", "textDocument/didChange", "textDocument/didClose", "$/cancelRequest"};

// The notifications the backend sends.
inline constexpr std::array<std::string_view, 19> kNotifications = {
    "textDocument/publishDiagnostics", "window/showMessage", "mcpp-ide/job/started", "mcpp-ide/job/output",
    "mcpp-ide/job/plan", "mcpp-ide/job/progress", "mcpp-ide/job/diagnostic", "mcpp-ide/job/artifact",
    "mcpp-ide/job/record", "mcpp-ide/job/finished", "mcpp-ide/test/result", "mcpp-ide/test/summary",
    "mcpp-ide/result/changed", "mcpp-ide/manifest/changed", "mcpp-ide/manifest/diagnostics",
    "mcpp-ide/project/mcppChanged", "mcpp-ide/languageService/changed", "mcpp-ide/test/caseResult",
    "mcpp-ide/test/caseEvent"};

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
    // `after` runs when the job has ended, before the editors are told (jobs::Request::after).
    Result job_start(const Json& p, std::function<void(std::string_view, const Json&)> after = {});
    Result run_launch(const Json& p, const rpc::Context& ctx);
    Result debug_configuration(const Json& p, const rpc::Context& ctx);
    Result library_search(const Json& p);
    Result cache_plan(const Json& p);
    Result cache_apply(const Json& p);
    Result graph(const Json& p, bool modules);
    Result project_summary(const Json& p);
    Result mcpp_exec(const Json& p);
    Result mcpp_availability();
    Result language_service_exec(const Json& p);
    Result project_new(const Json& p);
    Result manifest_check(const Json& p);
    Result language_service(const Json& p);
    Result targets(const Json& p);
    Result commands(const Json& p);
    Json server_actions();
    Result quick_menu(const Json& p);
    Result test_list(const Json& p);
    Result source_entries(const Json& p);
    Result test_cases(const Json& p);
    Result test_run_cases(const Json& p);
    Result test_history(const Json& p);
    Result test_insights(const Json& p);
    // The programs of `mcpp test --list` (kept per project), and lookups among them.
    std::vector<testing::Program> list_programs(const std::filesystem::path& root, const Json& p);
    std::vector<testing::Program> programs_of(const std::filesystem::path& root);
    std::optional<testing::Program> program_named(const std::filesystem::path& root, std::string_view name);
    std::optional<testing::Program> program_of_file(const std::filesystem::path& root, const std::filesystem::path& file);
    // Some cases run through the project's test session (O9), when its framework states one and
    // mcpp gives the programs' launches (B5); nothing when either is not so.
    std::optional<Json> run_through_session(const std::filesystem::path& root, const std::string& test,
                                            const std::vector<std::string>& cases, const Json& p);
    void on_session(const std::filesystem::path& root, std::string_view method, const Json& params);
    // A program's cases as it lists them (mcpp test spec B3), from one listing of the project.
    std::optional<std::vector<testing::Case>> listed_cases(const std::filesystem::path& root, const std::string& test,
                                                           const Json& p, bool ask);
    // The records of `mcpp test --cases` for the whole project. The last listing is kept: `ask` lists
    // again when the project has changed since, and without it the last listing answers as it is, so
    // that the cases shown stay until a listing replaces them.
    std::optional<std::vector<Json>> project_listing(const std::filesystem::path& root, const Json& p, bool ask);
    // What the project's test frameworks state of themselves (mcpp test spec B6, `mcpp test --guide`):
    // the name the editors show, and the command of a session one of them offers (O9), else empty.
    struct Frameworks {
        std::string name;
        std::vector<std::string> session;
    };
    Frameworks frameworks_of(const std::filesystem::path& root, const Json& p);
    std::string framework_name(const std::filesystem::path& root, const Json& p) { return frameworks_of(root, p).name; }
    // A record of a test job, with the program and part it belongs to, and its cases as the editors
    // receive them (design 2026-11-10).
    Json test_result_of(const std::filesystem::path& root, const Json& record, Json n);
    void protocol_case_results(std::string_view jobId, const std::filesystem::path& root, const std::string& test, const Json& record);
    // The store's last runs (kept per project; a run is read once).
    std::vector<teststore::Run> store_runs(const std::filesystem::path& root, std::size_t count);
    // The packages of a project's plan, from its build database when one was read.
    std::vector<std::string> plan_packages(const std::filesystem::path& root, bool read);
    Result project_mcpp(const Json& p);
    Result project_refresh(const Json& p);
    Result capabilities_explain(const Json& p);
    Result report_collect(const Json& p);
    // The rules layer's switches for the documents, from the settings.
    documents::Options document_options() const;
    // A document's diagnostics, to the editor.
    void publish(const documents::Document& d);
    // mcpp's catalogue for version completion, when it was read; asked in the background otherwise.
    std::optional<Json> catalogue(const std::filesystem::path& cwd);
    // LSP's document methods and notifications.
    void install_documents();

    // THE PROJECT'S mcpp (design 2026-10-30 part 2 §4.2): asked in the project, once, and asked again
    // when its manifest changes, when the editor regains focus, or when a job's envelope names
    // another version. Never more than two asked at once.
    mcpp::ProjectMcpp probe_of(const std::filesystem::path& root);
    Json probe_json(const std::filesystem::path& root, const mcpp::ProjectMcpp& probe) const;
    bool has_feature(const std::filesystem::path& root, std::string_view feature);
    std::vector<std::string> launch_args(const Json& p, bool stream, bool programArgs);
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
    // `mcpp test --message-format json` with the profile, the target and `mcpp.task.testArgs`: one
    // set of arguments for every test command, so that a listing and a run plan one configuration
    // (design 2026-11-10 supplement, T14).
    std::vector<std::string> test_args(const Json& p) const;
    std::filesystem::path any_root(const Json& p) const;
    void locate_mcpp();
    void on_changed(const std::filesystem::path& root, std::string_view what, const Json& detail);
    void on_record(std::string_view jobId, const Json& record);

    rpc::Server& rpc_;
    mutable std::mutex mutex_;
    mcpp::Client client_;
    std::optional<mcpp::Located> located_;
    std::string mcppError_;
    bool mcppMissing_ = false;
    Json settings_ = Json::object();
    bool trusted_ = true;
    mutable std::mutex localeMutex_;
    std::string locale_ = "en";
    std::string clientName_ = "mcpp-ide";
    std::string editor_ = "vscode";
    std::vector<std::string> mcpplsCandidates_;
    // Whether C++ Modules serves build.mcpp in the editor (it then answers what it answers better).
    bool mcpplsServes_ = false;
    // The documents the editor keeps open (mcpp.toml, build.mcpp), and the catalogue a dependency
    // version is completed from: asked once in the background, the editor told once that it was.
    documents::Store documents_;
    bool catalogueAsked_ = false;
    bool catalogueNoticed_ = false;
    // mcppls's actions as its last report named them (null until one was asked), and what the editors
    // were last told about it, so that a change is sent once.
    Json mcpplsActions_;
    std::string mcpplsSeen_;
    std::vector<std::filesystem::path> folders_;
    std::unique_ptr<jobs::Jobs> jobs_;
    std::unique_ptr<projects::Watcher> watcher_;
    // `mcpp search` takes a few hundred milliseconds and answers the same
    // catalogue for minutes: kept until an index changes or the user asks.
    Json searchCache_;
    std::chrono::steady_clock::time_point searchAt_{};
    std::map<std::filesystem::path, Json> buildDatabases_;
    struct Listing {
        std::vector<Json> records;
        bool current = true;   // false once the project changed
    };
    std::map<std::filesystem::path, Listing> listings_;   // project_listing
    std::map<std::filesystem::path, Frameworks> frameworks_;        // frameworks_of
    // THE TEST SESSIONS (mcpp test spec O9): one per project whose framework states one, and the
    // runs through them, each answered to the editor as a job of its own.
    struct SessionRun {
        std::string jobId;
        std::filesystem::path root;
        std::map<std::string, Json> cases;   // per case: its failures and its output, as they came
    };
    std::map<std::filesystem::path, std::unique_ptr<testsession::Client>> testSessions_;
    std::map<std::string, SessionRun> sessionRuns_;                                   // by the session's run
    std::map<std::string, std::pair<std::filesystem::path, std::string>> sessionJobs_; // job -> project, run
    std::uint64_t sessionJobNext_ = 1;
    // The project and kind of each job, for the notifications its records become.
    std::map<std::string, std::pair<std::filesystem::path, std::string>, std::less<>> jobOf_;
    // The whole output of the last test record of each running test job, for a case run to divide
    // among its cases when the job ends (test/runCases).
    std::map<std::string, std::string, std::less<>> programOutput_;
    // The test records of each running test job, whole: a program that implements the test protocol
    // reports its cases in them, one record per part of a program of several files (test/runCases).
    std::map<std::string, std::vector<Json>, std::less<>> testRecords_;
    // The programs of each project as `mcpp test --list` last named them.
    std::map<std::filesystem::path, std::vector<testing::Program>> programs_;
    // The runs of each project's store already read, by id.
    std::map<std::filesystem::path, std::map<std::string, teststore::Run>> storeRuns_;
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
// A name an editor or the environment spells in the host's rules (`C:\Users\x` on Windows), in the
// generic spelling every name the backend builds, compares and reports uses: `/` between components
// (mcpplibs.os). The backend runs as an ok image, whose names are `/`-separated on every system; a
// `\` left in a name is read as part of a component there, so `C:\a\b` would be one relative name.
std::filesystem::path host_path(std::string_view text) {
    if (text.empty()) return {};
    namespace fs = mcpplibs::os::fs;
    return fs::from_utf8(fs::generic(fs::from_utf8(text)));
}
std::filesystem::path root_of(const Json& p) {
    return p.contains("root") && p["root"].is_string() ? host_path(p["root"].get<std::string>()) : std::filesystem::path{};
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
            rpc_.notify("mcpp-ide/job/output", Json{{"jobId", id}, {"text", text}});
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
                // A launch job ends with the program it built, described (`mcpp.launch`).
                if (env.is_object() && env.value("kind", "") == "mcpp.launch" && env.contains("data"))
                    s["launch"] = env["data"];
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
                if (auto it = programOutput_.find(id); it != programOutput_.end()) programOutput_.erase(it);
                if (auto it = testRecords_.find(id); it != testRecords_.end()) testRecords_.erase(it);
            }
            rpc_.notify("mcpp-ide/job/finished", s);
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
                if (changed) rpc_.notify("mcpp-ide/project/mcppChanged", Json{{"root", root.generic_string()}, {"version", seen}});
            }
        },
        .started = [this](std::string_view id, const Json& started) {
            {
                std::lock_guard lock(mutex_);
                jobOf_[std::string(id)] = {std::filesystem::path(started.value("root", "")), started.value("kind", "")};
            }
            Json s = started;
            s["jobId"] = id;
            rpc_.notify("mcpp-ide/job/started", s);
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

std::vector<std::string> Session::test_args(const Json& p) const {
    std::vector<std::string> args{"test", "--message-format", "json"};
    const auto profile = p.value("profile", setting("mcpp.build.profile").is_string() ? setting("mcpp.build.profile").get<std::string>() : "dev");
    if (profile == "release") args.push_back("--release");
    else if (!profile.empty() && profile != "dev") {
        args.push_back("--profile");
        args.push_back(profile);
    }
    for (const auto& a : target_args(p)) args.push_back(a);
    for (const auto& a : strings_of(setting("mcpp.task.testArgs"))) args.push_back(a);
    return args;
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
        // An invalid explicit path is a setting error when another local mcpp exists.
        mcppMissing_ = configured.empty() || !mcpp::locate("").has_value();
        located_.reset();
        client_ = mcpp::Client{};
        jobs_->set_client(client_);
        return;
    }
    auto protocol = mcpp::handshake(located->program);
    mcppMissing_ = false;
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
        if (auto it = listings_.find(root); it != listings_.end()) it->second.current = false;
        if (what != "result") frameworks_.erase(root);
        if (what != "result") probes_.erase(root);   // a manifest may state another mcpp floor or pin
        if (what != "result") programs_.erase(root); // and other tests
    }
    if (what == "result") {
        rpc_.notify("mcpp-ide/result/changed", Json{{"root", root.generic_string()}, {"result", detail}});
        return;
    }
    rpc_.notify("mcpp-ide/manifest/changed", Json{{"root", root.generic_string()}});
    // The manifest's problems, from mcpp, located (mcpp docs/50 §2).
    auto checked = manifest_check(Json{{"root", root.generic_string()}});
    if (checked) rpc_.notify("mcpp-ide/manifest/diagnostics", *checked);
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
        rpc_.notify("mcpp-ide/job/plan", n);
    } else if (type == "progress") {
        rpc_.notify("mcpp-ide/job/progress", n);
    } else if (type == "diagnostic") {
        rpc_.notify("mcpp-ide/job/diagnostic", n);
    } else if (type == "artifact") {
        rpc_.notify("mcpp-ide/job/artifact", n);
    } else if (type == "test") {
        {
            std::lock_guard lock(mutex_);
            programOutput_[std::string(jobId)] += record.value("compile_output", "") + record.value("run_output", "");
            testRecords_[std::string(jobId)].push_back(record);
        }
        rpc_.notify("mcpp-ide/test/result", test_result_of(std::filesystem::path(root), record, std::move(n)));
    } else if (type == "case") {
        // A case as it begins or ends while its program runs (mcpp#917, `test-case-stream`).
        const auto& c = n["case"];
        Json event = c.value("event", "") == "end" ? testing::protocol_case(c) : Json{{"case", c.value("id", "")}};
        const auto part = c.value("test", "");
        std::vector<testing::Program> programs;
        {
            std::lock_guard lock(mutex_);
            if (auto it = programs_.find(std::filesystem::path(root)); it != programs_.end()) programs = it->second;
        }
        event["jobId"] = jobId;
        event["root"] = root;
        event["member"] = c.value("member", "");
        event["program"] = testing::owner_of(part, c.value("part_of", ""), programs);
        event["part"] = part;
        event["event"] = c.value("event", "");
        if (c.contains("t_ms") && c["t_ms"].is_number()) event["tMs"] = c["t_ms"];
        rpc_.notify("mcpp-ide/test/caseEvent", event);
    } else if (type == "summary") {
        rpc_.notify("mcpp-ide/test/summary", n);
    } else if (!(record.is_object() && record.contains("schemaVersion") && record.value("kind", "") != "mcpp.event")) {
        // The envelope that closes a stream reaches the adapter in `job/finished`.
        rpc_.notify("mcpp-ide/job/record", n);
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
    o.cwd = p.contains("cwd") && p["cwd"].is_string() ? host_path(p["cwd"].get<std::string>()) : std::filesystem::path{};
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
    if (r->exitCode != 0) {
        const auto diagnostics = mcpp::diagnostics_of(r->envelope);
        auto message = t("mcpp {0} failed with exit code {1}", {"emit build-database", std::to_string(r->exitCode)});
        if (!r->narration.empty()) message += "\n" + r->narration;
        else if (!diagnostics.empty()) message += "\n" + diagnostics.front().value("message", "");
        return std::unexpected(rpc::Error{rpc::REQUEST_FAILED, std::move(message),
                                          Json{{"exitCode", r->exitCode}, {"diagnostics", diagnostics}}});
    }
    if (r->envelope.is_null())
        return std::unexpected(rpc::Error{rpc::REQUEST_FAILED, t("mcpp emit build-database printed no document")});
    std::lock_guard lock(mutex_);
    buildDatabases_[root] = r->envelope;
    return r->envelope;
}

std::expected<Json, rpc::Error> Session::mcpp_availability() {
    bool retry;
    {
        std::lock_guard lock(mutex_);
        retry = !client_.ready();
    }
    if (retry) locate_mcpp();
    std::lock_guard lock(mutex_);
    return Json{{"available", client_.ready()}, {"missing", mcppMissing_}};
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
    // LSP's `initialize`: the client and the workspace in LSP's fields, what mcpp-ide asks in
    // `initializationOptions` (the editor, trust, the settings, mcppls's candidates).
    const Json options = p.contains("initializationOptions") && p["initializationOptions"].is_object() ? p["initializationOptions"]
                                                                                                         : Json::object();
    {
        std::lock_guard lock(mutex_);
        if (p.contains("clientInfo") && p["clientInfo"].is_object()) {
            const auto name = p["clientInfo"].value("name", "editor");
            const auto version = p["clientInfo"].value("version", "");
            clientName_ = std::format("mcpp-ide/{} ({}{}{})", kVersion, name, version.empty() ? "" : " ", version);
        }
        editor_ = options.value("editor", "vscode");
        {
            std::lock_guard localeLock(localeMutex_);
            locale_ = i18n::normalize_locale(options.value("locale", p.value("locale", "en")));
        }
        trusted_ = options.value("trusted", true);
        mcpplsServes_ = options.value("mcpplsServes", false);
        settings_ = settings::resolve(options.contains("settings") ? options["settings"] : Json::object())["values"];
        folders_.clear();
        if (p.contains("workspaceFolders") && p["workspaceFolders"].is_array())
            for (const auto& f : p["workspaceFolders"])
                if (f.is_object() && f.contains("uri") && f["uri"].is_string())
                    if (auto path = documents::path_of(f["uri"].get<std::string>()); !path.empty()) folders_.push_back(path);
        if (folders_.empty() && p.contains("rootUri") && p["rootUri"].is_string())
            if (auto path = documents::path_of(p["rootUri"].get<std::string>()); !path.empty()) folders_.push_back(path);
        if (options.contains("languageService") && options["languageService"].is_object())
            mcpplsCandidates_ = strings_of(options["languageService"].value("candidates", Json::array()));
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
    return Json{{"capabilities", documents::server_capabilities()},
                {"serverInfo", Json{{"name", "mcpp-ide"}, {"version", kVersion}}},
                {"protocol", kProtocol},
                {"series", kSeries},
                {"mcpp", check["mcpp"]},
                {"languageService", check["languageService"]},
                {"checks", check["checks"]},
                {"projects", found},
                {"features", Json::array({"jobs", "results", "launch", "debug", "tests", "library", "toolchains",
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
        if (p.contains("mcpplsServes") && p["mcpplsServes"].is_boolean()) mcpplsServes_ = p["mcpplsServes"].get<bool>();
        if (p.contains("locale")) {
            std::lock_guard localeLock(localeMutex_);
            locale_ = i18n::normalize_locale(p["locale"].get<std::string>());
        }
    }
    if (before != resolved["values"].value("mcpp.path", "")) locate_mcpp();
    // A switch or a severity may have changed what each open document earns.
    for (const auto& d : documents_.all()) publish(d);
    return resolved;
}

std::expected<Json, rpc::Error> Session::job_start(const Json& p, std::function<void(std::string_view, const Json&)> after) {
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
            // The programs, so that each part's record is attributed to its program (design 2026-11-10).
            (void)programs_of(root);
            r.args = test_args(p);
            if (p.contains("filter") && p["filter"].is_string() && !p["filter"].get<std::string>().empty())
                r.args.push_back(p["filter"].get<std::string>());
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
        case jobs::Kind::launch: {
            // A Run's or a Debug's build, reported as it goes: mcpp's narration, and with
            // `launch-stream` its events, then the launch in `job/finished` (design 2026-11-07 §4.9).
            const bool stream = p.value("test", "").empty() && has_feature(root, "launch-stream");
            r.args = launch_args(p, stream, true);
            r.mode = stream ? jobs::Mode::ndjson : jobs::Mode::envelope;
            break;
        }
        case jobs::Kind::command:
            r.args = {};
            break;
    }
    if (*kind != jobs::Kind::launch)
        for (const auto& a : extra) r.args.push_back(a);
    if (r.args.empty()) return std::unexpected(invalid("a command job names its arguments"));
    r.after = std::move(after);
    const auto id = jobs_->submit(std::move(r));
    watcher_->watch(root);
    return Json{{"jobId", id}};
}

// The mcpp command that builds a program and describes its launch: `mcpp run --print-launch`, or
// `mcpp test <name> --print-launch` for a test (mcpp's `test-launch`); the cases asked for are
// selected by the protocol's variables (debug_configuration). `stream` asks a run for the build's events before the
// envelope (mcpp's `launch-stream`), and `programArgs` are the program's own arguments, after `--`.
std::vector<std::string> Session::launch_args(const Json& p, bool stream, bool programArgs) {
    const auto test = p.value("test", "");
    std::vector<std::string> args{test.empty() ? "run" : "test", "--print-launch"};
    if (stream && test.empty()) {
        args.push_back("--message-format");
        args.push_back("json");
    }
    const auto profile = p.value("profile", setting("mcpp.build.profile").is_string() ? setting("mcpp.build.profile").get<std::string>() : "dev");
    if (profile == "release") args.push_back("--release");
    else if (!profile.empty() && profile != "dev") {
        args.push_back("--profile");
        args.push_back(profile);
    }
    for (const auto& a : target_args(p)) args.push_back(a);
    if (auto runner = p.value("runner", ""); !runner.empty()) {
        args.push_back("--runner");
        args.push_back(runner);
    }
    if (!test.empty()) {
        args.push_back(test);
    } else {
        if (auto bin = p.value("bin", ""); !bin.empty()) args.push_back(bin);
        if (const auto program = strings_of(p.value("args", Json::array())); programArgs && !program.empty()) {
            args.push_back("--");
            args.insert(args.end(), program.begin(), program.end());
        }
    }
    return args;
}

std::expected<Json, rpc::Error> Session::run_launch(const Json& p, const rpc::Context& ctx) {
    const auto root = root_of(p);
    if (root.empty()) return std::unexpected(invalid("run/launch names its project's root"));
    auto r = query(root, launch_args(p, false, false));
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

// A debug session starts the launch selected by mcpp. A named `debuggee`
// declares the host-debugger form; other runner names retain their own meaning.
// Configurations come from that actual build, including target-set selection
// and custom-profile build-program declarations.
std::expected<Json, rpc::Error> Session::debug_configuration(const Json& p, const rpc::Context& ctx) {
    auto launch = run_launch(p, ctx);
    if (!launch) return launch;
    if (!p.contains("runner")) {
        const auto configuration = launch->value("configuration", Json::object());
        const auto runners = configuration.is_object()
            ? strings_of(configuration.value("runners", Json::array())) : std::vector<std::string>{};
        if (std::ranges::find(runners, "debuggee") != runners.end()) {
            const auto triple = configuration.value("triple", "");
            const auto profile = configuration.value("profile", "");
            if (triple.empty() || profile.empty())
                return std::unexpected(rpc::Error{rpc::REQUEST_FAILED, t("mcpp's launch configuration omits its target or profile")});
            Json parameters = p;
            parameters["target"] = triple;
            parameters["profile"] = profile;
            parameters["runner"] = "debuggee";
            launch = run_launch(parameters, ctx);
            if (!launch) return launch;
            const auto selected = launch->value("configuration", Json::object());
            if (!selected.is_object() || selected.value("triple", "") != triple
                || selected.value("profile", "") != profile
                || selected.value("platform", Json(nullptr)) != configuration.value("platform", Json(nullptr)))
                return std::unexpected(rpc::Error{rpc::REQUEST_FAILED, t("mcpp's debug launch changed the selected target, profile or platform")});
        }
    }
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
    // SOME CASES OF A PROGRAM OF THE TEST PROTOCOL, DEBUGGED (mcpp test spec P2, P4): the debugger
    // starts the program as a runner would, with the protocol's variables and the cases selected;
    // the report goes to a file of this session's own. Any program the launch says implements the
    // protocol is debugged so, whatever framework wrote it.
    if (const auto cases = strings_of(p.value("cases", Json::array())); !cases.empty() && l.value("protocol", Json(nullptr)).is_number()) {
        const auto dir = host_path(mcpplibs::os::fs::utf8(mcpplibs::os::dirs::temp())) /
                         std::format("mcpp-ide-debug-{}", std::random_device{}());
        std::error_code ec;
        std::filesystem::create_directories(dir / "tmp", ec);
        std::string lines;
        for (const auto& c : cases) lines += c + "\n";
        std::ofstream(dir / "select.txt", std::ios::binary) << lines;
        env["MTEST_PROTOCOL"] = std::to_string(l["protocol"].get<int>());
        env["MTEST_MODE"] = "run";
        env["MTEST_SELECT"] = (dir / "select.txt").generic_string();
        env["MTEST_REPORT"] = (dir / "report.ndjson").generic_string();
        env["MTEST_TMPDIR"] = (dir / "tmp").generic_string();
    }
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
        cfg["mcppNote"] = t("the debugger starts {0}; source symbols depend on the runner's debugger support",
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
    const auto parent = host_path(p.value("parent", ""));
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
    const auto root = any_root(p);
    const auto report = languageservice::report(s, root);
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
    // What mcppls says about the workspace (§5.2): its capabilities, the workspace's state and
    // mcppls's actions under each editor's id, from `mcppls status` or the frozen table.
    out["root"] = root.generic_string();
    out["statusSource"] = report.source;
    out["capabilities"] = report.capabilities;
    out["serverActions"] = report.actions;
    out["workspace"] = report.workspace;
    out["instances"] = report.instances;
    if (!report.detail.empty()) out["statusError"] = report.detail;
    // A change the editors did not see yet: another mcppls, other actions, another state.
    const auto seen = Json{s.version, s.program.generic_string(), report.actions,
                           report.workspace.is_object() ? report.workspace.value("state", "") : std::string{}}.dump();
    bool changed = false;
    {
        std::lock_guard lock(mutex_);
        changed = !mcpplsSeen_.empty() && mcpplsSeen_ != seen;
        mcpplsSeen_ = seen;
        mcpplsActions_ = report.actions;
    }
    if (changed) rpc_.notify("mcpp-ide/languageService/changed", Json{{"root", out["root"]}, {"state", out["state"]},
                                                             {"workspaceState", report.workspace.is_object() ? report.workspace.value("state", "none") : "none"}});
    return out;
}

// mcppls's actions, asked for once when no report has named them yet.
Json Session::server_actions() {
    {
        std::lock_guard lock(mutex_);
        if (!mcpplsActions_.is_null()) return mcpplsActions_;
    }
    (void)language_service(Json::object());
    std::lock_guard lock(mutex_);
    return mcpplsActions_.is_null() ? Json::array() : mcpplsActions_;
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
    // `since` is the first mcpp that states the feature; a feature no release states yet names the
    // issue that proposes it instead (`issue`), and its message says so.
    struct Gate { const char* id; const char* feature; const char* since; const char* issue; };
    static constexpr Gate kGates[] = {
        {"targets.sets", "target-sets", "2026.10.30.1", nullptr},
        {"pack.formatList", "pkg-format-list", "2026.10.30.1", nullptr},
        {"tests.debug", "test-launch", "2026.10.31.2", nullptr},
        {"tests.cases", "test-protocol", "2026.11.8.1", nullptr},
        {"tests.parts", "test-parts", "", "mcpp#917"},
        {"tests.live", "test-case-stream", "", "mcpp#917"},
        {"tests.metrics", "test-metrics", "", "mcpp#917"},
    };
    Json features = Json::array();
    for (const auto& g : kGates) {
        const bool on = std::ranges::find(probe.features, g.feature) != probe.features.end();
        Json f{{"id", g.id}, {"available", on}};
        if (!on) {
            const bool known = probe.state == "ok" || probe.state == "not-probed";
            const auto message = !known ? probe.message
                                 : g.issue ? t("the project's mcpp {0} does not state {1}, which {2} proposes", {probe.version, g.feature, g.issue})
                                           : t("the project's mcpp {0} does not state {1}; mcpp {2} does", {probe.version, g.feature, g.since});
            f["reason"] = Json{{"source", "mcpp"}, {"capability", g.feature}, {"need", g.issue ? g.issue : g.since},
                               {"have", probe.version}, {"message", message}};
        }
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
std::expected<Json, rpc::Error> Session::commands(const Json& p) {
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
    // A forward runs mcppls's action under this editor's id, which mcppls states (§5.3); an action it
    // gives this editor no id for is not offered.
    Json actions = Json::array();
    const bool forwards = std::ranges::any_of(table["commands"], [&](const Json& c) {
        return spec::offered_in(c, editor) && c.contains("action") && c["action"].value("kind", "") == "forward";
    });
    if (forwards) actions = server_actions();
    Json groups = Json::object();
    for (const auto& g : table["groups"]) groups[g.value("id", "")] = spec::manifest_string(g.value("title", ""), locale);
    Json out = Json::array();
    for (const auto& c : table["commands"]) {
        if (!spec::offered_in(c, editor)) continue;
        Json row = c;
        if (c.contains("action") && c["action"].value("kind", "") == "forward") {
            const auto wanted = c["action"].value("mcppls", "");
            const auto it = std::ranges::find_if(actions, [&](const Json& a) { return a.value("id", "") == wanted && a.contains(editor); });
            if (it == actions.end()) continue;
            row["action"]["command"] = (*it)[editor];
            if (editor == "vscode" && it->contains("vscodeArguments")) row["action"]["arguments"] = (*it)["vscodeArguments"];
        }
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
// record per program), each with its files and parts (design 2026-11-10), else the test targets of
// the build database. The programs are kept per project: a run's record is attributed to its
// program by them.
std::expected<Json, rpc::Error> Session::test_list(const Json& p) {
    const auto root = root_of(p);
    if (root.empty()) return std::unexpected(invalid("test/list names its project's root"));
    Json tests = Json::array();
    for (const auto& program : list_programs(root, p)) tests.push_back(testing::program_json(program));
    if (tests.empty()) {
        auto db = build_database(root);
        if (db) for (const auto& row : views::project_summary(*db, Json(nullptr))["tests"])
            tests.push_back(Json{{"test", row.value("name", "")}, {"member", ""}, {"main", ""}, {"files", Json::array()},
                                 {"parts", Json::array()}, {"layout", "file"}});
    }
    return Json{{"root", root.generic_string()}, {"tests", tests}};
}

std::vector<std::string> Session::plan_packages(const std::filesystem::path& root, bool read) {
    Json db;
    {
        std::lock_guard lock(mutex_);
        if (auto it = buildDatabases_.find(root); it != buildDatabases_.end()) db = it->second;
    }
    if (db.is_null() && read)
        if (auto r = build_database(root)) db = *r;
    std::vector<std::string> out;
    if (!db.is_object()) return out;
    const auto& data = db.contains("data") ? db["data"] : db;
    const auto& database = data.contains("database") ? data["database"] : data;
    for (const auto& set : database.value("sets", Json::array())) {
        for (const auto* key : {"family-name", "name"})
            if (set.contains(key) && set[key].is_string() && std::ranges::find(out, set[key].get<std::string>()) == out.end())
                out.push_back(set[key].get<std::string>());
    }
    return out;
}

namespace {

std::string read_text(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), {});
}

// A test's name by mcpp's convention: the path below tests/ without its extension (docs/08).
std::optional<std::string> test_name_of(const std::filesystem::path& root, const std::filesystem::path& file) {
    const auto ext = file.extension().string();
    if (ext != ".cpp" && ext != ".cc" && ext != ".cxx") return std::nullopt;
    // Both as the file system spells them: a temporary directory behind a link (macOS's /var), a
    // drive letter in either case (Windows).
    std::error_code ec;
    auto canonical = [&](const std::filesystem::path& p) {
        auto c = std::filesystem::weakly_canonical(p, ec);
        return ec ? p.lexically_normal() : c;
    };
    const auto rel = canonical(file).lexically_relative(canonical(root));
    if (rel.empty() || *rel.begin() != "tests") return std::nullopt;
    std::filesystem::path below;
    bool first = true;
    for (const auto& part : rel) {
        if (first) { first = false; continue; }
        below /= part;
    }
    below.replace_extension();
    return below.generic_string();
}

// The source of a test by its name: tests/<name>.cpp (or .cc, .cxx).
std::filesystem::path test_source(const std::filesystem::path& root, std::string_view name) {
    for (const auto* ext : {".cpp", ".cc", ".cxx"}) {
        auto file = root / "tests" / (std::string(name) + ext);
        std::error_code ec;
        if (std::filesystem::is_regular_file(file, ec)) return file;
    }
    return {};
}

}  // namespace

std::vector<testing::Program> Session::list_programs(const std::filesystem::path& root, const Json& p) {
    mcpp::Client client;
    {
        std::lock_guard lock(mutex_);
        client = client_;
    }
    std::vector<testing::Program> programs;
    if (!client.ready()) return programs;
    mcpp::RunOptions o;
    o.cwd = root;
    o.args = test_args(p);
    o.args.push_back("--list");
    o.deny = deny();
    o.client = client_label();
    const auto definesMain = [&](const std::filesystem::path& f) { return scan::scan(read_text(f)).mainLine.has_value(); };
    if (auto r = client.records(o); r && r->exitCode == 0)
        for (const auto& rec : r->records)
            if (rec.is_object() && rec.contains("test") && rec["test"].is_string())
                programs.push_back(testing::program_of(rec, definesMain));
    std::lock_guard lock(mutex_);
    programs_[root] = programs;
    return programs;
}

// The project's programs as last listed, listed now when they were not.
std::vector<testing::Program> Session::programs_of(const std::filesystem::path& root) {
    {
        std::lock_guard lock(mutex_);
        if (auto it = programs_.find(root); it != programs_.end()) return it->second;
    }
    return list_programs(root, Json::object());
}

std::optional<testing::Program> Session::program_named(const std::filesystem::path& root, std::string_view name) {
    for (auto& program : programs_of(root))
        if (program.name == name) return program;
    return std::nullopt;
}

// The program of several files a file belongs to (a file program is found by its name).
std::optional<testing::Program> Session::program_of_file(const std::filesystem::path& root, const std::filesystem::path& file) {
    for (auto& program : programs_of(root))
        if (program.layout == "directory" && !testing::part_of_file(program, file).empty()) return program;
    return std::nullopt;
}

// What a C++ file is to its project (§6.3): a test's or a program's `main`, or neither, with the
// line of `main` read from the text (approximate: an editor takes its language service's position
// when it has one). Nothing is built and nothing run.
std::expected<Json, rpc::Error> Session::source_entries(const Json& p) {
    const std::filesystem::path file(p.value("file", ""));
    if (file.empty()) return std::unexpected(invalid("source/entries names a file"));
    auto proj = projects::project_for(file.generic_string(), p.value("boundary", ""));
    if (!proj) return Json{{"file", file.generic_string()}, {"kind", "none"}};
    const auto text = p.contains("text") && p["text"].is_string() ? p["text"].get<std::string>() : read_text(file);
    const auto found = scan::scan(text);
    Json out{{"file", file.generic_string()}, {"root", proj->root.generic_string()}, {"kind", "none"}};
    if (auto name = test_name_of(proj->root, file)) {
        out["kind"] = "test";
        out["test"] = *name;
        // A file of a program of several files: the program, and the part of it the file is.
        if (auto program = program_of_file(proj->root, file)) {
            out["test"] = program->name;
            out["part"] = testing::part_of_file(*program, file);
        }
    } else if (found.mainLine) {
        out["kind"] = "program";
    }
    if (found.mainLine && out["kind"] != "none") out["main"] = Json{{"line", *found.mainLine}, {"approximate", true}};
    return out;
}

// A test program's cases (§7.3): as its sources name them -- every file of a program of several
// (design 2026-11-10), each case with its file -- and, when `list` is asked for and the workspace is
// trusted, as the program lists them.
std::expected<Json, rpc::Error> Session::test_cases(const Json& p) {
    std::filesystem::path root = root_of(p);
    std::string test = p.value("test", "");
    std::filesystem::path file(p.value("file", ""));
    if (!file.empty() && root.empty())
        if (auto proj = projects::project_for(file.generic_string(), "")) root = proj->root;
    std::optional<testing::Program> program;
    if (!root.empty() && !file.empty() && test.empty()) {
        program = program_of_file(root, file);
        if (program) test = program->name;
        else if (auto name = test_name_of(root, file)) test = *name;
    }
    if (root.empty() || test.empty()) return std::unexpected(invalid("test/cases names a project's test, or a test's file"));
    if (!program) program = program_named(root, test);
    std::vector<std::filesystem::path> files;
    if (program && !program->files.empty()) for (const auto& f : program->files) files.emplace_back(f);
    else if (!file.empty()) files.push_back(file);
    else if (auto source = test_source(root, test); !source.empty()) files.push_back(source);
    const auto shown = program && !program->main.empty() ? std::filesystem::path(program->main) : files.empty() ? file : files.front();
    Json out{{"root", root.generic_string()}, {"test", test}, {"file", shown.generic_string()}, {"framework", nullptr},
             {"protocol", nullptr}, {"cases", Json::array()}};
    if (files.empty()) return out;
    if (program && program->layout == "directory") out["files"] = program->files;
    // `main`, read from the text (approximate); nothing of any framework is read.
    if (const auto found = scan::scan(read_text(shown)); found.mainLine)
        out["main"] = Json{{"line", *found.mainLine}, {"approximate", true}};
    bool trusted;
    {
        std::lock_guard lock(mutex_);
        trusted = trusted_;
    }
    // THE CASES ARE THE PROGRAM'S (mcpp test spec B2, B3): a program that implements the test
    // protocol lists them (`mcpp test --cases`), whatever framework or language wrote it, and its
    // cases run by `--case`. A program that implements none is one case, judged by its exit status.
    // `list` asks for a listing when the project changed since the last; without it the last one
    // answers, or none.
    if (!trusted || !has_feature(root, "test-protocol")) return out;
    auto listed = listed_cases(root, test, p, p.value("list", false));
    if (!listed) return out;
    out["protocol"] = 1;
    out["framework"] = framework_name(root, p);
    out["listed"] = true;
    for (const auto& c : *listed) out["cases"].push_back(testing::case_json(c));
    return out;
}

// The project's listing (mcpp test spec B3): one `mcpp test --cases` for every program, kept until
// the project changes. A listing per program planned the project once for each; the test fast path
// (mcpp 2026.11.10.1) answers an unchanged project without planning.
std::optional<std::vector<Json>> Session::project_listing(const std::filesystem::path& root, const Json& p, bool ask) {
    {
        std::lock_guard lock(mutex_);
        if (auto it = listings_.find(root); it != listings_.end() && (it->second.current || !ask)) return it->second.records;
    }
    if (!ask) return std::nullopt;
    mcpp::Client client;
    {
        std::lock_guard lock(mutex_);
        client = client_;
    }
    if (!client.ready()) return std::nullopt;
    mcpp::RunOptions o;
    o.cwd = root;
    o.args = test_args(p);
    o.args.push_back("--cases");
    o.deny = deny();
    o.client = client_label();
    auto r = client.records(o);
    if (!r) return std::nullopt;
    std::vector<Json> records;
    for (const auto& rec : r->records)
        if (rec.is_object() && rec.contains("test")) records.push_back(rec);
    std::lock_guard lock(mutex_);
    listings_[root] = Listing{records, true};
    return records;
}

Session::Frameworks Session::frameworks_of(const std::filesystem::path& root, const Json& p) {
    {
        std::lock_guard lock(mutex_);
        if (auto it = frameworks_.find(root); it != frameworks_.end()) return it->second;
    }
    Frameworks out{"protocol", {}};
    mcpp::Client client;
    {
        std::lock_guard lock(mutex_);
        client = client_;
    }
    if (!client.ready()) return out;
    mcpp::RunOptions o;
    o.cwd = root;
    o.args = test_args(p);
    o.args.push_back("--guide");
    o.deny = deny();
    o.client = client_label();
    if (auto r = client.records(o))
        for (const auto& rec : r->records) {
            if (!rec.is_object() || !rec.contains("guide")) continue;
            const auto list = rec["guide"].value("frameworks", Json::array());
            if (list.size() == 1) out.name = list[0].value("package", out.name);
            for (const auto& f : list) {
                const auto caps = f.value("capabilities", Json::array());
                const bool session = std::ranges::find(caps, Json("session")) != caps.end();
                if (session && f.contains("session") && f["session"].is_array() && !f["session"].empty())
                    out.session = strings_of(f["session"]);
            }
        }
    std::lock_guard lock(mutex_);
    frameworks_[root] = out;
    return out;
}

std::optional<std::vector<testing::Case>> Session::listed_cases(const std::filesystem::path& root, const std::string& test,
                                                                const Json& p, bool ask) {
    auto records = project_listing(root, p, ask);
    if (!records) return std::nullopt;
    for (const auto& rec : *records) {
        if (rec.value("test", "") != test || !rec.contains("listed") || !rec["protocol"].is_number()) continue;
        std::vector<testing::Case> listed;
        for (const auto& c : testing::protocol_listing(rec)) {
            testing::Case one{c.id, c.line, "listed"};
            one.file = c.file;
            one.kind = c.kind;
            one.tags = c.tags;
            one.expect = c.expect;
            one.bounds = c.bounds;
            listed.push_back(std::move(one));
        }
        return listed;
    }
    return std::nullopt;
}

// Runs some of a test program's cases (mcpp test spec P4): `mcpp test <test> --case <id>...`, each
// case's outcome sent as `test/caseResult` from the program's records when the job ends (and as it
// happens through `test/caseEvent`, test-case-stream). Any program that implements the test
// protocol is run so, whatever framework wrote it; the `framework` a client names is not read.
std::expected<Json, rpc::Error> Session::test_run_cases(const Json& p) {
    const auto root = root_of(p);
    const auto test = p.value("test", "");
    if (root.empty() || test.empty()) return std::unexpected(invalid("test/runCases names a project and a test"));
    const auto cases = strings_of(p.value("cases", Json::array()));
    if (auto through = run_through_session(root, test, cases, p)) return *through;
    Json args = Json::array();
    for (const auto& c : cases) {
        args.push_back("--case");
        args.push_back(c);
    }
    Json job{{"kind", "test"}, {"root", root.generic_string()}, {"filter", test}, {"args", args}, {"color", p.value("color", true)}};
    for (const auto* key : {"profile", "target"}) if (p.contains(key)) job[key] = p[key];
    return job_start(job, [this, root, test](std::string_view jobId, const Json&) {
        // Every record of the job: a program of several files reports one per part.
        std::vector<Json> records;
        {
            std::lock_guard lock(mutex_);
            if (auto it = testRecords_.find(jobId); it != testRecords_.end()) records = it->second;
        }
        for (const auto& record : records) protocol_case_results(jobId, root, test, record);
    });
}

std::optional<Json> Session::run_through_session(const std::filesystem::path& root, const std::string& test,
                                                 const std::vector<std::string>& cases, const Json& p) {
    const auto chosen = setting("mcpp.test.session");
    if (chosen.is_string() && chosen.get<std::string>() == "off") return std::nullopt;
    bool trusted;
    mcpp::Client client;
    {
        std::lock_guard lock(mutex_);
        trusted = trusted_;
        client = client_;
    }
    if (!trusted || !client.ready() || !has_feature(root, "test-launch-records")) return std::nullopt;
    const auto command = frameworks_of(root, p).session;
    if (command.empty()) return std::nullopt;
    testsession::Client* session = nullptr;
    {
        std::lock_guard lock(mutex_);
        if (auto it = testSessions_.find(root); it != testSessions_.end() && it->second->alive()) session = it->second.get();
    }
    if (!session) {
        auto started = testsession::Client::start(
            command, root, {{"MTEST_MCPP", client.program().string()}},
            [this, root](std::string_view method, const Json& params) { on_session(root, method, params); });
        if (!started) {
            rpc_.notify("window/logMessage", Json{{"type", 2}, {"message", started.error()}});
            return std::nullopt;
        }
        std::lock_guard lock(mutex_);
        session = (testSessions_[root] = std::move(*started)).get();
    }
    // The configuration a run of mcpp plans, as every test command states it (test_args).
    auto args = test_args(p);
    args.erase(args.begin(), args.begin() + 3);   // `test --message-format json`
    Json selection = Json::array({Json{{"program", test}, {"cases", cases}}});
    std::string jobId;
    {
        std::lock_guard lock(mutex_);
        jobId = std::format("job-s{}", sessionJobNext_++);
    }
    // The run is known, and its job started, before the session's first notification of it.
    auto answered = session->request(
        "test/run", Json{{"project", root.generic_string()}, {"selection", selection}, {"options", Json{{"args", args}}}},
        std::chrono::seconds{60}, [this, root, test, jobId](const Json& result) {
            if (!result.is_object() || !result.contains("run") || !result["run"].is_string()) return;
            const auto run = result["run"].get<std::string>();
            {
                std::lock_guard lock(mutex_);
                sessionRuns_[run] = SessionRun{jobId, root, {}};
                sessionJobs_[jobId] = {root, run};
            }
            rpc_.notify("mcpp-ide/job/started", Json{{"jobId", jobId}, {"kind", "test"}, {"root", root.generic_string()},
                                                     {"argv", Json::array({"test/run", test})}});
        });
    if (!answered || !answered->contains("run")) return std::nullopt;
    return Json{{"jobId", jobId}};
}

// What the session states, as the editors receive a job's: a case's beginning and end
// (`test/caseEvent`, with its failures and output as they came), a program's output where it has no
// case of its own, and the run's end (`job/finished`).
void Session::on_session(const std::filesystem::path& root, std::string_view method, const Json& params) {
    const auto run = params.value("run", "");
    std::string jobId;
    {
        std::lock_guard lock(mutex_);
        auto it = sessionRuns_.find(run);
        if (it == sessionRuns_.end()) return;
        jobId = it->second.jobId;
    }
    const auto program = params.value("program", "");
    if (method == "test/case") {
        const auto id = params.value("id", "");
        const auto event = params.value("event", "");
        if (event == "begin") {
            Json e{{"jobId", jobId}, {"root", root.generic_string()}, {"member", ""}, {"program", program}, {"part", program},
                   {"case", id}, {"event", "begin"}};
            if (params.contains("t_ms")) e["tMs"] = params["t_ms"];
            rpc_.notify("mcpp-ide/test/caseEvent", e);
        } else if (event == "output" || event == "failure") {
            std::lock_guard lock(mutex_);
            auto& c = sessionRuns_[run].cases[id];
            if (!c.is_object()) c = Json::object();
            if (event == "output") c["output"] = c.value("output", std::string{}) + params.value("text", std::string{});
            else {
                Json f = params;
                for (const auto* k : {"run", "program", "event", "id"}) f.erase(k);
                c["failures"].push_back(std::move(f));
            }
        } else if (event == "end") {
            Json c{{"id", id}, {"status", params.value("status", std::string("fail"))}, {"duration_ms", params.value("ms", 0.0)},
                   {"reason", params.value("reason", std::string{})}};
            if (params.contains("checks")) c["checks"] = params["checks"];
            {
                std::lock_guard lock(mutex_);
                const auto& kept = sessionRuns_[run].cases[id];
                if (kept.contains("failures")) c["failures"] = kept["failures"];
                if (kept.contains("output")) c["output"] = kept["output"];
            }
            Json e = testing::protocol_case(c);
            e["jobId"] = jobId;
            e["root"] = root.generic_string();
            e["member"] = "";
            e["program"] = program;
            e["part"] = program;
            e["event"] = "end";
            rpc_.notify("mcpp-ide/test/caseEvent", e);
        }
    } else if (method == "test/program" || method == "test/build") {
        std::string text;
        if (params.contains("output") && params["output"].is_string()) text = params["output"].get<std::string>();
        if (params.contains("error") && params["error"].is_string()) text += params["error"].get<std::string>() + "\n";
        if (!text.empty()) rpc_.notify("mcpp-ide/job/output", Json{{"jobId", jobId}, {"text", text}});
    } else if (method == "test/run") {
        const auto status = params.value("status", std::string("fail"));
        {
            std::lock_guard lock(mutex_);
            sessionRuns_.erase(run);
            sessionJobs_.erase(jobId);
        }
        rpc_.notify("mcpp-ide/job/finished", Json{{"jobId", jobId}, {"kind", "test"}, {"root", root.generic_string()},
                                                  {"status", status == "pass" ? "ok" : status == "cancelled" ? "cancelled" : "failed"},
                                                  {"exitCode", status == "pass" ? 0 : 1}});
    }
}

Json Session::test_result_of(const std::filesystem::path& root, const Json& record, Json n) {
    std::vector<testing::Program> programs;
    {
        std::lock_guard lock(mutex_);
        if (auto it = programs_.find(root); it != programs_.end()) programs = it->second;
    }
    const auto name = record.value("test", "");
    const auto program = testing::owner_of(name, n.value("partOf", ""), programs);
    n["program"] = program;
    if (program != name) n["part"] = name;
    if (record.contains("protocol") && record["protocol"].is_number()) n["protocol"] = record["protocol"];
    if (record.contains("cases") && record["cases"].is_array()) {
        Json cases = Json::array();
        for (const auto& o : testing::protocol_outcomes(record)) cases.push_back(o.detail);
        n["cases"] = std::move(cases);
    }
    if (record.contains("usage") && record["usage"].is_object()) n["usage"] = record["usage"];
    return n;
}

// The cases of one record of a test/runCases job, each sent as `test/caseResult` with its program
// and part, a failure's message as its output. The record names them, as a test/result's does: the
// job's filter may be a part, and mcpp names a run that selects cases after the program.
void Session::protocol_case_results(std::string_view jobId, const std::filesystem::path& root, const std::string& test,
                                    const Json& record) {
    std::vector<testing::Program> programs;
    {
        std::lock_guard lock(mutex_);
        if (auto it = programs_.find(root); it != programs_.end()) programs = it->second;
    }
    const auto part = record.value("test", "");
    const auto program = testing::owner_of(part, record.value("part_of", ""), programs);
    for (const auto& o : testing::protocol_outcomes(record)) {
        bool truncated = false;
        auto output = views::bounded_output(o.outcome.status == "failed" ? std::string_view(o.outcome.message) : std::string_view{}, truncated);
        Json n = o.detail;
        n["jobId"] = jobId;
        n["root"] = root.generic_string();
        n["test"] = test;
        n["program"] = program;
        if (part != program) n["part"] = part;
        n["output"] = std::move(output);
        n["outputTruncated"] = truncated;
        rpc_.notify("mcpp-ide/test/caseResult", n);
    }
}

std::vector<teststore::Run> Session::store_runs(const std::filesystem::path& root, std::size_t count) {
    const auto dir = teststore::store_dir(root);
    std::map<std::string, teststore::Run> cache;
    {
        std::lock_guard lock(mutex_);
        cache = storeRuns_[root];
    }
    auto runs = teststore::last_runs(dir, count, cache);
    {
        std::lock_guard lock(mutex_);
        storeRuns_[root] = std::move(cache);
    }
    const auto programs = programs_of(root);
    teststore::attribute(runs, [&](std::string_view name) { return testing::owner_of(name, "", programs); });
    return runs;
}

// The window of runs a request asks for: `runs` (20 by default, 50 at most).
static std::size_t window_of(const Json& p) {
    const auto asked = p.contains("runs") && p["runs"].is_number_integer() ? p["runs"].get<long long>() : 20;
    return static_cast<std::size_t>(std::clamp<long long>(asked, 1, 50));
}

// A project's test runs as mcpp kept them (design 2026-11-10): each case's results across the last runs.
std::expected<Json, rpc::Error> Session::test_history(const Json& p) {
    const auto root = root_of(p);
    if (root.empty()) return std::unexpected(invalid("test/history names its project's root"));
    auto out = teststore::history(store_runs(root, window_of(p)), p.value("program", ""), p.value("case", ""));
    out["root"] = root.generic_string();
    out["store"] = teststore::store_dir(root).generic_string();
    return out;
}

// What the last runs say: the run, its failures, the slowest and the flaky cases, the metrics against
// their bounds, the schedule. A store mcpp has not written yet answers no run.
std::expected<Json, rpc::Error> Session::test_insights(const Json& p) {
    const auto root = root_of(p);
    if (root.empty()) return std::unexpected(invalid("test/insights names its project's root"));
    auto out = teststore::insights(store_runs(root, window_of(p)));
    out["root"] = root.generic_string();
    out["store"] = teststore::store_dir(root).generic_string();
    out["features"] = Json{{"parts", has_feature(root, "test-parts")}, {"live", has_feature(root, "test-case-stream")},
                           {"metrics", has_feature(root, "test-metrics")}};
    return out;
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

documents::Options Session::document_options() const {
    std::lock_guard lock(mutex_);
    std::string locale;
    {
        std::lock_guard localeLock(localeMutex_);
        locale = locale_;
    }
    return documents::options_of(settings_, trusted_, mcpplsServes_, locale);
}

void Session::publish(const documents::Document& d) {
    if (d.kind == documents::Kind::other) return;
    rpc_.notify("textDocument/publishDiagnostics",
                Json{{"uri", d.uri}, {"version", d.version}, {"diagnostics", documents::diagnostics(d, document_options())}});
}

std::optional<Json> Session::catalogue(const std::filesystem::path& cwd) {
    {
        std::lock_guard lock(mutex_);
        if (!searchCache_.is_null() && std::chrono::steady_clock::now() - searchAt_ < std::chrono::minutes(10)) return searchCache_;
        if (catalogueAsked_) return std::nullopt;
        catalogueAsked_ = true;
        if (!catalogueNoticed_) {
            catalogueNoticed_ = true;
            rpc_.notify("window/showMessage",
                        Json{{"type", 3}, {"message", t("Completing dependency versions runs `mcpp search`, which may use the network.")}});
        }
    }
    std::thread([this, cwd] {
        auto r = query(cwd, {"search", "--format", "json"});
        std::lock_guard lock(mutex_);
        catalogueAsked_ = false;
        if (r && r->envelope.is_object() && r->envelope.value("kind", "") == "mcpp.search") {
            searchCache_ = r->envelope;
            searchAt_ = std::chrono::steady_clock::now();
        }
    }).detach();
    return std::nullopt;
}

void Session::install_documents() {
    auto& s = rpc_;
    s.on_notification("initialized", [](const Json&) {});
    s.on_notification("textDocument/didOpen", [this](const Json& p) {
        const auto& d = p.value("textDocument", Json::object());
        const auto uri = d.value("uri", "");
        documents_.open(uri, d.value("version", 0), d.value("text", ""));
        if (auto doc = documents_.get(uri)) publish(*doc);
    });
    s.on_notification("textDocument/didChange", [this](const Json& p) {
        const auto& d = p.value("textDocument", Json::object());
        const auto uri = d.value("uri", "");
        const auto changes = p.value("contentChanges", Json::array());
        // Full synchronisation: the last change is the whole text.
        if (changes.empty() || !changes.back().contains("text")) return;
        documents_.change(uri, d.value("version", 0), changes.back()["text"].get<std::string>());
        if (auto doc = documents_.get(uri)) publish(*doc);
    });
    s.on_notification("textDocument/didClose", [this](const Json& p) {
        const auto uri = p.value("textDocument", Json::object()).value("uri", "");
        documents_.close(uri);
        rpc_.notify("textDocument/publishDiagnostics", Json{{"uri", uri}, {"diagnostics", Json::array()}});
    });
    // Answered on the reading thread: each sees the documents as the notifications before it left them.
    auto at = [this](const Json& p) -> std::optional<std::tuple<documents::Document, int, int>> {
        auto doc = documents_.get(p.value("textDocument", Json::object()).value("uri", ""));
        if (!doc) return std::nullopt;
        const auto& pos = p.value("position", Json::object());
        return std::tuple{*doc, pos.value("line", 0), pos.value("character", 0)};
    };
    s.on_inline("textDocument/completion", [this, at](const Json& p, const rpc::Context&) -> Result {
        auto a = at(p);
        if (!a) return Json(nullptr);
        const auto& [doc, line, character] = *a;
        const auto options = document_options();
        bool need = false;
        auto list = documents::completion(doc, line, character, options, nullptr, need);
        if (need) {
            if (auto c = catalogue(doc.path.parent_path())) list = documents::completion(doc, line, character, options, &*c, need);
        }
        return list;
    });
    s.on_inline("textDocument/hover", [this, at](const Json& p, const rpc::Context&) -> Result {
        auto a = at(p);
        if (!a) return Json(nullptr);
        const auto& [doc, line, character] = *a;
        return documents::hover(doc, line, character, document_options());
    });
    s.on_inline("textDocument/definition", [this, at](const Json& p, const rpc::Context&) -> Result {
        auto a = at(p);
        if (!a) return Json(nullptr);
        const auto& [doc, line, character] = *a;
        return documents::definition(doc, line, character, document_options());
    });
    s.on_inline("textDocument/documentSymbol", [this](const Json& p, const rpc::Context&) -> Result {
        auto doc = documents_.get(p.value("textDocument", Json::object()).value("uri", ""));
        if (!doc) return Json(nullptr);
        return documents::symbols(*doc);
    });
}

void Session::install() {
    auto& s = rpc_;
    install_documents();
    s.on("initialize", [this](const Json& p, const rpc::Context&) { return initialize(p); });
    s.on("mcpp-ide/settings/update", [this](const Json& p, const rpc::Context&) { return update_settings(p); });
    s.on("mcpp-ide/settings/registry", [this](const Json&, const rpc::Context&) -> Result {
        std::lock_guard lock(localeMutex_);
        return settings::registry(locale_);
    });
    s.on("mcpp-ide/settings/preset", [](const Json& p, const rpc::Context&) -> Result {
        auto preset = settings::preset(p.value("id", ""));
        if (!preset) return std::unexpected(invalid("no such preset"));
        return *preset;
    });
    s.on("mcpp-ide/i18n/bundle", [this](const Json& p, const rpc::Context&) -> Result {
        std::string locale;
        {
            std::lock_guard lock(localeMutex_);
            locale = locale_;
        }
        return i18n::bundle(p.value("locale", locale));
    });
    s.on("mcpp-ide/selfCheck", [this](const Json&, const rpc::Context&) -> Result { locate_mcpp(); return self_check(); });
    s.on("mcpp-ide/commands/list", [this](const Json& p, const rpc::Context&) { return commands(p); });
    s.on("mcpp-ide/menu/quick", [this](const Json& p, const rpc::Context&) { return quick_menu(p); });
    s.on("mcpp-ide/languageService/status", [this](const Json& p, const rpc::Context&) { return language_service(p); });
    s.on("mcpp-ide/projects/list", [this](const Json& p, const rpc::Context&) -> Result {
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
    s.on("mcpp-ide/projects/forFile", [this](const Json& p, const rpc::Context&) -> Result {
        auto proj = projects::project_for(p.value("path", ""), p.value("boundary", ""));
        if (!proj) return Json(nullptr);
        watcher_->watch(proj->root);
        return project_json(*proj);
    });
    s.on("mcpp-ide/project/summary", [this](const Json& p, const rpc::Context&) { return project_summary(p); });
    s.on("mcpp-ide/mcpp/exec", [this](const Json& p, const rpc::Context&) { return mcpp_exec(p); });
    s.on("mcpp-ide/mcpp/availability", [this](const Json&, const rpc::Context&) { return mcpp_availability(); });
    s.on("mcpp-ide/languageService/exec", [this](const Json& p, const rpc::Context&) { return language_service_exec(p); });
    s.on("mcpp-ide/project/files", [](const Json& p, const rpc::Context&) -> Result { return projects::files(root_of(p)); });
    s.on("mcpp-ide/project/new", [this](const Json& p, const rpc::Context&) { return project_new(p); });
    s.on("mcpp-ide/result/last", [](const Json& p, const rpc::Context&) -> Result { return projects::last_result(root_of(p)); });
    s.on("mcpp-ide/job/start", [this](const Json& p, const rpc::Context&) { return job_start(p); });
    s.on("mcpp-ide/job/cancel", [this](const Json& p, const rpc::Context&) -> Result {
        const auto jobId = p.value("jobId", "");
        testsession::Client* client = nullptr;
        std::string run;
        {
            std::lock_guard lock(mutex_);
            if (auto it = sessionJobs_.find(jobId); it != sessionJobs_.end())
                if (auto c = testSessions_.find(it->second.first); c != testSessions_.end()) {
                    client = c->second.get();
                    run = it->second.second;
                }
        }
        // A run through a test session is cancelled by the session: it ends the processes (O9).
        if (client) return Json{{"cancelled", client->request("test/cancel", Json{{"run", run}}).has_value()}};
        return Json{{"cancelled", jobs_->cancel(jobId)}};
    });
    s.on("mcpp-ide/job/list", [this](const Json&, const rpc::Context&) -> Result { return jobs_->list(); });
    s.on("mcpp-ide/run/launch", [this](const Json& p, const rpc::Context& c) { return run_launch(p, c); });
    s.on("mcpp-ide/debug/configuration", [this](const Json& p, const rpc::Context& c) { return debug_configuration(p, c); });
    s.on("mcpp-ide/test/list", [this](const Json& p, const rpc::Context&) { return test_list(p); });
    s.on("mcpp-ide/source/entries", [this](const Json& p, const rpc::Context&) { return source_entries(p); });
    s.on("mcpp-ide/test/cases", [this](const Json& p, const rpc::Context&) { return test_cases(p); });
    s.on("mcpp-ide/test/history", [this](const Json& p, const rpc::Context&) { return test_history(p); });
    s.on("mcpp-ide/test/insights", [this](const Json& p, const rpc::Context&) { return test_insights(p); });
    s.on("mcpp-ide/test/runCases", [this](const Json& p, const rpc::Context&) { return test_run_cases(p); });
    s.on("mcpp-ide/project/mcpp", [this](const Json& p, const rpc::Context&) { return project_mcpp(p); });
    s.on("mcpp-ide/project/refresh", [this](const Json& p, const rpc::Context&) { return project_refresh(p); });
    s.on("mcpp-ide/capabilities/explain", [this](const Json& p, const rpc::Context&) { return capabilities_explain(p); });
    s.on("mcpp-ide/report/collect", [this](const Json& p, const rpc::Context&) { return report_collect(p); });
    s.on("mcpp-ide/library/search", [this](const Json& p, const rpc::Context&) { return library_search(p); });
    s.on("mcpp-ide/library/indexes", [this](const Json& p, const rpc::Context&) -> Result {
        auto r = query(any_root(p), {"index", "list", "--format", "json"});
        if (!r) return std::unexpected(r.error());
        return r->envelope.is_object() ? r->envelope["data"] : Json(nullptr);
    });
    s.on("mcpp-ide/library/add", [this](const Json& p, const rpc::Context&) -> Result {
        auto dep = p.value("dependency", "");
        if (p.contains("version") && p["version"].is_string() && !p["version"].get<std::string>().empty())
            dep += "@" + p["version"].get<std::string>();
        return job_start(Json{{"kind", "add"}, {"root", p.value("root", "")}, {"dependency", dep}});
    });
    s.on("mcpp-ide/library/updateIndex", [this](const Json& p, const rpc::Context&) -> Result {
        {
            std::lock_guard lock(mutex_);
            searchCache_ = nullptr;
        }
        return job_start(Json{{"kind", "command"}, {"root", any_root(p).generic_string()}, {"args", Json::array({"index", "update"})}});
    });
    s.on("mcpp-ide/toolchains/list", [this](const Json& p, const rpc::Context&) -> Result {
        auto r = query(any_root(p), {"toolchain", "list", "--format", "json"});
        if (!r) return std::unexpected(r.error());
        return views::toolchains(r->envelope);
    });
    s.on("mcpp-ide/toolchains/install", [this](const Json& p, const rpc::Context&) -> Result {
        Json job{{"kind", "toolchain"}, {"root", any_root(p).generic_string()}, {"spec", p.value("spec", "")}};
        if (p.contains("target")) job["target"] = p["target"];
        return job_start(job);
    });
    s.on("mcpp-ide/toolchains/setDefault", [this](const Json& p, const rpc::Context&) -> Result {
        auto spec = p.value("spec", "");
        if (spec.empty()) return std::unexpected(invalid("toolchains/setDefault names a spec"));
        auto r = query(any_root(p), {"toolchain", "default", spec});
        if (!r) return std::unexpected(r.error());
        if (r->exitCode != 0) return std::unexpected(rpc::Error{rpc::REQUEST_FAILED, r->narration});
        return Json{{"spec", spec}};
    });
    s.on("mcpp-ide/targets/list", [this](const Json& p, const rpc::Context&) { return targets(p); });
    s.on("mcpp-ide/cache/inventory", [this](const Json& p, const rpc::Context&) -> Result {
        auto r = query(any_root(p), {"cache", "list", "--format", "json"});
        if (!r) return std::unexpected(r.error());
        views::CacheOptions o;
        const auto topN = setting("mcpp.views.cache.topN");
        o.topN = p.value("topN", topN.is_number() ? topN.get<int>() : 5);
        return views::cache_inventory(r->envelope, o);
    });
    s.on("mcpp-ide/cache/plan", [this](const Json& p, const rpc::Context&) { return cache_plan(p); });
    s.on("mcpp-ide/cache/apply", [this](const Json& p, const rpc::Context&) { return cache_apply(p); });
    s.on("mcpp-ide/graph/packages", [this](const Json& p, const rpc::Context&) { return graph(p, false); });
    s.on("mcpp-ide/graph/modules", [this](const Json& p, const rpc::Context&) { return graph(p, true); });
    s.on("mcpp-ide/pack/formats", [this](const Json& p, const rpc::Context&) -> Result {
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
    s.on("mcpp-ide/pack/start", [this](const Json& p, const rpc::Context&) -> Result {
        Json job = p;
        job["kind"] = "pack";
        return job_start(job);
    });
    s.on("mcpp-ide/manifest/check", [this](const Json& p, const rpc::Context&) { return manifest_check(p); });
}

}  // namespace mcppide::server
