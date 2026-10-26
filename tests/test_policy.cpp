// The cleanup table: every action's argv and its level, and that an action
// needing a preview or an acknowledgement is not confirmed without it.
#include "support.hpp"
import std;
import nlohmann.json;
import mcppide.policy;

using namespace mcppide;

int main() {
    policy::CleanOptions o;
    for (auto a : policy::kActions) CHECK(policy::plan(a, o).has_value());
    CHECK(!policy::plan("everything", o));
    auto project = *policy::plan("project", o);
    CHECK(project.level == 1 && project.argv == std::vector<std::string>{"clean"} && project.requiresTrust);
    auto stale = *policy::plan("stale", o);
    CHECK(stale.level == 2 && stale.previewSource == "mcpp" && stale.previewArgv.back() == "--dry-run");
    auto all = *policy::plan("cacheAll", o);
    CHECK(all.level == 3 && all.acknowledge && all.previewSource == "inventory");
    o.budgetGiB = 2;
    auto gc = *policy::plan("cacheGc", o);
    CHECK((gc.argv == std::vector<std::string>{"cache", "gc", "--max-size", "2GiB"}));
    CHECK(!policy::confirmed(all, nlohmann::json{{"confirmed", true}, {"previewShown", true}}));
    CHECK(policy::confirmed(all, nlohmann::json{{"confirmed", true}, {"previewShown", true}, {"acknowledged", true}}));
    CHECK(policy::confirmed(*policy::plan("cacheVerify", o), nullptr));
    std::println("test_policy: ok");
}
