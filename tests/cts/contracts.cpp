// The two contracts, held to their schemas (design 2026-10-30 part 2 §2): every golden fixture
// (mcpp's real output, and the shapes mcpp#917 proposes) against the definition of
// protocol/mcpp-contract.schema.json that protocol/fixtures.json names for it; the methods the backend
// registers and the notifications it sends against protocol/protocol.schema.json, both ways.
import std;
import mcppide.testsupport;
import mcpplibs.mtest;
import nlohmann.json;
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

}  // namespace

CTS contracts{"contracts", [] {
    "mcpp's output conforms to the contract"_test = [] {
        const auto contract = load(testsupport::repo() / "protocol" / "mcpp-contract.schema.json");
        const auto index = load(testsupport::repo() / "protocol" / "fixtures.json");
        for (const auto& f : index["fixtures"]) {
            const auto path = testsupport::fixtures() / f["file"].get<std::string>();
            if (!std::filesystem::exists(path)) {
                check(f.value("optional", false));
                continue;
            }
            const auto text = testsupport::read(path);
            if (f.contains("each")) {
                const Json def{{"$ref", "#/$defs/" + f["each"].get<std::string>()}};
                std::istringstream lines(text);
                int n = 0;
                for (std::string line; std::getline(lines, line);) {
                    if (line.empty()) continue;
                    check(conforms(Json::parse(line), def, contract, std::format("{}:{}", f["file"].get<std::string>(), ++n)));
                }
                check(n > 0);
            } else {
                const Json def{{"$ref", "#/$defs/" + f["schema"].get<std::string>()}};
                check(conforms(Json::parse(text), def, contract, f["file"].get<std::string>()));
            }
        }
    };
    "the backend registers what its protocol states"_test = [] {
        const auto protocol = load(testsupport::repo() / "protocol" / "protocol.schema.json");
        std::set<std::string> methods, notifications;
        for (auto it = protocol["methods"].begin(); it != protocol["methods"].end(); ++it) methods.insert(it.key());
        for (auto it = protocol["notifications"].begin(); it != protocol["notifications"].end(); ++it) notifications.insert(it.key());
        std::set<std::string> registered(server::kMethods.begin(), server::kMethods.end());
        std::set<std::string> sent(server::kNotifications.begin(), server::kNotifications.end());
        for (const auto& m : methods) if (!registered.contains(m)) std::println(std::cerr, "schema method not registered: {}", m);
        for (const auto& m : registered) if (!methods.contains(m)) std::println(std::cerr, "registered method not in the schema: {}", m);
        check(methods == registered);
        check(notifications == sent);
    };
}};
