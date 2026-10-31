// What mcppls says about a workspace (design 2026-10-30 part 2 §5): `mcppls status` from 0.0.20 on,
// the frozen table for 0.0.18 and 0.0.19; mcppls's actions offered in each editor under the id
// mcppls gives that editor, and none where it gives none; a change of state told once. The mock
// mcppls sits in a payload made here, as an editor's own payload would.
#include "support.hpp"
import std;
import nlohmann.json;
import mcppide.rpc;
import mcppide.server;
import mcppide.languageservice;

using namespace mcppide;
using Json = nlohmann::json;
namespace ls = mcppide::languageservice;

namespace {

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
    int count(std::string_view method) {
        std::lock_guard g(lock);
        return static_cast<int>(std::ranges::count_if(messages, [&](const Json& m) { return m.value("method", "") == method; }));
    }
};

Json command(const Json& list, std::string_view id) {
    for (const auto& c : list["commands"]) if (c.value("id", "") == id) return c;
    return Json{};
}

}  // namespace

int main() {
    // The document, read: what `status` printed, and the frozen table when it printed nothing usable.
    {
        const auto r = ls::parse_status(R"({"schema": 1, "capabilities": ["status", 7], "workspace": {"state": "ready"},
                                            "actions": [{"id": "restart", "vscode": "mcppls.restartServer"}, {"title": "no id"}]})");
        CHECK(r.source == "status");
        CHECK(r.capabilities == Json::array({"status"}));
        CHECK(r.actions.size() == 1 && r.workspace["state"] == "ready");
        const auto broken = ls::parse_status("mcppls: unknown command status");
        CHECK(broken.source == "frozen" && !broken.detail.empty());
        CHECK(broken.actions == ls::frozen_actions());
    }

    const auto mock = testsupport::built("mcpp-ide-mock-mcppls");
    CHECK(!mock.empty());
    const auto base = std::filesystem::temp_directory_path() / std::format("mcppide-mcppls-{}", std::chrono::steady_clock::now().time_since_epoch().count());
    const auto payload = base / "payload";
    const auto root = base / "project";
    std::filesystem::create_directories(payload / "bin");
    std::filesystem::create_directories(root);
    std::filesystem::copy_file(mock, payload / "bin" / mock.filename());
    std::ofstream(payload / "payload.json") << std::format(R"({{"server": {{"path": "bin/{}"}}}})", mock.filename().string());
    std::ofstream(root / "mcpp.toml") << "[package]\nname = \"p\"\nversion = \"0.1.0\"\n";

    Wire wire;
    server::Session session(wire.server);
    session.install();
    auto result = [&](std::string_view method, Json params) {
        auto answer = wire.call(method, std::move(params));
        if (!answer.contains("result")) std::println(std::cerr, "{}: {}", method, answer.dump());
        CHECK(answer.contains("result"));
        return answer["result"];
    };
    const Json candidates = Json::array({payload.generic_string()});
    result("initialize", Json{{"clientInfo", {{"name", "test"}}}, {"workspaceFolders", Json::array({Json{{"uri", testsupport::file_uri(root)}}})},
                {"initializationOptions", Json{{"editor", "jetbrains"}, {"trusted", true},
                              {"languageService", {{"candidates", candidates}}}}}});

    // 0.0.19: no `status`; the actions are the frozen table's, and the workspace is not known.
    std::ofstream(payload / "mock-version") << "0.0.19\n";
    auto old = result("mcpp-ide/languageService/status", Json{{"candidates", candidates}, {"root", root.generic_string()}});
    CHECK(old["state"] == "ok" && old["version"] == "0.0.19");
    CHECK(old["statusSource"] == "frozen" && old["workspace"].is_null());
    CHECK(old["serverActions"] == ls::frozen_actions());

    // 0.0.20: the status document.
    std::ofstream(payload / "mock-version") << "0.0.20\n";
    std::ofstream(root / ".mock-mcppls-state") << "starting\n";
    auto now = result("mcpp-ide/languageService/status", Json{{"candidates", candidates}, {"root", root.generic_string()}});
    CHECK(now["statusSource"] == "status");
    CHECK(now["workspace"]["state"] == "starting");
    CHECK(now["capabilities"] == Json::array({"status"}));
    CHECK(now["serverActions"].size() == 3);

    // The commands each editor is offered: mcppls's action under that editor's id, or not at all.
    auto jetbrains = result("mcpp-ide/commands/list", Json{{"editor", "jetbrains"}});
    CHECK(command(jetbrains, "mcpp.languageServer.restart")["action"]["command"] == "community.mcpp.mcppls.RestartServer");
    CHECK(command(jetbrains, "mcpp.languageServer.restartEngine").is_null());
    auto nvim = result("mcpp-ide/commands/list", Json{{"editor", "nvim"}});
    CHECK(command(nvim, "mcpp.languageServer.restart")["action"]["command"] == "McpplsRestart");
    auto vscode = result("mcpp-ide/commands/list", Json{{"editor", "vscode"}});
    CHECK(command(vscode, "mcpp.languageServer.openLogFolder")["action"]["arguments"] == Json::array({"logs"}));
    CHECK(command(vscode, "mcpp.languageServer.restartEngine")["action"]["command"] == "mcppls.restartClangd");
    auto cli = result("mcpp-ide/commands/list", Json{{"editor", "cli"}});
    CHECK(std::ranges::none_of(cli["commands"], [](const Json& c) { return c["action"].value("kind", "") == "forward"; }));

    // A change of state is told once; asking again with nothing changed tells nothing.
    const int before = wire.count("mcpp-ide/languageService/changed");
    std::ofstream(root / ".mock-mcppls-state") << "ready\n";
    auto ready = result("mcpp-ide/languageService/status", Json{{"candidates", candidates}, {"root", root.generic_string()}});
    CHECK(ready["workspace"]["state"] == "ready");
    (void)result("mcpp-ide/languageService/status", Json{{"candidates", candidates}, {"root", root.generic_string()}});
    CHECK(wire.count("mcpp-ide/languageService/changed") == before + 1);

    std::error_code ignored;
    std::filesystem::remove_all(base, ignored);
    std::println("test_mcppls_status: ok");
    return 0;
}
