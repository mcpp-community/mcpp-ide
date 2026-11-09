// The runs of a project's tests as mcpp keeps them (mtest spec P12; design 2026-11-10): a store read
// back, its rows attributed to their programs, and what the window says -- the failures and since
// when, the slowest, the flaky, the metrics against their bounds, the schedule and its critical path.
// And the model of a program: the parts of a directory program derived as mcpp 2026.11.9.1 forms
// them, or taken as mcpp#917 states them; a record attributed to its program; a case's status.
import std;
import mcppide.testsupport;
import mcpplibs.mtest;
import nlohmann.json;
import mcppide.scan;
import mcppide.testing;
import mcppide.teststore;

using testsupport::check;
using namespace mcpplibs::mtest;

using namespace mcppide;
using Json = nlohmann::json;

UTest teststore{"teststore", [] {
    const auto fixture = testsupport::repo() / "protocol" / "fixtures" / "mtest-store";

    // The store's last runs, oldest first; a run read once is kept, and a window takes the newest.
    "the last runs of a store"_test = [&] {
        std::map<std::string, teststore::Run> cache;
        auto runs = teststore::last_runs(fixture, 20, cache);
        check(runs.size() == 3 && cache.size() == 3);
        check(runs.front().id == "20261108T100000-0a01" && runs.back().id == "20261110T100000-0c03");
        check(runs.back().programs.size() == 3 && runs.back().cases.size() == 2);
        check(runs.back().cases[0].kind == "bench" && runs.back().cases[0].metrics.size() == 1);
        check(runs.front().cases[0].kind.empty() && !runs.front().cases[0].startedMs);
        const auto two = teststore::last_runs(fixture, 2, cache);
        check(two.size() == 2 && two.front().id == "20261109T100000-0b02");
        std::map<std::string, teststore::Run> none;
        check(teststore::last_runs(fixture / "absent", 20, none).empty());

        // The parts are a program's: `unit/a` and `unit/b` are `unit`'s.
        teststore::attribute(runs, [](std::string_view name) { return name.starts_with("unit/") ? std::string("unit") : std::string(name); });
        check(runs.back().cases[1].program == "unit" && runs.back().cases[1].part == "unit/b");
        check(runs.back().programs[0].name == "unit" && runs.back().programs[0].part == "unit/a");
        check(runs.back().programs[2].name == "smoke" && runs.back().programs[2].part.empty());

        const auto in = teststore::insights(runs);
        check(in["runs"] == 3 && in["run"]["id"] == "20261110T100000-0c03" && in["run"]["status"] == "fail");
        check(in["byKind"]["bench"]["pass"] == 1 && in["byKind"]["unit"]["fail"] == 1);
        // beta failed, passed, failed: it fails since the last run, and it is flaky.
        check(in["failures"].size() == 1 && in["failures"][0]["case"] == "beta" && in["failures"][0]["since"] == "20261110T100000-0c03");
        check(in["failures"][0]["new"] == true && in["failures"][0]["part"] == "unit/b" && in["failures"][0]["line"] == 3);
        check(in["failures"][0]["expected"] == "4" && in["failures"][0]["actual"] == "5");
        check(in["flaky"].size() == 1 && in["flaky"][0]["passed"] == 1 && in["flaky"][0]["failed"] == 2 && in["flaky"][0]["flips"] == 2);
        check(in["slowest"][0]["case"] == "beta" && in["slowest"][0]["ms"] == 1.6 && in["slowest"][0]["medianMs"] == 1.5);
        check(in["metrics"].size() == 1 && in["metrics"][0]["name"] == "ready" && in["metrics"][0]["values"].size() == 1);
        check(std::abs(in["metrics"][0]["margin"].get<double>() - 0.72) < 1e-9 && in["metrics"][0]["better"] == "lower");
        // Two workers; the critical path is the lane that ends last (worker 0: unit/a, then smoke).
        check(in["schedule"]["lanes"].size() == 2 && in["schedule"]["criticalPath"] == Json::array({"unit", "smoke"}));
        check(in["available"]["metrics"] == true && in["available"]["kinds"] == true && in["available"]["cases"] == true);

        const auto h = teststore::history(runs, "unit", "beta");
        check(h["runs"].size() == 3 && h["cases"].size() == 1 && h["cases"][0]["results"][1]["status"] == "pass");
        check(h["cases"][0]["results"][2]["part"] == "unit/b");
        check(teststore::insights({})["run"].is_null());
    };

    // `[test] store` names the store; target/mtest otherwise.
    "the store of a project"_test = [&] {
        const auto root = testsupport::temp() / std::format("mcppide-store-{}", std::chrono::steady_clock::now().time_since_epoch().count());
        std::filesystem::create_directories(root);
        std::ofstream(root / "mcpp.toml") << "[package]\nname = \"a\"\n\n[test]\nprograms = \"directory\"\n";
        check(teststore::store_dir(root) == root / "target" / "mtest");
        std::ofstream(root / "mcpp.toml") << "[package]\nname = \"a\"\n\n[test]\nstore = \".runs\"\n\n[build]\nstore = \"x\"\n";
        check(teststore::store_dir(root) == root / ".runs");
        std::filesystem::remove_all(root);
    };

    // A program of a directory, as mcpp 2026.11.9.1 forms it: its sources, a part per file, and `main`
    // only where a file defines it; a program of one file is that file. mcpp#917 states them.
    "the programs of a directory"_test = [&] {
        const auto root = testsupport::temp() / std::format("mcppide-program-{}", std::chrono::steady_clock::now().time_since_epoch().count());
        std::filesystem::create_directories(root / "tests" / "unit");
        for (const auto* f : {"b.cpp", "a.cpp", "notes.md"}) std::ofstream(root / "tests" / "unit" / f) << "\n";
        const auto a = (root / "tests" / "unit" / "a.cpp").generic_string(), b = (root / "tests" / "unit" / "b.cpp").generic_string();
        const auto none = [](const std::filesystem::path&) { return false; };
        auto unit = testing::program_of(Json{{"test", "unit"}, {"main", a}}, none);
        check(unit.layout == "directory" && unit.main.empty() && unit.files == std::vector<std::string>{a, b});
        check(unit.parts.size() == 2 && unit.parts[0].first == "unit/a" && unit.parts[1].second == b);
        const auto withMain = testing::program_of(Json{{"test", "unit"}, {"main", a}},
                                                  [&](const std::filesystem::path& f) { return f.generic_string() == b; });
        check(withMain.main == b);
        const auto one = testing::program_of(Json{{"test", "unit/a"}, {"main", a}}, none);
        check(one.layout == "file" && one.files == std::vector<std::string>{a} && one.parts.empty() && one.main == a);
        const auto stated = testing::program_of(Json{{"test", "unit"}, {"main", nullptr}, {"files", Json::array({a, b})},
                                                     {"parts", Json::array({Json{{"name", "unit/a"}, {"file", a}}})}}, none);
        check(stated.layout == "directory" && stated.main.empty() && stated.parts.size() == 1);
        const auto json = testing::program_json(unit);
        check(json["layout"] == "directory" && json["parts"][1]["name"] == "unit/b" && json["main"] == "");

        const std::vector<testing::Program> programs{unit, one, testing::program_of(Json{{"test", "smoke"}, {"main", "x/smoke.cpp"}}, none)};
        check(testing::owner_of("unit/b", "", programs) == "unit");
        check(testing::owner_of("unit/a", "", programs) == "unit/a");   // a program of that name
        check(testing::owner_of("smoke", "", programs) == "smoke");
        check(testing::owner_of("other/x", "", programs) == "other/x");
        check(testing::owner_of("unit/b", "stated", programs) == "stated");
        check(testing::part_of_file(unit, b) == "unit/b" && testing::part_of_file(unit, "x.cpp").empty());
        check(testing::kind_of_path(a) == "unit" && testing::kind_of_path("/p/tests/bench/x.cpp") == "bench");
        check(testing::kind_of_path("/p/tests/x.cpp").empty() && testing::kind_of_path("/p/src/unit/x.cpp").empty());
        std::filesystem::remove_all(root);
    };

    // A case's status as the editors know it, and its failures, metrics and bounds.
    "a case as the editors receive it"_test = [&] {
        check(testing::case_status("pass").status == "passed" && testing::case_status("skip").status == "skipped");
        check(testing::case_status("xfail").status == "passed" && testing::case_status("xfail").expectedFailure);
        check(testing::case_status("xpass").status == "failed" && testing::case_status("xpass").unexpectedPass);
        check(testing::case_status("invalid").status == "errored" && testing::case_status("fail").status == "failed");
        const auto c = testing::protocol_case(Json{{"id", "r"}, {"status", "fail"}, {"duration_ms", 2.6},
            {"failures", Json::array({Json{{"file", "t.cpp"}, {"line", 7}, {"message", "a == b"}, {"expected", "1"}, {"actual", "2"}}})},
            {"metrics", Json::array({Json{{"name", "ready"}, {"value", 3}}})}, {"bounds", Json::array({Json{{"metric", "ready"}, {"max", 5}}, Json{{"max", 1}}})},
            {"kind", "bench"}, {"tags", Json::array({"slow"})}});
        check(c["case"] == "r" && c["status"] == "failed" && c["durationMs"] == 2 && c["location"]["line"] == 6);
        check(c["failures"][0]["expected"] == "1" && c["message"] == "t.cpp:7: a == b (expected 1, actual 2)");
        check(c["metrics"].size() == 1 && c["bounds"].size() == 1 && c["kind"] == "bench" && c["tags"][0] == "slow");
        const auto plain = testing::protocol_case(Json{{"id", "s"}, {"status", "skip"}, {"reason", "no network"}});
        check(plain["message"] == "no network" && plain["location"].is_null() && !plain.contains("metrics") && !plain.contains("expectedFailure"));
    };

    // A case's kind and the bounds on its metrics, as its source states them (mtest's manifest).
    "a case as its source states it"_test = [&] {
        const auto found = scan::scan("import mcpplibs.mtest;\nusing namespace mcpplibs::mtest;\n"
                                      "Bench alpha{\"alpha\", at_most{\"ready\", 15}, [] {}};\nUTest beta{\"beta\", [] {}};\n", {});
        const auto cases = testing::static_cases(*testing::framework("mtest"), found, Json::object());
        check(cases.size() == 2 && cases[0].kind == "bench" && cases[0].bounds.size() == 1 && cases[0].bounds[0]["max"] == 15);
        check(cases.size() == 2 && cases[1].kind == "unit" && cases[1].bounds.empty());
        const auto row = testing::case_json(cases[0]);
        check(row["kind"] == "bench" && row["bounds"][0]["metric"] == "ready" && !row.contains("tags") && !row.contains("file"));
    };
}};
