// The project's mcpp (design 2026-10-30 part 2 §4, §13): each project asked in its own directory,
// through the program a version manager's shim stands for, with another project's
// XLINGS_PROJECT_DIR removed; features as mcpp states them, else from the frozen table; a pinned
// version that is not installed; an untrusted workspace; and a job whose envelope names another
// version, after which the project is asked again. The mock mcpp plays the shim per directory.
#include "support.hpp"
import std;
import nlohmann.json;
import mcppide.rpc;
import mcppide.server;
import mcppide.mcpp;

using namespace mcppide;
using Json = nlohmann::json;

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
    bool saw(std::string_view method) {
        std::lock_guard g(lock);
        for (const auto& m : messages) if (m.value("method", "") == method) return true;
        return false;
    }
};

std::filesystem::path project(const std::filesystem::path& base, std::string_view name,
                              std::vector<std::pair<std::string, std::string>> files) {
    const auto root = base / std::string(name);
    std::filesystem::create_directories(root / "src");
    std::ofstream(root / "mcpp.toml") << std::format("[package]\nname = \"{}\"\nversion = \"0.1.0\"\n", name);
    for (const auto& [file, text] : files) std::ofstream(root / file) << text << '\n';
    return root;
}

}  // namespace

int main() {
    const auto mock = testsupport::built("mcpp-ide-mock-mcpp");
    CHECK(!mock.empty());
    const auto base = std::filesystem::temp_directory_path() / std::format("mcppide-probe-{}", std::chrono::steady_clock::now().time_since_epoch().count());
    const auto old = project(base, "old", {{".mock-mcpp-version", "2026.10.29.1"}});
    const auto sets = project(base, "sets", {{".mock-mcpp-version", "2026.10.30.1"}});
    const auto stated = project(base, "stated", {{".mock-mcpp-version", "2026.10.31.1"},
                                                 {".mock-mcpp-features", "target-sets,pkg-format-list,polyglot"}});
    const auto missing = project(base, "missing", {{".mock-mcpp-missing", "2026.8.17.1"}});

    Wire wire;
    server::Session session(wire.server);
    session.install();
    auto result = [&](std::string_view method, Json params) {
        auto answer = wire.call(method, std::move(params));
        if (!answer.contains("result")) std::println(std::cerr, "{}: {}", method, answer.dump());
        CHECK(answer.contains("result"));
        return answer["result"];
    };
    result("initialize", Json{{"clientInfo", {{"name", "test"}}}, {"workspaceFolders", Json::array({Json{{"uri", testsupport::file_uri(base)}}})},
                {"initializationOptions", Json{{"editor", "vscode"}, {"trusted", true},
                              {"settings", {{"mcpp.path", mock.generic_string()}}}}}});

    // Before features: the frozen table, by the version the project's mcpp answers.
    auto a = result("mcpp-ide/project/mcpp", Json{{"root", old.generic_string()}});
    CHECK(a["state"] == "ok" && a["version"] == "2026.10.29.1");
    CHECK(a["features"].empty() && a["featuresFrozen"] == true);
    CHECK(a["fromSetting"] == true);
    auto b = result("mcpp-ide/project/mcpp", Json{{"root", sets.generic_string()}});
    CHECK(b["version"] == "2026.10.30.1" && b["featuresFrozen"] == true);
    CHECK(b["features"] == Json::array({"target-sets", "pkg-format-list"}));
    // Stated: taken at mcpp's word, a name the table does not hold included.
    auto c = result("mcpp-ide/project/mcpp", Json{{"root", stated.generic_string()}});
    CHECK(c["version"] == "2026.10.31.1" && c["featuresFrozen"] == false);
    CHECK(c["features"] == Json::array({"target-sets", "pkg-format-list", "polyglot"}));
    // A pinned version that is not installed: the shim's words and its install command.
    auto d = result("mcpp-ide/project/mcpp", Json{{"root", missing.generic_string()}});
    CHECK(d["state"] == "pinned-missing");
    CHECK(d["install"] == "xlings install mcpp@2026.8.17.1");
    CHECK(d["message"].get<std::string>().find("not found") != std::string::npos);

    // Why a feature is not offered: the party, the capability and the version that has it.
    auto why = result("mcpp-ide/capabilities/explain", Json{{"root", old.generic_string()}});
    bool explained = false;
    for (const auto& f : why["features"])
        if (f["id"] == "targets.sets") {
            CHECK(f["available"] == false);
            CHECK(f["reason"]["source"] == "mcpp" && f["reason"]["need"] == "2026.10.30.1");
            explained = true;
        }
    CHECK(explained);

    // No run carries another project's pin: every run of mcpp removes XLINGS_PROJECT_DIR, which an
    // editor started from another project's terminal inherits (mcpp docs/50). The removal is the
    // spawn's own instruction; a variable set in this process now would reach no child, since a
    // program's environment is the one it was started with.
    {
        mcpp::Client client(mock, mcpp::Protocol{});
        const auto o = client.spawn_options(mcpp::RunOptions{});
        CHECK(std::ranges::any_of(o.env, [](const auto& kv) { return kv.first == "XLINGS_PROJECT_DIR" && !kv.second; }));
    }

    // A job's envelope names another version: the project is asked again, and the editor told.
    std::ofstream(old / ".mock-mcpp-version") << "2026.10.31.1\n";
    auto job = result("mcpp-ide/job/start", Json{{"kind", "build"}, {"root", old.generic_string()}});
    for (int i = 0; i < 2000 && !wire.saw("mcpp-ide/project/mcppChanged"); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(5));
    CHECK(wire.saw("mcpp-ide/project/mcppChanged"));
    auto again = result("mcpp-ide/project/mcpp", Json{{"root", old.generic_string()}});
    CHECK(again["version"] == "2026.10.31.1");
    (void)job;

    // The report names every project asked, with no secret in it.
    auto report = result("mcpp-ide/report/collect", Json::object());
    CHECK(report["kind"] == "mcpp-ide.report" && report["projects"].size() >= 4);

    // An untrusted workspace: the pinned program is not run.
    Wire untrusted;
    server::Session second(untrusted.server);
    second.install();
    auto answer = untrusted.call("initialize", Json{{"clientInfo", {{"name", "test"}}}, {"workspaceFolders", Json::array({Json{{"uri", testsupport::file_uri(base)}}})},
                {"initializationOptions", Json{{"editor", "vscode"}, {"trusted", false},
                                                    {"settings", {{"mcpp.path", mock.generic_string()}}}}}});
    CHECK(answer.contains("result"));
    auto e = untrusted.call("mcpp-ide/project/mcpp", Json{{"root", missing.generic_string()}});
    CHECK(e.contains("result") && e["result"]["state"] == "not-probed");

    std::error_code ignored;
    std::filesystem::remove_all(base, ignored);
    std::println("test_probe: ok");
    return 0;
}
