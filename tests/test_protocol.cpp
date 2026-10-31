// The two contracts, held to their schemas:
//   - every golden fixture (mcpp's real output) against the definition of
//     protocol/mcpp-contract.schema.json that protocol/fixtures.json names for it;
//   - the methods the backend registers and the notifications it sends against
//     protocol/protocol.schema.json, both ways;
//   - a session over the mock mcpp: each answer against its method's `result`, and
//     every notification a build and a test send against its `params`.
#include "support.hpp"
import std;
import nlohmann.json;
import mcppide.rpc;
import mcppide.server;
import mcppide.schema;

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

int main() {
    const auto contract = load(testsupport::repo() / "protocol" / "mcpp-contract.schema.json");
    const auto index = load(testsupport::repo() / "protocol" / "fixtures.json");
    for (const auto& f : index["fixtures"]) {
        const auto path = testsupport::fixtures() / f["file"].get<std::string>();
        if (!std::filesystem::exists(path)) {
            CHECK(f.value("optional", false));
            continue;
        }
        const auto text = testsupport::read(path);
        if (f.contains("each")) {
            const Json def{{"$ref", "#/$defs/" + f["each"].get<std::string>()}};
            std::istringstream lines(text);
            int n = 0;
            for (std::string line; std::getline(lines, line);) {
                if (line.empty()) continue;
                CHECK(conforms(Json::parse(line), def, contract, std::format("{}:{}", f["file"].get<std::string>(), ++n)));
            }
            CHECK(n > 0);
        } else {
            const Json def{{"$ref", "#/$defs/" + f["schema"].get<std::string>()}};
            CHECK(conforms(Json::parse(text), def, contract, f["file"].get<std::string>()));
        }
    }

    const auto protocol = load(testsupport::repo() / "protocol" / "protocol.schema.json");
    std::set<std::string> methods, notifications;
    for (auto it = protocol["methods"].begin(); it != protocol["methods"].end(); ++it) methods.insert(it.key());
    for (auto it = protocol["notifications"].begin(); it != protocol["notifications"].end(); ++it) notifications.insert(it.key());
    std::set<std::string> registered(server::kMethods.begin(), server::kMethods.end());
    std::set<std::string> sent(server::kNotifications.begin(), server::kNotifications.end());
    for (const auto& m : methods) if (!registered.contains(m)) std::println(std::cerr, "schema method not registered: {}", m);
    for (const auto& m : registered) if (!methods.contains(m)) std::println(std::cerr, "registered method not in the schema: {}", m);
    CHECK(methods == registered);
    CHECK(notifications == sent);

    // A session over the mock mcpp.
    const auto mock = testsupport::built("mcpp-ide-mock-mcpp");
    CHECK(!mock.empty());
    const auto root = std::filesystem::temp_directory_path() / std::format("mcppide-protocol-{}", std::chrono::steady_clock::now().time_since_epoch().count());
    std::filesystem::create_directories(root / "src");
    std::ofstream(root / "mcpp.toml") << "[package]\nname = \"app\"\nversion = \"0.1.0\"\n";
    Wire wire;
    server::Session session(wire.server);
    session.install();
    auto result_of = [&](std::string_view method, Json params) {
        auto answer = wire.call(method, std::move(params));
        if (!answer.contains("result")) std::println(std::cerr, "{}: {}", method, answer.dump());
        CHECK(answer.contains("result"));
        const auto& def = protocol["methods"][std::string(method)]["result"];
        CHECK(conforms(answer["result"], def, protocol, method));
        return answer["result"];
    };
    auto init = result_of("initialize", Json{{"clientInfo", {{"name", "test"}}}, {"workspaceFolders", Json::array({Json{{"uri", testsupport::file_uri(root)}}})},
                {"initializationOptions", Json{{"editor", "vscode"}, {"settings", {{"mcpp.path", mock.generic_string()}}}}}});
    CHECK(init["projects"].size() == 1);
    CHECK(init["mcpp"].is_object() && init["mcpp"]["contract"] == true);
    CHECK(init["capabilities"]["textDocumentSync"]["change"] == 1 && init["capabilities"]["hoverProvider"] == true);

    // The rules layer as LSP: a manifest opened has its diagnostics published (0-based, UTF-16), and
    // completion, hover and the outline answer for it.
    const auto tomlUri = testsupport::file_uri(root / "mcpp.toml");
    const std::string toml = "[package]\nname = \"app\"\ncolour = 1\n[build]\nbmi_schedule = \"\"\n";
    wire.server.dispatch(Json{{"jsonrpc", "2.0"}, {"method", "textDocument/didOpen"},
                              {"params", {{"textDocument", {{"uri", tomlUri}, {"languageId", "mcpp-toml"}, {"version", 1}, {"text", toml}}}}}}.dump());
    auto published = wire.notifications("textDocument/publishDiagnostics");
    CHECK(!published.empty() && published.back()["uri"] == tomlUri && published.back()["diagnostics"].size() == 1);
    CHECK(published.back()["diagnostics"][0]["range"]["start"] == Json({{"line", 2}, {"character", 0}}));
    CHECK(published.back()["diagnostics"][0]["code"] == "mcpp.manifest.unknownKey");
    auto at = [&](int line, int character) {
        return Json{{"textDocument", {{"uri", tomlUri}}}, {"position", {{"line", line}, {"character", character}}}};
    };
    auto list = result_of("textDocument/completion", at(4, 16));
    std::set<std::string> labels;
    for (const auto& i : list["items"]) labels.insert(i["label"].get<std::string>());
    CHECK(labels.contains("auto") && labels.contains("on") && labels.contains("off"));
    auto hover = result_of("textDocument/hover", at(4, 2));
    CHECK(hover["contents"]["value"].get<std::string>().find("bmi_schedule") != std::string::npos);
    auto outline = result_of("textDocument/documentSymbol", Json{{"textDocument", {{"uri", tomlUri}}}});
    CHECK(outline.size() == 2 && outline[1]["name"] == "[build]" && outline[1]["children"].size() == 1);
    // A change is the whole text; closing clears the document's diagnostics.
    wire.server.dispatch(Json{{"jsonrpc", "2.0"}, {"method", "textDocument/didChange"},
                              {"params", {{"textDocument", {{"uri", tomlUri}, {"version", 2}}}, {"contentChanges", Json::array({{{"text", "[package]\n"}}})}}}}.dump());
    CHECK(wire.notifications("textDocument/publishDiagnostics").back()["diagnostics"].empty());
    wire.server.dispatch(Json{{"jsonrpc", "2.0"}, {"method", "textDocument/didClose"}, {"params", {{"textDocument", {{"uri", tomlUri}}}}}}.dump());
    CHECK(result_of("textDocument/hover", at(0, 2)).is_null());
    const Json rooted{{"root", root.generic_string()}};
    result_of("mcpp-ide/project/summary", rooted);
    auto tests = result_of("mcpp-ide/test/list", rooted);
    CHECK(tests["tests"].size() == 1 && tests["tests"][0]["test"] == "test_smoke");
    auto targets = result_of("mcpp-ide/targets/list", rooted);
    CHECK(!targets["rows"].empty());
    auto cmds = result_of("mcpp-ide/commands/list", Json{{"editor", "jetbrains"}});
    for (const auto& c : cmds["commands"]) CHECK(c["action"]["kind"] != "forward");
    // Neovim's table: no forward to mcppls's commands (mcppls-nvim has its own), no panel it has no
    // text for, its own quickfix command, and each row's subcommand (`:Mcpp <subcommand>`).
    const Json nvimInit{{"clientInfo", {{"name", "Neovim"}}}, {"workspaceFolders", Json::array({Json{{"uri", testsupport::file_uri(root)}}})},
                        {"initializationOptions", Json{{"editor", "nvim"}, {"locale", "en_US.UTF-8"}, {"trusted", true}, {"settings", Json::object()}}}};
    CHECK(conforms(nvimInit, protocol["methods"]["initialize"]["params"], protocol, "initialize params (nvim)"));
    std::map<std::string, std::string> nvim;
    for (const auto& c : result_of("mcpp-ide/commands/list", Json{{"editor", "nvim"}})["commands"]) {
        const auto kind = c["action"]["kind"].get<std::string>();
        CHECK(kind != "forward" && kind != "editor");
        CHECK(!(kind == "panel" && c["action"]["panel"] == "settings"));
        nvim[c["subcommand"].get<std::string>()] = c["id"].get<std::string>();
    }
    CHECK(nvim["cancel"] == "mcpp.cancelJob" && nvim["build"] == "mcpp.build" && nvim["problems"] == "mcpp.problems");
    CHECK(nvim.contains("showMenu") && nvim.contains("library.search") && !nvim.contains("openSettings"));
    for (const auto& c : result_of("mcpp-ide/commands/list", Json{{"editor", "vscode"}})["commands"]) CHECK(c["id"] != "mcpp.problems");
    // A row that leaves Neovim out says why.
    for (const auto& c : load(testsupport::repo() / "spec" / "commands.json")["commands"]) {
        if (!c.contains("editors") || std::ranges::find(c["editors"], Json("nvim")) != c["editors"].end()) continue;
        if (c["action"]["kind"] == "editor") continue;
        const auto reason = c.contains("omitted") ? c["omitted"].value("nvim", "") : std::string{};
        if (reason.empty()) std::println(std::cerr, "{} leaves nvim out without saying why", c["id"].get<std::string>());
        CHECK(!reason.empty());
    }
    result_of("mcpp-ide/menu/quick", rooted);
    result_of("mcpp-ide/languageService/status", Json{{"candidates", Json::array()}});
    // The pack panel's choices: the closures SPEC-014 names, and linkages only where mcpp takes them.
    auto pack = result_of("mcpp-ide/pack/formats", rooted);
    CHECK(pack["modes"].size() == 4);
    const bool helpNamesLinkage = testsupport::read(testsupport::fixtures() / "pack-help.txt").find("--linkage") != std::string::npos;
    CHECK(pack["linkages"].empty() != helpNamesLinkage);
    result_of("mcpp-ide/graph/packages", rooted);
    result_of("mcpp-ide/graph/modules", rooted);
    result_of("mcpp-ide/manifest/check", rooted);
    result_of("mcpp-ide/settings/registry", Json::object());
    result_of("mcpp-ide/selfCheck", Json::object());
    result_of("mcpp-ide/cache/inventory", Json::object());
    auto launch = result_of("mcpp-ide/run/launch", rooted);
    CHECK(!launch["argv"].empty());
    result_of("mcpp-ide/debug/configuration", Json{{"root", root.generic_string()}, {"available", Json::array({"lldb"})}});
    // What mcpp-vscode's own parsers read: one command's output as it printed it, and a project's
    // files with its source count.
    auto exec = result_of("mcpp-ide/mcpp/exec", Json{{"args", Json::array({"--version"})}, {"cwd", root.generic_string()}});
    CHECK(exec["exitCode"] == 0 && exec["stdout"].get<std::string>().find("mcpp") != std::string::npos);
    CHECK(exec["timedOut"] == false && exec["truncated"] == false);
    auto bounded = result_of("mcpp-ide/mcpp/exec", Json{{"args", Json::array({"--version"})}, {"maxOutputBytes", 3}});
    CHECK(bounded["truncated"] == true && bounded["stdout"].get<std::string>().size() <= 3);
    CHECK(wire.call("mcpp-ide/mcpp/exec", Json{{"args", "--version"}}).contains("error"));
    // mcppls's own commands run as the command the status found (here a program that answers --version).
    result_of("mcpp-ide/languageService/status", Json{{"candidates", Json::array({mock.generic_string()})}});
    auto ls = result_of("mcpp-ide/languageService/exec", Json{{"args", Json::array({"--version"})}});
    CHECK(ls["exitCode"] == 0 && !ls["stdout"].get<std::string>().empty());
    std::ofstream(root / "src" / "main.cpp") << "int main() {}\n";
    std::ofstream(root / "src" / "notes.txt") << "not a source\n";
    std::filesystem::create_directories(root / "target" / "obj");
    std::ofstream(root / "target" / "obj" / "gen.cpp") << "// build output\n";
    auto files = result_of("mcpp-ide/project/files", rooted);
    CHECK(files["manifest"].get<std::string>().starts_with("[package]") && files["lock"].is_null());
    CHECK(files["sourceFiles"] == 1);

    result_of("mcpp-ide/job/start", Json{{"kind", "build"}, {"root", root.generic_string()}});
    result_of("mcpp-ide/job/start", Json{{"kind", "test"}, {"root", root.generic_string()}});
    for (int i = 0; i < 2000 && wire.notifications("mcpp-ide/job/finished").size() < 2; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    CHECK(wire.notifications("mcpp-ide/job/finished").size() == 2);
    for (const auto& name : server::kNotifications) {
        const auto& def = protocol["notifications"][std::string(name)]["params"];
        for (const auto& n : wire.notifications(name)) CHECK(conforms(n, def, protocol, name));
    }
    CHECK(!wire.notifications("mcpp-ide/job/plan").empty());
    CHECK(!wire.notifications("mcpp-ide/job/artifact").empty());
    const auto results = wire.notifications("mcpp-ide/test/result");
    CHECK(results.size() == 1 && results[0]["status"] == "passed");
    CHECK(wire.notifications("mcpp-ide/test/summary").size() == 1);
    // The plan a build sent is the project's configuration from then on.
    auto after = result_of("mcpp-ide/project/summary", rooted);
    CHECK(after.contains("configuration"));
    std::filesystem::remove_all(root);
    std::println("test_protocol: ok");
}
