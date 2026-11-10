// The cleanup table: every action's argv and its level, and that an action
// needing a preview or an acknowledgement is not confirmed without it.
import std;
import mcppide.testsupport;
import mcpplibs.mtest;
import nlohmann.json;
import mcppide.policy;

using testsupport::check;
using namespace mcpplibs::mtest;

using namespace mcppide;

UTest policy{"policy", [] {
    policy::CleanOptions o;
    for (auto a : policy::kActions) check(policy::plan(a, o).has_value());
    check(!policy::plan("everything", o));
    auto project = *policy::plan("project", o);
    check(project.level == 1 && project.argv == std::vector<std::string>{"clean"} && project.requiresTrust);
    auto stale = *policy::plan("stale", o);
    check(stale.level == 2 && stale.previewSource == "mcpp" && stale.previewArgv.back() == "--dry-run");
    auto all = *policy::plan("cacheAll", o);
    check(all.level == 3 && all.acknowledge && all.previewSource == "inventory");
    o.budgetGiB = 2;
    auto gc = *policy::plan("cacheGc", o);
    check((gc.argv == std::vector<std::string>{"cache", "gc", "--max-size", "2GiB"}));
    check(!policy::confirmed(all, nlohmann::json{{"confirmed", true}, {"previewShown", true}}));
    check(policy::confirmed(all, nlohmann::json{{"confirmed", true}, {"previewShown", true}, {"acknowledged", true}}));
    check(policy::confirmed(*policy::plan("cacheVerify", o), nullptr));
}};
