// The view models, from the golden fixtures: what each panel and tree shows.
#include "support.hpp"
import std;
import nlohmann.json;
import mcppide.views;
import mcppide.policy;

using namespace mcppide;
using Json = nlohmann::json;

static Json fixture(const char* name) { return Json::parse(testsupport::read(testsupport::fixtures() / name)); }

int main() {
    auto cache = fixture("cache-list.json");
    views::CacheOptions o;
    o.topN = 3;
    auto inv = views::cache_inventory(cache, o);
    CHECK(inv["totalEntries"].get<int>() == static_cast<int>(cache["data"]["entries"].size()));
    CHECK(inv["topLabels"].size() <= 3);
    std::int64_t sum = 0;
    for (const auto& b : inv["ageBuckets"]) sum += b["entries"].get<std::int64_t>();
    CHECK(sum <= inv["totalEntries"].get<std::int64_t>());
    CHECK(inv["ageBuckets"].size() == 4);

    policy::CleanOptions co;
    auto all = views::cache_preview(cache, *policy::plan("cacheAll", co), co);
    CHECK(all["count"].get<std::size_t>() == cache["data"]["entries"].size());
    auto pkgs = views::cache_preview(cache, *policy::plan("cacheDeps", co), co);
    for (const auto& e : pkgs["entries"]) CHECK(e["kind"] == "pkg");
    co.budgetGiB = 0.000001;
    auto gc = views::cache_preview(cache, *policy::plan("cacheGc", co), co);
    CHECK(gc["count"].get<int>() >= 0);

    auto tc = views::toolchains(fixture("toolchain-list.json"));
    CHECK(!tc["rows"].empty() && tc["defaults"].size() >= 1);
    for (const auto& r : tc["rows"]) {
        const auto spec = r["spec"].get<std::string>();
        if (r["toolchain"].get<std::string>().find(' ') == std::string::npos) CHECK(spec.empty());
        else CHECK(spec.find('@') != std::string::npos);
    }

    auto search = fixture("search.json");
    auto lib = views::library(search, views::LibraryQuery{});
    CHECK(lib["total"].get<std::size_t>() == search["data"]["packages"].size());
    const auto firstId = lib["packages"][0]["id"].get<std::string>();
    CHECK(!views::library_detail(search, firstId).is_null());
    auto filtered = views::library(search, views::LibraryQuery{"zzzz-no-such-package"});
    CHECK(filtered["total"] == 0);

    auto db = fixture("build-database.json");
    auto pg = views::package_graph(db);
    CHECK(pg["nodes"].size() >= 2 && !pg["edges"].empty());
    auto mg = views::module_graph(db);
    bool std_node = false;
    for (const auto& n : mg["nodes"]) std_node = std_node || n["id"] == "std";
    CHECK(std_node);
    auto summary = views::project_summary(db, Json{{"record", fixture("result-ok.json")["data"]}});
    CHECK(summary["targets"].size() == 1 && summary["tests"].size() == 1);
    CHECK(summary["lastBuild"]["status"] == "ok" && !summary["artifacts"].empty());
    CHECK(views::format_bytes(1536) == "1.5 KiB");

    // Targets: one row per target, the selected one marked, and the platform a row names (the shape
    // of mcpp's platform object, design 2026-10-28 §9) carried and rendered.
    auto list = fixture("toolchain-list.json");
    auto t = views::targets(list, "");
    CHECK(!t["rows"].empty());
    std::set<std::string> seen;
    int selected = 0;
    for (const auto& r : t["rows"]) {
        CHECK(seen.insert(r["target"].get<std::string>()).second);
        if (r["selected"].get<bool>()) { ++selected; CHECK(r["target"] == t["host"]); }
        CHECK(r["platform"].is_null() == r["platformText"].get<std::string>().empty());
    }
    CHECK(selected == 1);
    Json platform{{"name", "openkal-platform"}, {"version", "0.17.1"}, {"source", "built-in"}};
    platform["fields"]["runner"] = Json{{"value", "okrun"}, {"source", "platform"}};
    list["data"]["targets"].push_back(Json{{"target", "x86_64-openkal"}, {"toolchain", "llvm 23.1.3"}, {"pin", "llvm@23.1.3"},
                                           {"status", "installed"}, {"default", false}, {"note", "ok"}, {"platform", platform}});
    list["data"]["hostPlatform"] = Json{{"name", ""}, {"version", nullptr}, {"source", "built-in"}};
    auto withPlatform = views::targets(list, "x86_64-openkal");
    bool found = false;
    for (const auto& r : withPlatform["rows"])
        if (r["target"] == "x86_64-openkal") {
            found = true;
            CHECK(r["selected"] == true);
            CHECK(r["platformText"] == "openkal-platform 0.17.1 (built-in)");
        }
    CHECK(found);
    CHECK(withPlatform["hostPlatformText"] == "host conventions (built-in)");

    // Stream records: each line of a build and of a test, normalised.
    std::istringstream build(testsupport::read(testsupport::fixtures() / "build-failed.ndjson"));
    std::set<std::string> types;
    for (std::string line; std::getline(build, line);)
        if (!line.empty()) types.insert(views::stream_record(Json::parse(line))["type"].get<std::string>());
    CHECK(types.contains("plan") && types.contains("progress") && types.contains("diagnostic"));
    std::istringstream test(testsupport::read(testsupport::fixtures() / "test.ndjson"));
    std::string first;
    std::getline(test, first);
    auto r = views::stream_record(Json::parse(first));
    CHECK(r["type"] == "test" && r["status"] == "passed" && r["test"] == "test_smoke");
    auto failed = views::stream_record(Json{{"test", "t"}, {"status", "fail"}, {"timed_out", true}});
    CHECK(failed["status"] == "timedOut");
    std::println("test_views: ok");
}
