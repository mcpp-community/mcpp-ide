// The view models, from the golden fixtures: what each panel and tree shows.
import std;
import mcppide.testsupport;
import mcpplibs.mtest;
import nlohmann.json;
import mcppide.views;
import mcppide.policy;

using testsupport::check;
using namespace mcpplibs::mtest;

using namespace mcppide;
using Json = nlohmann::json;

static Json fixture(const char* name) { return Json::parse(testsupport::read(testsupport::fixtures() / name)); }

UTest views{"views", [] {
    auto cache = fixture("cache-list.json");
    views::CacheOptions o;
    o.topN = 3;
    auto inv = views::cache_inventory(cache, o);
    check(inv["totalEntries"].get<int>() == static_cast<int>(cache["data"]["entries"].size()));
    check(inv["topLabels"].size() <= 3);
    std::int64_t sum = 0;
    for (const auto& b : inv["ageBuckets"]) sum += b["entries"].get<std::int64_t>();
    check(sum <= inv["totalEntries"].get<std::int64_t>());
    check(inv["ageBuckets"].size() == 4);
    for (const auto& k : inv["byKind"]) check(k["bytesText"].is_string());
    for (const auto& b : inv["ageBuckets"]) check(b["bytesText"].is_string());

    policy::CleanOptions co;
    auto all = views::cache_preview(cache, *policy::plan("cacheAll", co), co);
    check(all["count"].get<std::size_t>() == cache["data"]["entries"].size());
    auto pkgs = views::cache_preview(cache, *policy::plan("cacheDeps", co), co);
    for (const auto& e : pkgs["entries"]) check(e["kind"] == "pkg");
    co.budgetGiB = 0.000001;
    auto gc = views::cache_preview(cache, *policy::plan("cacheGc", co), co);
    check(gc["count"].get<int>() >= 0);

    auto tc = views::toolchains(fixture("toolchain-list.json"));
    check(!tc["rows"].empty() && tc["defaults"].size() >= 1);
    for (const auto& r : tc["rows"]) {
        const auto spec = r["spec"].get<std::string>();
        if (r["toolchain"].get<std::string>().find(' ') == std::string::npos) check(spec.empty());
        else check(spec.find('@') != std::string::npos);
    }

    auto search = fixture("search.json");
    auto lib = views::library(search, views::LibraryQuery{});
    check(lib["total"].get<std::size_t>() == search["data"]["packages"].size());
    const auto firstId = lib["packages"][0]["id"].get<std::string>();
    check(!views::library_detail(search, firstId).is_null());
    auto filtered = views::library(search, views::LibraryQuery{"zzzz-no-such-package"});
    check(filtered["total"] == 0);

    auto db = fixture("build-database.json");
    auto pg = views::package_graph(db);
    check(pg["nodes"].size() >= 2 && !pg["edges"].empty());
    auto mg = views::module_graph(db);
    bool std_node = false;
    for (const auto& n : mg["nodes"]) std_node = std_node || n["id"] == "std";
    check(std_node);
    auto summary = views::project_summary(db, Json{{"record", fixture("result-ok.json")["data"]}});
    check(summary["targets"].size() == 1 && summary["tests"].size() == 1);
    check(summary["lastBuild"]["status"] == "ok" && !summary["artifacts"].empty());
    check(views::format_bytes(1536) == "1.5 KiB");

    // Targets: one row per target, the selected one marked, and the platform a row names (the shape
    // of mcpp's platform object, design 2026-10-28 §9) carried and rendered.
    auto list = fixture("toolchain-list.json");
    auto t = views::targets(list, "");
    check(!t["rows"].empty());
    std::set<std::string> seen;
    int selected = 0;
    for (const auto& r : t["rows"]) {
        check(seen.insert(r["target"].get<std::string>()).second);
        if (r["selected"].get<bool>()) { ++selected; check(r["target"] == t["host"]); }
        check(r["platform"].is_null() == r["platformText"].get<std::string>().empty());
    }
    check(selected == 1);
    // A row names its platform as the target table does, a package spec (mcpp SPEC-013); the view
    // renders it as a plan's platform object.
    bool found = false;
    for (const auto& r : t["rows"])
        if (r["target"] == "x86_64-openkal") {
            found = true;
            check(r["platform"]["name"] == "mcpplibs.openkal-platform");
            check(r["platformText"].get<std::string>().starts_with("mcpplibs.openkal-platform 0.17"));
            check(r["platformText"].get<std::string>().ends_with("(target table)"));
        }
    check(found);
    list["data"]["hostPlatform"] = Json{{"name", ""}, {"version", nullptr}, {"source", "built-in"}};
    auto withPlatform = views::targets(list, "x86_64-openkal");
    for (const auto& r : withPlatform["rows"])
        if (r["target"] == "x86_64-openkal") check(r["selected"] == true);
    check(withPlatform["hostPlatformText"] == "host conventions (built-in)");
    // Target sets (mcpp SPEC-015): offered only when the installed mcpp names them; one row per
    // group of targets of one platform that differ only in the architecture, before its members.
    for (const auto& r : t["rows"]) check(!r.contains("set"));
    auto withSets = views::targets(list, "openkal", true);
    std::size_t setAt = 0, memberAt = 0, i = 0;
    bool setSelected = false;
    for (const auto& r : withSets["rows"]) {
        if (r["target"] == "openkal") {
            setAt = i;
            check(r["set"] == true);
            check(r["members"].size() >= 2);
            // The members in the target table's order.
            check(r["label"] == "openkal (aarch64, x86_64)");
            setSelected = r["selected"].get<bool>();
        }
        if (r["target"] == "x86_64-openkal" && memberAt == 0) memberAt = i;
        ++i;
    }
    check(setSelected && setAt < memberAt);
    // A planned row is no member: linux-musl is x86_64 and aarch64, not the planned riscv64.
    for (const auto& r : withSets["rows"])
        if (r["target"] == "linux-musl") check(r["members"].size() == 2);

    // Stream records: each line of a build and of a test, normalised.
    std::istringstream build(testsupport::read(testsupport::fixtures() / "build-failed.ndjson"));
    std::set<std::string> types;
    for (std::string line; std::getline(build, line);)
        if (!line.empty()) types.insert(views::stream_record(Json::parse(line))["type"].get<std::string>());
    check(types.contains("plan") && types.contains("progress") && types.contains("diagnostic"));
    std::istringstream test(testsupport::read(testsupport::fixtures() / "test.ndjson"));
    std::string first;
    std::getline(test, first);
    auto r = views::stream_record(Json::parse(first));
    check(r["type"] == "test" && r["status"] == "passed" && r["test"] == "test_smoke");
    // mcpp's statuses, value for value (D16): pass, run_fail (timed_out), compile_fail, not_run, built.
    auto timedOut = views::stream_record(Json{{"test", "t"}, {"status", "run_fail"}, {"timed_out", true}});
    check(timedOut["status"] == "timedOut" && timedOut["timedOut"] == true);
    auto failed = views::stream_record(Json{{"test", "t"}, {"status", "run_fail"},
                                            {"run_output", "running\ntests/t.cpp:12: assertion failed\n"}});
    check(failed["status"] == "failed" && failed["location"]["path"] == "tests/t.cpp" && failed["location"]["line"] == 11);
    auto compileFailed = views::stream_record(Json{{"test", "t"}, {"status", "compile_fail"}, {"compile_output", "tests/t.cpp:3:5: error: x\n"}});
    check(compileFailed["status"] == "failed" && compileFailed["compileFailed"] == true && compileFailed["location"]["line"] == 2);
    auto notRun = views::stream_record(Json{{"test", "t"}, {"status", "not_run"}, {"reason", "no runner for riscv64"}});
    check(notRun["status"] == "skipped" && notRun["reason"] == "no runner for riscv64" && notRun["location"].is_null());
    auto built = views::stream_record(Json{{"test", "t"}, {"status", "built"}});
    check(built["status"] == "skipped" && built["builtOnly"] == true && !built["reason"].get<std::string>().empty());
    check(views::stream_record(Json{{"test", "t"}, {"status", "fail"}})["status"] == "errored");
    // A passed test keeps its output.
    auto passed = views::stream_record(Json{{"test", "t"}, {"status", "pass"}, {"run_output", "hello\n"}});
    check(passed["status"] == "passed" && passed["output"] == "hello\n" && passed["outputTruncated"] == false);
    // Past the limit, the head and the tail are kept, and the record says so.
    std::string big = "first line\n";
    while (big.size() < 3 * views::kOutputLimit) big += "a line of output\n";
    big += "last line\n";
    auto long_ = views::stream_record(Json{{"test", "t"}, {"status", "pass"}, {"run_output", big}});
    const auto kept = long_["output"].get<std::string>();
    check(long_["outputTruncated"] == true && kept.size() <= views::kOutputLimit + 100);
    check(kept.starts_with("first line\n") && kept.ends_with("last line\n") && kept.contains("bytes of output left out"));
    // Where a failure is: GCC/Clang, MSVC and a drive; nothing that only looks like it.
    check(views::failure_location("src/a.cpp:3:5: error: x")["line"] == 2);
    check(views::failure_location("C:\\p\\a.cpp(12): error C2065")["path"] == "C:\\p\\a.cpp");
    check(views::failure_location("C:\\p\\a.cpp(12): error C2065")["line"] == 11);
    check(views::failure_location("C:/p/b.cpp:7: Failure")["path"] == "C:/p/b.cpp");
    check(views::failure_location("started at 12:30: ok").is_null());
    check(views::failure_location("no location here").is_null());
}};
