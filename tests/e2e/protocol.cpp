// A session over the mock mcpp, held to the backend's protocol (protocol/protocol.schema.json): each
// answer against its method's `result`, and every notification a build and a test send against its
// `params`. (The schemas themselves are held to mcpp's output and to the backend in cts/contracts.cpp.)
import std;
import mcppide.testsupport;
import mcpplibs.mtest;
import nlohmann.json;
import mcppide.rpc;
import mcppide.server;
import mcppide.schema;

using testsupport::check;
using namespace mcpplibs::mtest;

using namespace mcppide;
using Json = nlohmann::json;

namespace {

Json load(const std::filesystem::path& p) { return Json::parse(testsupport::read(p)); }

bool conforms(const Json& value, const Json& schema, const Json& root, std::string_view what) {
    const auto problems = schema::validate(value, schema, root);
    for (const auto& p : problems) std::println(std::cerr, "{}: {}", what, p);
    return problems.empty();
}

// A session on an in-process wire: requests are dispatched, answers and notifications collected.
struct Wire {
    std::mutex lock;
    std::vector<Json> messages;
    rpc::Server server{[this](std::string_view frame) {
        rpc::FrameReader r;
        r.feed(frame);
        std::lock_guard g(lock);
        while (auto body = r.next()) messages.push_back(Json::parse(*body));
        return true;
    }};
    int next = 1;

    Json call(std::string_view method, Json params) {
        const int id = next++;
        server.dispatch(Json{{"jsonrpc", "2.0"}, {"id", id}, {"method", method}, {"params", std::move(params)}}.dump());
        for (int i = 0; i < 3000; ++i) {
            {
                std::lock_guard g(lock);
                for (const auto& m : messages) if (m.contains("id") && m["id"] == id) return m;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        return Json{{"error", {{"message", "no answer"}}}};
    }
    std::vector<Json> notifications(std::string_view method) {
        std::lock_guard g(lock);
        std::vector<Json> out;
        for (const auto& m : messages) if (m.value("method", "") == method) out.push_back(m["params"]);
        return out;
    }
};

}  // namespace

E2E protocol{"protocol", [] {
    const auto protocol = load(testsupport::repo() / "protocol" / "protocol.schema.json");

    // A session over the mock mcpp.
    const auto mock = testsupport::built("mcpp-ide-mock-mcpp");
    check(!mock.empty());
    const auto root = testsupport::temp() / std::format("mcppide-protocol-{}", std::chrono::steady_clock::now().time_since_epoch().count());
    std::filesystem::create_directories(root / "src");
    std::ofstream(root / "mcpp.toml") << "[package]\nname = \"app\"\nversion = \"0.1.0\"\n";
    Wire wire;
    server::Session session(wire.server);
    session.install();
    auto result_of = [&](std::string_view method, Json params) {
        auto answer = wire.call(method, std::move(params));
        if (!answer.contains("result")) std::println(std::cerr, "{}: {}", method, answer.dump());
        check(answer.contains("result"));
        const auto& def = protocol["methods"][std::string(method)]["result"];
        check(conforms(answer["result"], def, protocol, method));
        return answer["result"];
    };
    auto init = result_of("initialize", Json{{"clientInfo", {{"name", "test"}}}, {"workspaceFolders", Json::array({Json{{"uri", testsupport::file_uri(root)}}})},
                {"initializationOptions", Json{{"editor", "vscode"}, {"settings", {{"mcpp.path", mock.generic_string()}}}}}});
    check(init["projects"].size() == 1);
    check(init["mcpp"].is_object() && init["mcpp"]["contract"] == true);
    check(init["capabilities"]["textDocumentSync"]["change"] == 1 && init["capabilities"]["hoverProvider"] == true);

    // The rules layer as LSP: a manifest opened has its diagnostics published (0-based, UTF-16), and
    // completion, hover and the outline answer for it.
    const auto tomlUri = testsupport::file_uri(root / "mcpp.toml");
    const std::string toml = "[package]\nname = \"app\"\ncolour = 1\n[build]\nbmi_schedule = \"\"\n";
    wire.server.dispatch(Json{{"jsonrpc", "2.0"}, {"method", "textDocument/didOpen"},
                              {"params", {{"textDocument", {{"uri", tomlUri}, {"languageId", "mcpp-toml"}, {"version", 1}, {"text", toml}}}}}}.dump());
    auto published = wire.notifications("textDocument/publishDiagnostics");
    check(!published.empty() && published.back()["uri"] == tomlUri && published.back()["diagnostics"].size() == 1);
    check(published.back()["diagnostics"][0]["range"]["start"] == Json({{"line", 2}, {"character", 0}}));
    check(published.back()["diagnostics"][0]["code"] == "mcpp.manifest.unknownKey");
    auto at = [&](int line, int character) {
        return Json{{"textDocument", {{"uri", tomlUri}}}, {"position", {{"line", line}, {"character", character}}}};
    };
    auto list = result_of("textDocument/completion", at(4, 16));
    std::set<std::string> labels;
    for (const auto& i : list["items"]) labels.insert(i["label"].get<std::string>());
    check(labels.contains("auto") && labels.contains("on") && labels.contains("off"));
    auto hover = result_of("textDocument/hover", at(4, 2));
    check(hover["contents"]["value"].get<std::string>().find("bmi_schedule") != std::string::npos);
    auto outline = result_of("textDocument/documentSymbol", Json{{"textDocument", {{"uri", tomlUri}}}});
    check(outline.size() == 2 && outline[1]["name"] == "[build]" && outline[1]["children"].size() == 1);
    // A change is the whole text; closing clears the document's diagnostics.
    wire.server.dispatch(Json{{"jsonrpc", "2.0"}, {"method", "textDocument/didChange"},
                              {"params", {{"textDocument", {{"uri", tomlUri}, {"version", 2}}}, {"contentChanges", Json::array({{{"text", "[package]\n"}}})}}}}.dump());
    check(wire.notifications("textDocument/publishDiagnostics").back()["diagnostics"].empty());
    wire.server.dispatch(Json{{"jsonrpc", "2.0"}, {"method", "textDocument/didClose"}, {"params", {{"textDocument", {{"uri", tomlUri}}}}}}.dump());
    check(result_of("textDocument/hover", at(0, 2)).is_null());
    const Json rooted{{"root", root.generic_string()}};
    result_of("mcpp-ide/project/summary", rooted);
    auto tests = result_of("mcpp-ide/test/list", rooted);
    check(tests["tests"].size() == 1 && tests["tests"][0]["test"] == "test_smoke");
    auto targets = result_of("mcpp-ide/targets/list", rooted);
    check(!targets["rows"].empty());
    auto cmds = result_of("mcpp-ide/commands/list", Json{{"editor", "jetbrains"}});
    for (const auto& c : cmds["commands"]) check(c["action"]["kind"] != "forward");
    // Neovim's table: no forward to mcppls's commands (mcppls-nvim has its own), no panel it has no
    // text for, its own quickfix command, and each row's subcommand (`:Mcpp <subcommand>`).
    const Json nvimInit{{"clientInfo", {{"name", "Neovim"}}}, {"workspaceFolders", Json::array({Json{{"uri", testsupport::file_uri(root)}}})},
                        {"initializationOptions", Json{{"editor", "nvim"}, {"locale", "en_US.UTF-8"}, {"trusted", true}, {"settings", Json::object()}}}};
    check(conforms(nvimInit, protocol["methods"]["initialize"]["params"], protocol, "initialize params (nvim)"));
    std::map<std::string, std::string> nvim;
    for (const auto& c : result_of("mcpp-ide/commands/list", Json{{"editor", "nvim"}})["commands"]) {
        const auto kind = c["action"]["kind"].get<std::string>();
        check(kind != "forward" && kind != "editor");
        check(!(kind == "panel" && c["action"]["panel"] == "settings"));
        nvim[c["subcommand"].get<std::string>()] = c["id"].get<std::string>();
    }
    check(nvim["cancel"] == "mcpp.cancelJob" && nvim["build"] == "mcpp.build" && nvim["problems"] == "mcpp.problems");
    check(nvim.contains("showMenu") && nvim.contains("library.search") && !nvim.contains("openSettings"));
    for (const auto& c : result_of("mcpp-ide/commands/list", Json{{"editor", "vscode"}})["commands"]) check(c["id"] != "mcpp.problems");
    // A row that leaves Neovim out says why.
    for (const auto& c : load(testsupport::repo() / "spec" / "commands.json")["commands"]) {
        if (!c.contains("editors") || std::ranges::find(c["editors"], Json("nvim")) != c["editors"].end()) continue;
        if (c["action"]["kind"] == "editor") continue;
        const auto reason = c.contains("omitted") ? c["omitted"].value("nvim", "") : std::string{};
        if (reason.empty()) std::println(std::cerr, "{} leaves nvim out without saying why", c["id"].get<std::string>());
        check(!reason.empty());
    }
    result_of("mcpp-ide/menu/quick", rooted);
    result_of("mcpp-ide/languageService/status", Json{{"candidates", Json::array()}});
    // The pack panel's choices: the closures SPEC-014 names, and linkages only where mcpp takes them.
    auto pack = result_of("mcpp-ide/pack/formats", rooted);
    check(pack["modes"].size() == 4);
    const bool helpNamesLinkage = testsupport::read(testsupport::fixtures() / "pack-help.txt").find("--linkage") != std::string::npos;
    check(pack["linkages"].empty() != helpNamesLinkage);
    result_of("mcpp-ide/graph/packages", rooted);
    result_of("mcpp-ide/graph/modules", rooted);
    // A failed database is an error, not an empty graph or a cached answer.
    // The mock flag changes without a manifest edit, so immediate recovery
    // proves the failed response itself was never cached.
    const auto badDatabase = root / "database-error";
    std::filesystem::create_directories(badDatabase);
    std::ofstream(badDatabase / "mcpp.toml") << "[package]\nname = \"app\"\nversion = \"0.1.0\"\n";
    std::ofstream(badDatabase / ".mock-mcpp-fail") << "manifest\n";
    const Json badRoot{{"root", badDatabase.generic_string()}};
    for (const auto* method : {"mcpp-ide/graph/packages", "mcpp-ide/graph/modules"}) {
        const auto failed = wire.call(method, badRoot);
        check(failed.contains("error"));
        check(failed["error"]["code"] == rpc::REQUEST_FAILED);
        check(failed["error"]["data"]["exitCode"] == 1);
        check(!failed["error"]["data"]["diagnostics"].empty());
        check(failed["error"]["message"].get<std::string>().find("emit build-database") != std::string::npos);
    }
    std::filesystem::remove(badDatabase / ".mock-mcpp-fail");
    check(!result_of("mcpp-ide/graph/packages", badRoot)["nodes"].empty());
    check(!result_of("mcpp-ide/graph/modules", badRoot)["nodes"].empty());
    result_of("mcpp-ide/manifest/check", rooted);
    result_of("mcpp-ide/settings/registry", Json::object());
    result_of("mcpp-ide/selfCheck", Json::object());
    result_of("mcpp-ide/cache/inventory", Json::object());
    auto launch = result_of("mcpp-ide/run/launch", rooted);
    check(!launch["argv"].empty());
    result_of("mcpp-ide/debug/configuration", Json{{"root", root.generic_string()}, {"available", Json::array({"lldb"})}});
    // Debug selection follows the configuration of the actual launch, not a
    // separately queried default profile or the first leg of a target set.
    const auto launchFile = root / ".mock-mcpp-launch";
    const auto selectedFile = root / ".mock-mcpp-runner-launch";
    const auto commandLog = root / ".mock-mcpp-log";
    Json ordinary{{"kind", "mcpp.launch"}, {"data", {
        {"program", "app.oke"}, {"argv", Json::array({"okrun", "app.oke"})},
        {"cwd", root.generic_string()}, {"env", Json::object()}, {"via", "runner"}}}};
    auto commands = [&] {
        std::vector<std::string> found;
        std::istringstream input(testsupport::read(commandLog));
        for (std::string line; std::getline(input, line);)
            if (line.starts_with("run --print-launch ") || line.starts_with("test --print-launch ")) found.push_back(line);
        return found;
    };
    auto reset_launch = [&](const Json& first, const Json& second) {
        std::ofstream(launchFile) << first.dump();
        std::ofstream(selectedFile) << second.dump();
        std::ofstream(commandLog).close();
    };
    auto debugParams = rooted;
    debugParams["engine"] = "lldb";
    debugParams["args"] = Json::array({"with spaces", ""});
    // Older cores omit configuration; board actions called `debug` are not
    // host-debugger launch declarations and are never guessed as one.
    for (bool withBoardRunner : {false, true}) {
        auto first = ordinary;
        if (withBoardRunner) first["data"]["configuration"] = {
            {"triple", "x86_64-openkal"}, {"profile", "inspect"}, {"runners", Json::array({"debug"})}};
        reset_launch(first, first);
        auto debug = result_of("mcpp-ide/debug/configuration", debugParams);
        check(commands().size() == 1);
        check(debug["launch"]["argv"] == Json::array({"okrun", "app.oke", "with spaces", ""}));
    }
    ordinary["data"]["configuration"] = {{"triple", "x86_64-openkal"}, {"profile", "inspect"},
        {"platform", nullptr}, {"runners", Json::array({"debug", "debuggee"})}};
    auto selected = ordinary;
    selected["data"]["argv"] = Json::array({"okrun", "--debugger", "app.oke"});
    for (bool test : {false, true}) {
        reset_launch(ordinary, selected);
        auto params = debugParams;
        params["target"] = "all";
        if (test) params["test"] = "unit/math";
        auto debug = result_of("mcpp-ide/debug/configuration", params);
        auto calls = commands();
        check(calls.size() == 2);
        check(calls[0].find("--target all ") != std::string::npos);
        check(calls[1].find("--target x86_64-openkal ") != std::string::npos);
        check(calls[1].find("--profile inspect ") != std::string::npos);
        check(calls[1].find("--runner debuggee ") != std::string::npos);
        check(calls[1].starts_with(test ? "test " : "run "));
        if (test) check(calls[1].find("unit/math ") != std::string::npos);
        check(debug["launch"]["argv"] == Json::array({"okrun", "--debugger", "app.oke", "with spaces", ""}));
        check(debug["configuration"]["initCommands"] == Json::array({"settings set plugin.jit-loader.gdb.enable on"}));
    }
    // Explicit runner choices, including an explicit default, are preserved.
    for (const auto* runner : {"", "monitor"}) {
        reset_launch(ordinary, selected);
        auto params = debugParams;
        params["runner"] = runner;
        result_of("mcpp-ide/debug/configuration", params);
        check(commands().size() == 1);
    }
    auto missing = ordinary;
    missing["data"]["configuration"].erase("profile");
    reset_launch(missing, selected);
    check(wire.call("mcpp-ide/debug/configuration", debugParams).contains("error"));
    check(commands().size() == 1);
    for (const auto* field : {"triple", "profile", "platform"}) {
        auto changed = selected;
        changed["data"]["configuration"][field] = "different";
        reset_launch(ordinary, changed);
        check(wire.call("mcpp-ide/debug/configuration", debugParams).contains("error"));
        check(commands().size() == 2);
    }
    reset_launch(ordinary, selected);
    auto custom = rooted;
    custom["profile"] = "inspect";
    result_of("mcpp-ide/run/launch", custom);
    check(commands().size() == 1 && commands()[0].find("--profile inspect ") != std::string::npos);
    for (const auto& path : {launchFile, selectedFile, commandLog}) std::filesystem::remove(path);
    // What mcpp-vscode's own parsers read: one command's output as it printed it, and a project's
    // files with its source count.
    auto exec = result_of("mcpp-ide/mcpp/exec", Json{{"args", Json::array({"--version"})}, {"cwd", root.generic_string()}});
    auto availability = result_of("mcpp-ide/mcpp/availability", Json::object());
    check(availability["available"] == true && availability["missing"] == false);
    check(exec["exitCode"] == 0 && exec["stdout"].get<std::string>().find("mcpp") != std::string::npos);
    check(exec["timedOut"] == false && exec["truncated"] == false);
    auto bounded = result_of("mcpp-ide/mcpp/exec", Json{{"args", Json::array({"--version"})}, {"maxOutputBytes", 3}});
    check(bounded["truncated"] == true && bounded["stdout"].get<std::string>().size() <= 3);
    check(wire.call("mcpp-ide/mcpp/exec", Json{{"args", "--version"}}).contains("error"));
    // mcppls's own commands run as the command the status found (here a program that answers --version).
    result_of("mcpp-ide/languageService/status", Json{{"candidates", Json::array({mock.generic_string()})}});
    auto ls = result_of("mcpp-ide/languageService/exec", Json{{"args", Json::array({"--version"})}});
    check(ls["exitCode"] == 0 && !ls["stdout"].get<std::string>().empty());
    std::ofstream(root / "src" / "main.cpp") << "int main() {}\n";
    std::ofstream(root / "src" / "notes.txt") << "not a source\n";
    std::filesystem::create_directories(root / "target" / "obj");
    std::ofstream(root / "target" / "obj" / "gen.cpp") << "// build output\n";
    auto files = result_of("mcpp-ide/project/files", rooted);
    check(files["manifest"].get<std::string>().starts_with("[package]") && files["lock"].is_null());
    check(files["sourceFiles"] == 1);

    result_of("mcpp-ide/job/start", Json{{"kind", "build"}, {"root", root.generic_string()}});
    result_of("mcpp-ide/job/start", Json{{"kind", "test"}, {"root", root.generic_string()}});
    for (int i = 0; i < 2000 && wire.notifications("mcpp-ide/job/finished").size() < 2; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    check(wire.notifications("mcpp-ide/job/finished").size() == 2);
    for (const auto& name : server::kNotifications) {
        const auto& def = protocol["notifications"][std::string(name)]["params"];
        for (const auto& n : wire.notifications(name)) check(conforms(n, def, protocol, name));
    }
    check(!wire.notifications("mcpp-ide/job/plan").empty());
    check(!wire.notifications("mcpp-ide/job/artifact").empty());
    const auto results = wire.notifications("mcpp-ide/test/result");
    check(results.size() == 1 && results[0]["status"] == "passed");
    check(wire.notifications("mcpp-ide/test/summary").size() == 1);
    // The plan a build sent is the project's configuration from then on.
    auto after = result_of("mcpp-ide/project/summary", rooted);
    check(after.contains("configuration"));

    // A launch job (design 2026-11-07 §4.9): mcpp's narration while it builds, and the launch in
    // job/finished, the program's arguments after `--`. With mcpp's launch-stream, the build's progress too.
    const auto streamed = root.parent_path() / (root.filename().string() + "-stream");
    std::filesystem::create_directories(streamed);
    std::ofstream(streamed / "mcpp.toml") << "[package]\nname = \"app\"\nversion = \"0.1.0\"\n";
    std::ofstream(streamed / ".mock-mcpp-features") << "launch-stream";
    const auto finishedBefore = wire.notifications("mcpp-ide/job/finished").size();
    const auto progressBefore = wire.notifications("mcpp-ide/job/progress").size();
    const auto plain = result_of("mcpp-ide/job/start", Json{{"kind", "launch"}, {"root", root.generic_string()}, {"args", Json::array({"one"})}});
    const auto live = result_of("mcpp-ide/job/start", Json{{"kind", "launch"}, {"root", streamed.generic_string()}, {"args", Json::array({"two"})}});
    for (int i = 0; i < 2000 && wire.notifications("mcpp-ide/job/finished").size() < finishedBefore + 2; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    const auto ends = wire.notifications("mcpp-ide/job/finished");
    check(ends.size() == finishedBefore + 2);
    for (const auto& id : {plain["jobId"], live["jobId"]}) {
        const auto end = std::ranges::find_if(ends, [&](const Json& n) { return n["jobId"] == id; });
        check(end != ends.end() && (*end)["status"] == "ok" && (*end)["kind"] == "launch");
        if (end != ends.end()) check((*end).contains("launch") && !(*end)["launch"]["argv"].empty());
        const auto outputs = wire.notifications("mcpp-ide/job/output");
        check(std::ranges::any_of(outputs, [&](const Json& n) {
            return n["jobId"] == id && n["text"].get<std::string>().find("Finished") != std::string::npos; }));
    }
    const auto progress = wire.notifications("mcpp-ide/job/progress");
    check(progress.size() > progressBefore);
    check(std::ranges::all_of(progress.begin() + static_cast<std::ptrdiff_t>(progressBefore), progress.end(),
                              [&](const Json& n) { return n["jobId"] == live["jobId"]; }));
    for (const auto& name : {"mcpp-ide/job/finished", "mcpp-ide/job/progress", "mcpp-ide/job/output"}) {
        const auto& def = protocol["notifications"][name]["params"];
        for (const auto& n : wire.notifications(name)) check(conforms(n, def, protocol, name));
    }
    // A program that implements the test protocol (mcpp's `test-protocol`): its cases listed by
    // `mcpp test --cases`, a selection run by `--case`, and each case's result from the record.
    const auto protocolRoot = root.parent_path() / (root.filename().string() + "-cases");
    std::filesystem::create_directories(protocolRoot / "tests" / "unit");
    std::ofstream(protocolRoot / "mcpp.toml") << "[package]\nname = \"app\"\nversion = \"0.1.0\"\n";
    std::ofstream(protocolRoot / ".mock-mcpp-features") << "test-protocol";
    std::ofstream(protocolRoot / "tests" / "unit" / "parse.cpp")
        << "import mcpplibs.mtest;\nusing namespace mcpplibs::mtest;\nsuite parsing{\"parsing\", [] {\n"
           "    \"an empty line\"_test = [] {};\n    \"a field\"_test = [] {};\n}};\n";
    const auto listing = result_of("mcpp-ide/test/cases", Json{{"root", protocolRoot.generic_string()}, {"test", "unit/parse"}, {"list", true}});
    check(listing["protocol"] == 1 && listing.value("listed", false) && listing["framework"].is_string());
    check(listing["cases"].size() == 2 && listing["cases"][0]["name"] == "parsing/an empty line" && listing["cases"][0]["line"] == 3);
    const auto caseResultsBefore = wire.notifications("mcpp-ide/test/caseResult").size();
    const auto run = result_of("mcpp-ide/test/runCases", Json{{"root", protocolRoot.generic_string()}, {"test", "unit/parse"},
                                                             {"cases", Json::array({"parsing/an empty line", "parsing/a field"})}});
    check(run.contains("jobId"));
    for (int i = 0; i < 2000 && wire.notifications("mcpp-ide/test/caseResult").size() < caseResultsBefore + 2; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    const auto caseResults = wire.notifications("mcpp-ide/test/caseResult");
    check(caseResults.size() == caseResultsBefore + 2);
    if (caseResults.size() == caseResultsBefore + 2) {
        check(caseResults[caseResultsBefore]["case"] == "parsing/an empty line" && caseResults[caseResultsBefore]["status"] == "passed");
        const auto& failed = caseResults[caseResultsBefore + 1];
        check(failed["status"] == "failed" && failed["location"]["line"] == 6
              && failed["message"].get<std::string>().contains("expected 2, actual 1"));
        for (const auto& n : caseResults)
            check(conforms(n, protocol["notifications"]["mcpp-ide/test/caseResult"]["params"], protocol, "mcpp-ide/test/caseResult"));
    }
    // A program of several files (design 2026-11-10): as mcpp 2026.11.9.1 states it (its parts
    // derived), and as mcpp#917 states it (parts, case records, metrics). A run's part records are
    // attributed to their program, each part's cases reported, and the store's runs read back.
    for (const std::string set : {"directory", "directory-917"}) {
        const bool rich = set == "directory-917";
        const auto dir = root.parent_path() / (root.filename().string() + "-" + set);
        std::filesystem::create_directories(dir / "tests" / "unit");
        std::ofstream(dir / "mcpp.toml") << "[package]\nname = \"app\"\nversion = \"0.1.0\"\n\n[test]\nprograms = \"directory\"\n";
        std::ofstream(dir / ".mock-mcpp-tests") << set;
        std::ofstream(dir / ".mock-mcpp-features") << (rich ? "test-protocol,test-parts,test-case-stream,test-metrics" : "test-protocol");
        std::ofstream(dir / "tests" / "smoke.cpp") << "int main() { return 0; }\n";
        std::ofstream(dir / "tests" / "unit" / "a.cpp")
            << "import mcpplibs.mtest;\nusing namespace mcpplibs::mtest;\n\nBench alpha{\"alpha\", at_most{\"ready\", 15}, [] {}};\n";
        std::ofstream(dir / "tests" / "unit" / "b.cpp") << "import mcpplibs.mtest;\nusing namespace mcpplibs::mtest;\n\nUTest beta{\"beta\", [] {}};\n";
        const auto a = (dir / "tests" / "unit" / "a.cpp").generic_string(), b = (dir / "tests" / "unit" / "b.cpp").generic_string();
        // mcpp names a file as the file system spells the project (Windows' long names), the test as
        // its temporary directory does (it may be a short name): one file, either spelling.
        const auto same = [](const Json& named, const std::string& file) {
            std::error_code ec;
            return named.is_string() && std::filesystem::equivalent(named.get<std::string>(), file, ec);
        };
        const auto listed = result_of("mcpp-ide/test/list", Json{{"root", dir.generic_string()}});
        const auto unit = std::ranges::find_if(listed["tests"], [](const Json& t) { return t["test"] == "unit"; });
        check(unit != listed["tests"].end());
        if (unit != listed["tests"].end()) {
            check((*unit)["layout"] == "directory" && (*unit)["main"] == "" && (*unit)["files"].size() == 2
                  && same((*unit)["files"][0], a) && same((*unit)["files"][1], b));
            check((*unit)["parts"].size() == 2 && (*unit)["parts"][1]["name"] == "unit/b" && same((*unit)["parts"][1]["file"], b));
        }
        const auto entries = result_of("mcpp-ide/source/entries", Json{{"file", b}});
        check(entries["kind"] == "test" && entries["test"] == "unit" && entries["part"] == "unit/b");
        // Before a listing, the program's files and no case; the listing places each case in its file.
        const auto statics = result_of("mcpp-ide/test/cases", Json{{"root", dir.generic_string()}, {"test", "unit"}});
        check(statics["test"] == "unit" && statics["files"].size() == 2 && same(statics["files"][0], a) && same(statics["files"][1], b));
        const auto listedCases = result_of("mcpp-ide/test/cases", Json{{"root", dir.generic_string()}, {"test", "unit"}, {"list", true}});
        check(listedCases.value("listed", false) && listedCases["protocol"] == 1 && listedCases["cases"].size() == 2);
        if (listedCases["cases"].size() == 2) {
            const auto& alpha = listedCases["cases"][0];
            check(alpha["name"] == "alpha" && same(alpha["file"], a) && alpha["line"] == 3 && alpha["kind"] == "bench" && alpha["bounds"][0]["max"] == 15);
            check(listedCases["cases"][1]["tags"] == Json::array({"slow"}) && same(listedCases["cases"][1]["file"], b));
        }
        const auto resultsBefore = wire.notifications("mcpp-ide/test/result").size();
        const auto eventsBefore = wire.notifications("mcpp-ide/test/caseEvent").size();
        const auto finishedBefore = wire.notifications("mcpp-ide/job/finished").size();
        result_of("mcpp-ide/job/start", Json{{"kind", "test"}, {"root", dir.generic_string()}});
        for (int i = 0; i < 2000 && wire.notifications("mcpp-ide/job/finished").size() <= finishedBefore; ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        const auto results = wire.notifications("mcpp-ide/test/result");
        check(results.size() == resultsBefore + 3);
        if (results.size() == resultsBefore + 3) {
            const auto& partB = results[resultsBefore + 1];
            check(results[resultsBefore]["program"] == "unit" && results[resultsBefore]["part"] == "unit/a");
            check(partB["program"] == "unit" && partB["part"] == "unit/b" && partB["cases"].size() == 1);
            check(partB["cases"][0]["status"] == "failed" && partB["cases"][0]["failures"][0]["expected"] == "4"
                  && partB["cases"][0]["failures"][0]["actual"] == "5" && partB["cases"][0]["location"]["line"] == 3);
            check(results[resultsBefore + 2]["program"] == "smoke" && !results[resultsBefore + 2].contains("part"));
            if (rich) check(results[resultsBefore]["cases"][0]["metrics"][0]["value"] == 4.2);
        }
        const auto events = wire.notifications("mcpp-ide/test/caseEvent");
        check(events.size() == eventsBefore + (rich ? 4 : 0));
        if (rich && events.size() == eventsBefore + 4) {
            check(events[eventsBefore]["event"] == "begin" && events[eventsBefore]["program"] == "unit" && events[eventsBefore]["case"] == "alpha");
            check(events[eventsBefore + 1]["event"] == "end" && events[eventsBefore + 1]["status"] == "passed"
                  && events[eventsBefore + 1]["metrics"][0]["name"] == "ready");
            check(events[eventsBefore + 3]["status"] == "failed" && events[eventsBefore + 3]["part"] == "unit/b");
        }
        // Cases asked for through a part: each result names its program and its own part, as the
        // record states them, not the job's filter.
        const auto caseResultsBefore = wire.notifications("mcpp-ide/test/caseResult").size();
        const auto finishedCases = wire.notifications("mcpp-ide/job/finished").size();
        result_of("mcpp-ide/test/runCases", Json{{"root", dir.generic_string()}, {"test", "unit/a"},
                                                 {"cases", Json::array({"alpha"})}});
        for (int i = 0; i < 2000 && wire.notifications("mcpp-ide/job/finished").size() <= finishedCases; ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        const auto caseResults = wire.notifications("mcpp-ide/test/caseResult");
        check(caseResults.size() == caseResultsBefore + 2);
        if (caseResults.size() == caseResultsBefore + 2) {
            const auto& alpha = caseResults[caseResultsBefore];
            const auto& beta = caseResults[caseResultsBefore + 1];
            check(alpha["test"] == "unit/a" && alpha["program"] == "unit" && alpha["part"] == "unit/a" && alpha["case"] == "alpha");
            check(beta["program"] == "unit" && beta["part"] == "unit/b" && beta["status"] == "failed");
        }
        for (const auto* name : {"mcpp-ide/test/result", "mcpp-ide/test/caseEvent", "mcpp-ide/test/caseResult"})
            for (const auto& n : wire.notifications(name)) check(conforms(n, protocol["notifications"][name]["params"], protocol, name));
        // The store mcpp keeps (target/mtest): three runs, the last with mcpp#917's fields.
        const auto noRuns = result_of("mcpp-ide/test/insights", Json{{"root", dir.generic_string()}});
        check(noRuns["run"].is_null() && noRuns["runs"] == 0 && noRuns["features"]["live"] == rich);
        std::filesystem::create_directories(dir / "target" / "mtest");
        std::filesystem::copy(testsupport::repo() / "protocol" / "fixtures" / "mtest-store", dir / "target" / "mtest",
                              std::filesystem::copy_options::recursive);
        const auto insights = result_of("mcpp-ide/test/insights", Json{{"root", dir.generic_string()}});
        check(insights["runs"] == 3 && insights["run"]["id"] == "20261110T100000-0c03");
        check(insights["failures"].size() == 1 && insights["failures"][0]["case"] == "beta" && insights["failures"][0]["program"] == "unit"
              && insights["failures"][0]["part"] == "unit/b" && insights["failures"][0]["new"] == true && insights["failures"][0]["expected"] == "4");
        check(insights["flaky"].size() == 1 && insights["flaky"][0]["flips"] == 2);
        check(insights["metrics"].size() == 1 && insights["metrics"][0]["last"] == 4.2 && insights["metrics"][0]["bound"]["max"] == 15);
        check(insights["schedule"]["lanes"].size() == 2 && insights["schedule"]["criticalPath"] == Json::array({"unit", "smoke"}));
        check(insights["available"]["metrics"] == true && insights["available"]["kinds"] == true);
        const auto history = result_of("mcpp-ide/test/history", Json{{"root", dir.generic_string()}, {"program", "unit"}, {"case", "beta"}});
        check(history["runs"].size() == 3 && history["cases"].size() == 1 && history["cases"][0]["results"].size() == 3);
        if (history["cases"].size() == 1) check(history["cases"][0]["results"][1]["status"] == "pass");
        std::filesystem::remove_all(dir);
    }
    std::filesystem::remove_all(protocolRoot);
    std::filesystem::remove_all(streamed);
    std::filesystem::remove_all(root);
}};
