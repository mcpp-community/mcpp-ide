// What a person waits for in an editor, measured where the backend spends it: the rules layer's
// answer to a keystroke in a large manifest, the reading of a large test file for its entries and
// cases (`source/entries`, `test/cases`), and the insights over a full window of stored runs
// (`test/insights`). Each bound is a median per call, set well above what a CI runner measures, so a
// bound that fails names a regression rather than a slow machine.
import std;
import mcppide.testsupport;
import mcpplibs.mtest;
import mcpplibs.mtest.bench;
import nlohmann.json;
import mcppide.documents;
import mcppide.scan;
import mcppide.teststore;

using namespace mcpplibs::mtest;

using namespace mcppide;
using Json = nlohmann::json;

namespace {

// A manifest of `n` dependencies, as a large workspace member's.
documents::Document manifest(int n) {
    documents::Document d;
    d.uri = "file:///bench/mcpp.toml";
    d.path = "/bench/mcpp.toml";
    d.kind = documents::Kind::manifest;
    d.lines = {"[package]", "name = \"bench\"", "version = \"0.1.0\"", "", "[dependencies]"};
    for (int i = 0; i < n; ++i) d.lines.push_back(std::format("dep{} = \"1.{}.0\"", i, i % 10));
    d.lines.push_back("");
    d.lines.push_back("[build]");
    d.lines.push_back("sources = [\"src/*.cppm\"]");
    return d;
}

// A test file of `n` cases in suites of ten, as a large unit test file's.
std::string test_file(int n) {
    std::string text = "import std;\nimport mcpplibs.mtest;\nusing namespace mcpplibs::mtest;\n\n";
    for (int s = 0; s < n / 10; ++s) {
        text += std::format("UTest suite{0}{{\"suite {0}\", [] {{\n", s);
        for (int c = 0; c < 10; ++c)
            text += std::format("    \"case {0}\"_test = [] {{ expect(eq({0} + 1, {1})); }};  // \"not a case\"\n", c, c + 1);
        text += "}};\n\n";
    }
    return text;
}

// A window of `runs` stored runs of `cases` cases each, every tenth failing in turn and every case
// reporting one metric against its bound.
std::vector<teststore::Run> window(int runs, int cases) {
    std::vector<teststore::Run> out;
    for (int r = 0; r < runs; ++r) {
        teststore::Run run;
        run.id = std::format("20261110T{:06}-0000", r);
        run.status = "fail";
        for (int p = 0; p < 8; ++p)
            run.programs.push_back(teststore::ProgramRun{.name = std::format("unit/f{}", p), .status = "pass", .ms = 100.0 + p,
                                                         .startedMs = 10.0 * p, .worker = p % 4});
        for (int c = 0; c < cases; ++c) {
            teststore::CaseRun one{.program = std::format("unit/f{}", c % 8), .id = std::format("case {}", c), .kind = "unit",
                                   .status = (c + r) % 10 == 0 ? "fail" : "pass", .ms = 1.0 + c % 7};
            one.metrics = Json::array({Json{{"name", "ready"}, {"value", 1.0 + (c + r) % 5}, {"unit", "s"}}});
            one.bounds = Json::array({Json{{"metric", "ready"}, {"max", 15}}});
            run.cases.push_back(std::move(one));
        }
        out.push_back(std::move(run));
    }
    return out;
}

}  // namespace

Bench keystroke{"a keystroke in a manifest of 400 dependencies", at_most{"manifest.keystroke", 0.05}, [] {
    const auto d = manifest(400);
    const documents::Options o;
    const int last = static_cast<int>(d.lines.size()) - 1;
    bench::measure("manifest.keystroke", [&] {
        bool needCatalogue = false;
        bench::keep(documents::diagnostics(d, o));
        bench::keep(documents::completion(d, last, 10, o, nullptr, needCatalogue));
    });
}};

Bench testFile{"the entries and cases of a test file of 2000 cases", at_most{"tests.scan", 0.1}, [] {
    const auto text = test_file(2000);
    bench::measure("tests.scan", [&] {
        const auto found = scan::scan(text, {});
        testsupport::check(found.cases.size() == 2200);
        bench::keep(found);
    }, {.samples = 10});
}};

Bench insights{"the insights over 50 runs of 400 cases", at_most{"tests.insights", 0.5}, [] {
    const auto runs = window(50, 400);
    bench::measure("tests.insights", [&] { bench::keep(teststore::insights(runs)); }, {.samples = 10});
}};
