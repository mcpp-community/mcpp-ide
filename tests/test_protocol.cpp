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
    auto init = result_of("initialize", Json{{"client", {{"name", "test"}}}, {"editor", "vscode"},
                                             {"settings", {{"mcpp.path", mock.generic_string()}}},
                                             {"workspaceFolders", Json::array({root.generic_string()})}});
    CHECK(init["projects"].size() == 1);
    CHECK(init["mcpp"].is_object() && init["mcpp"]["contract"] == true);
    const Json rooted{{"root", root.generic_string()}};
    result_of("project/summary", rooted);
    auto tests = result_of("test/list", rooted);
    CHECK(tests["tests"].size() == 1 && tests["tests"][0]["test"] == "test_smoke");
    auto targets = result_of("targets/list", rooted);
    CHECK(!targets["rows"].empty());
    auto cmds = result_of("commands/list", Json{{"editor", "jetbrains"}});
    for (const auto& c : cmds["commands"]) CHECK(c["action"]["kind"] != "forward");
    result_of("menu/quick", rooted);
    result_of("languageService/status", Json{{"candidates", Json::array()}});
    // The pack panel's choices: the closures SPEC-014 names, and linkages only where mcpp takes them.
    auto pack = result_of("pack/formats", rooted);
    CHECK(pack["modes"].size() == 4);
    const bool helpNamesLinkage = testsupport::read(testsupport::fixtures() / "pack-help.txt").find("--linkage") != std::string::npos;
    CHECK(pack["linkages"].empty() != helpNamesLinkage);
    result_of("graph/packages", rooted);
    result_of("graph/modules", rooted);
    result_of("manifest/check", rooted);
    result_of("settings/registry", Json::object());
    result_of("selfCheck", Json::object());
    result_of("cache/inventory", Json::object());
    auto launch = result_of("run/launch", rooted);
    CHECK(!launch["argv"].empty());
    result_of("debug/configuration", Json{{"root", root.generic_string()}, {"available", Json::array({"lldb"})}});

    result_of("job/start", Json{{"kind", "build"}, {"root", root.generic_string()}});
    result_of("job/start", Json{{"kind", "test"}, {"root", root.generic_string()}});
    for (int i = 0; i < 2000 && wire.notifications("job/finished").size() < 2; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    CHECK(wire.notifications("job/finished").size() == 2);
    for (const auto& name : server::kNotifications) {
        const auto& def = protocol["notifications"][std::string(name)]["params"];
        for (const auto& n : wire.notifications(name)) CHECK(conforms(n, def, protocol, name));
    }
    CHECK(!wire.notifications("job/plan").empty());
    CHECK(!wire.notifications("job/artifact").empty());
    const auto results = wire.notifications("test/result");
    CHECK(results.size() == 1 && results[0]["status"] == "passed");
    CHECK(wire.notifications("test/summary").size() == 1);
    // The plan a build sent is the project's configuration from then on.
    auto after = result_of("project/summary", rooted);
    CHECK(after.contains("configuration"));
    std::filesystem::remove_all(root);
    std::println("test_protocol: ok");
}
