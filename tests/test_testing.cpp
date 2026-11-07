// A source read as text (design 2026-10-30 part 2 §6.3, §7.3): `main` and a framework's macros,
// never inside a comment, a string or a raw string; a program's framework needs both its package
// and its macros; cases named as each framework names them, listed names attached to them, the
// filter escaped by the framework's rules, and the three report formats read back. And the
// backend's source/entries and test/cases over a project on disk.
import std;
import mcppide.testsupport;
import nlohmann.json;
import mcppide.scan;
import mcppide.testing;
import mcppide.rpc;
import mcppide.server;

using testsupport::check;

using namespace mcppide;
using Json = nlohmann::json;

namespace {

const testing::Framework& fw(std::string_view id) {
    const auto* f = testing::framework(id);
    check(f != nullptr);
    return *f;
}

}  // namespace

int main() {
    // main: the first `int main(` or `auto main(` at file scope, outside comments and literals.
    {
        const auto r = scan::scan(R"(// int main() in a comment
/* int main() */
const char* s = "int main(";
const char* raw = R"x(int main()
)x";
#define X int main(
namespace n { int main(); }
int
main(int argc, char** argv) { return 0; }
)", {});
        check(r.mainLine && *r.mainLine == 8);
        check(!scan::scan("void f() { obj.main(); std::main(); }", {}).mainLine);
        check(scan::scan("export auto main() -> int { return 0; }", {}).mainLine == 0);
    }
    // Macro calls at the start of a declaration, with their arguments.
    const auto macros = testing::macros(Json::object());
    {
        const auto r = scan::scan(R"(#include <gtest/gtest.h>
TEST(Math, Adds) { EXPECT_EQ(1 + 1, 2); }
// TEST(Not, This)
TEST_P(Param, Works) {}
void helper() { auto x = TEST(1, 2); }
TEST(Math,
     Spans) {}
)", macros);
        check(r.calls.size() == 3);
        check(r.calls[0].name == "TEST" && r.calls[0].line == 1 && r.calls[0].arguments == std::vector<std::string>({"Math", "Adds"}));
        check(r.calls[1].name == "TEST_P" && r.calls[1].line == 3);
        check(r.calls[2].line == 5 && r.calls[2].arguments[1] == "Spans");
        check(scan::literal_value(R"("a \"b\"" " c")") == "a \"b\" c");
    }

    // A program's framework: its package in the plan and its macros in its source.
    {
        const auto gtestSource = scan::scan("TEST(A, B) {}\nTEST_F(F, C) {}\n", macros);
        check(testing::framework_of({"app", "compat.gtest"}, gtestSource, Json::object()) == &fw("gtest"));
        check(testing::framework_of({"app"}, gtestSource, Json::object()) == nullptr);
        const auto ownMain = scan::scan("int main() { return 0; }\n", macros);
        check(testing::framework_of({"compat.gtest"}, ownMain, Json::object()) == nullptr);
        // A macro of the project's own, named by mcpp.test.macros.
        const Json extra{{"gtest", Json::array({"MY_TEST"})}};
        const auto own = scan::scan("MY_TEST(S, N) {}\n", testing::macros(extra));
        check(testing::framework_of({"compat.gtest"}, own, extra) == &fw("gtest"));
        const auto cases = testing::static_cases(fw("gtest"), own, extra);
        check(cases.size() == 1 && cases[0].name == "S.N" && cases[0].line == 0);
        // Catch2 and doctest share TEST_CASE; the package decides.
        const auto both = scan::scan("TEST_CASE(\"adds\", \"[math]\") {}\nSCENARIO(\"grows\") {}\n", macros);
        check(testing::framework_of({"compat.catch2"}, both, Json::object()) == &fw("catch2"));
        check(testing::framework_of({"compat.doctest"}, both, Json::object()) == &fw("doctest"));
        const auto catchCases = testing::static_cases(fw("catch2"), both, Json::object());
        check(catchCases.size() == 2 && catchCases[0].name == "adds" && catchCases[1].name == "Scenario: grows");
        const auto doctestCases = testing::static_cases(fw("doctest"), both, Json::object());
        check(doctestCases[1].name == "  Scenario: grows");
    }

    // Listings, base names and the merge.
    {
        const auto names = testing::listed(fw("gtest"), "Math.\n  Adds\n  Spans\nInst/Param.\n  Works/0  # GetParam() = 1\n  Works/1  # GetParam() = 2\nTyped/0.  # TypeParam = int\n  Holds\n");
        check(names == std::vector<std::string>({"Math.Adds", "Math.Spans", "Inst/Param.Works/0", "Inst/Param.Works/1", "Typed/0.Holds"}));
        check(testing::base_name(fw("gtest"), "Inst/Param.Works/1") == "Param.Works");
        check(testing::base_name(fw("gtest"), "Typed/0.Holds") == "Typed.Holds");
        const std::vector<testing::Case> statics{{"Math.Adds", 1, "static"}, {"Param.Works", 3, "static"}, {"Math.Spans", 5, "static"}};
        const auto merged = testing::merge(statics, names, fw("gtest"));
        check(merged[0].name == "Math.Adds" && merged[0].source == "listed");
        check(merged[1].name == "Inst/Param.Works/0" && merged[1].line == 3);
        check(std::ranges::any_of(merged, [](const auto& c) { return c.name == "Inst/Param.Works/1" && c.line == 3; }));
        check(std::ranges::any_of(merged, [](const auto& c) { return c.name == "Typed/0.Holds" && c.line == -1; }));
        const auto listedCatch = testing::listed(fw("catch2"), R"({"version": 1, "listings": {"tests": [{"name": "adds"}, {"name": "a, b"}]}})");
        check(listedCatch == std::vector<std::string>({"adds", "a, b"}));
        const auto listedDoctest = testing::listed(fw("doctest"), "[doctest] doctest version is \"2.4.12\"\n===\nadds\n  Scenario: grows\n===\n[doctest] unskipped test cases passing the current filters: 2\n");
        check(listedDoctest == std::vector<std::string>({"adds", "  Scenario: grows"}));
    }

    // The arguments after `--`: the filter escaped by the framework's rules, and the report.
    {
        const auto g = testing::run_args(fw("gtest"), {"Math.Adds", "Inst/Param.Works/0"}, "/r/out");
        check(g == std::vector<std::string>({"--gtest_filter=Math.Adds:*/Math.Adds/*:Math/*.Adds:Inst/Param.Works/0", "--gtest_output=json:/r/out"}));
        const auto c = testing::run_args(fw("catch2"), {"a, b", "x[1]"}, "/r/out");
        check(c == std::vector<std::string>({"a\\, b,x\\[1\\]", "--reporter", "json::out=/r/out"}));
        const auto d = testing::run_args(fw("doctest"), {}, "/r/out");
        check(d == std::vector<std::string>({"--reporters=junit", "--out=/r/out"}));
    }

    // Reports.
    {
        const auto g = testing::read_report(fw("gtest"), R"({"testsuites": [{"name": "Math", "testsuite": [
            {"name": "Adds", "status": "RUN", "result": "COMPLETED", "time": "0.002s"},
            {"name": "Spans", "status": "RUN", "result": "COMPLETED", "time": "0s", "failures": [{"failure": "a.cpp:3\nExpected 2"}]},
            {"name": "Later", "status": "RUN", "result": "SKIPPED", "time": "0s"}]}]})");
        check(g.size() == 3 && g[0].name == "Math.Adds" && g[0].status == "passed" && g[0].durationMs == 2);
        check(g[1].status == "failed" && g[1].message.find("Expected 2") != std::string::npos);
        check(g[2].status == "skipped");
        const auto c = testing::read_report(fw("catch2"), R"({"version": 1, "test-run": {"test-cases": [
            {"test-info": {"name": "adds"}, "runs": [], "totals": {"assertions": {"passed": 1, "failed": 0}}},
            {"test-info": {"name": "grows"}, "runs": [{"path": [{"kind": "assertion", "status": false, "source-location": {"filename": "g.cpp", "line": 7}}]}],
             "totals": {"assertions": {"passed": 0, "failed": 1}}}]}})");
        check(c.size() == 2 && c[0].status == "passed" && c[1].status == "failed" && c[1].message.starts_with("g.cpp:7"));
        const auto d = testing::read_report(fw("doctest"), R"(<?xml version="1.0"?>
<testsuites><testsuite name="t" tests="3">
<testcase classname="t.cpp" name="adds" time="0.001" status="run"/>
<testcase classname="t.cpp" name="a &amp; b" time="0" status="run"><failure message="check( 1 == 2 ) is NOT correct!" type="CHECK"/></testcase>
<testcase classname="t.cpp" name="later" time="0" status="skipped"/>
</testsuite></testsuites>)");
        check(d.size() == 3 && d[0].status == "passed" && d[0].durationMs == 1);
        check(d[1].name == "a & b" && d[1].status == "failed" && d[1].message.find("NOT correct") != std::string::npos);
        check(d[2].status == "skipped");

        // Each case's own output, by GoogleTest's boundaries (D16); none for a framework without them.
        const auto outputs = testing::case_outputs(fw("gtest"), "Running main() from gtest_main.cc\n"
            "[==========] Running 3 tests from 1 test suite.\n"
            "[ RUN      ] Math.Adds\nadding\n[       OK ] Math.Adds (2 ms)\n"
            "[ RUN      ] Math.Spans\na.cpp:3: Failure\nExpected 2\n[  FAILED  ] Math.Spans (0 ms)\n"
            "[ RUN      ] Math.Crashes\nhalf way\n");
        check(outputs.size() == 3);
        check(outputs.at("Math.Adds") == "[ RUN      ] Math.Adds\nadding\n[       OK ] Math.Adds (2 ms)\n");
        check(outputs.at("Math.Spans").contains("a.cpp:3: Failure") && outputs.at("Math.Spans").ends_with("[  FAILED  ] Math.Spans (0 ms)\n"));
        check(outputs.at("Math.Crashes") == "[ RUN      ] Math.Crashes\nhalf way\n");
        check(testing::case_outputs(fw("catch2"), "[ RUN      ] x\n").empty());
    }

    // source/entries and test/cases over a project on disk (no mcpp: the plan's packages are not
    // known, so no framework is claimed, and the static entries still answer).
    {
        const auto base = testsupport::temp() / std::format("mcppide-testing-{}", std::random_device{}());
        std::filesystem::create_directories(base / "src");
        std::filesystem::create_directories(base / "tests" / "unit");
        std::ofstream(base / "mcpp.toml") << "[package]\nname = \"p\"\nversion = \"0.1.0\"\n";
        std::ofstream(base / "src" / "main.cpp") << "import std;\n\nint main() { return 0; }\n";
        std::ofstream(base / "src" / "lib.cpp") << "int f() { return 1; }\n";
        std::ofstream(base / "tests" / "unit" / "math.cpp") << "#include <gtest/gtest.h>\nTEST(Math, Adds) {}\n";
        rpc::Server server{[](std::string_view) { return true; }};
        server::Session session(server);
        session.install();
        std::vector<Json> answers;
        rpc::Server wire{[&](std::string_view frame) {
            rpc::FrameReader r;
            r.feed(frame);
            while (auto body = r.next()) answers.push_back(Json::parse(*body));
            return true;
        }};
        server::Session s2(wire);
        s2.install();
        auto call = [&](std::string_view method, Json params) {
            answers.clear();
            wire.dispatch(Json{{"jsonrpc", "2.0"}, {"id", 1}, {"method", method}, {"params", std::move(params)}}.dump());
            for (int i = 0; i < 2000 && answers.empty(); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(5));
            check(!answers.empty() && answers.back().contains("result"));
            return answers.back()["result"];
        };
        (void)call("initialize", Json{{"clientInfo", {{"name", "test"}}}, {"workspaceFolders", Json::array({Json{{"uri", testsupport::file_uri(base)}}})},
                {"initializationOptions", Json{{"editor", "vscode"}, {"trusted", false},
                                      {"settings", {{"mcpp.path", "/no/such/mcpp"}}}}}});
        auto program = call("mcpp-ide/source/entries", Json{{"file", (base / "src" / "main.cpp").generic_string()}});
        check(program["kind"] == "program" && program["main"]["line"] == 2 && program["main"]["approximate"] == true);
        check(call("mcpp-ide/source/entries", Json{{"file", (base / "src" / "lib.cpp").generic_string()}})["kind"] == "none");
        auto test = call("mcpp-ide/source/entries", Json{{"file", (base / "tests" / "unit" / "math.cpp").generic_string()}});
        check(test["kind"] == "test" && test["test"] == "unit/math" && !test.contains("framework"));
        auto cases = call("mcpp-ide/test/cases", Json{{"root", base.generic_string()}, {"test", "unit/math"}});
        check(cases["framework"].is_null() && cases["cases"].empty());
        std::error_code ignored;
        std::filesystem::remove_all(base, ignored);
    }

    std::println("test_testing: ok");
    return 0;
}
