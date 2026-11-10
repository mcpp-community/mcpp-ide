// What the editors read of a test program (mcpp test spec, mcpp docs/specs/test.md): where a source
// starts (`main`, never inside a comment, a string or a raw string), a program's cases as it lists
// them (B3), each case's outcome with the details of its checks (O3) and its own output (O8), and the
// backend's source/entries and test/cases over a project on disk. No test framework is known.
import std;
import mcppide.testsupport;
import mcpplibs.mtest;
import nlohmann.json;
import mcppide.scan;
import mcppide.testing;
import mcppide.rpc;
import mcppide.server;

using testsupport::check;
using namespace mcpplibs::mtest;

using namespace mcppide;
using Json = nlohmann::json;

UTest sources{"sources", [] {
    // main: the first `int main(` or `auto main(` at file scope, outside comments and literals.
    "main is found outside comments and literals"_test = [&] {
        const auto r = scan::scan(R"(// int main() in a comment
/* int main() */
const char* s = "int main(";
const char* raw = R"x(int main()
)x";
#define X int main(
namespace n { int main(); }
int
main(int argc, char** argv) { return 0; }
)");
        check(r.mainLine && *r.mainLine == 8);
        check(!scan::scan("void f() { obj.main(); std::main(); }").mainLine);
        check(scan::scan("export auto main() -> int { return 0; }").mainLine == 0);
    };

    // A program's cases as it lists them and as its records report them (the test protocol).
    "cases as a program lists and reports them"_test = [&] {
        const auto record = Json::parse(R"({"test":"parse","protocol":1,
            "listed":[{"id":"parsing/an empty line","file":"tests/parse.cpp","line":5}],
            "cases":[{"id":"a","status":"pass","duration_ms":1.5,"output":"said by a\n"},
                     {"id":"b","status":"fail","duration_ms":2,"checks":{"held":3,"failed":1},
                      "failures":[{"file":"tests/parse.cpp","line":9,"message":"1 == 2","expected":"2","actual":"1",
                                   "source":"expect(eq(x, 2));","fatal":true}]},
                     {"id":"c","status":"skip","duration_ms":0,"reason":"later"},
                     {"id":"d","status":"xfail","duration_ms":0}]})");
        const auto listed = testing::protocol_listing(record);
        check(listed.size() == 1 && listed[0].id == "parsing/an empty line" && listed[0].line == 4);
        const auto outcomes = testing::protocol_outcomes(record);
        check(outcomes.size() == 4);
        check(outcomes.size() == 4 && outcomes[0].outcome.status == "passed" && outcomes[0].outcome.durationMs == 1);
        check(outcomes.size() == 4 && outcomes[0].detail["output"] == "said by a\n");
        check(outcomes.size() == 4 && outcomes[1].outcome.status == "failed" && outcomes[1].location["line"] == 8
              && outcomes[1].outcome.message.contains("expected 2, actual 1"));
        check(outcomes.size() == 4 && outcomes[1].detail["checks"]["failed"] == 1
              && outcomes[1].detail["failures"][0]["source"] == "expect(eq(x, 2));" && outcomes[1].detail["failures"][0]["fatal"] == true);
        check(outcomes.size() == 4 && outcomes[2].outcome.status == "skipped" && outcomes[2].outcome.message == "later");
        check(outcomes.size() == 4 && outcomes[3].outcome.status == "passed");
    };

    // source/entries and test/cases over a project on disk (no mcpp: nothing is listed, and the
    // entries still answer).
    "source entries and cases on disk"_test = [&] {
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
        check(cases["framework"].is_null() && cases["protocol"].is_null() && cases["cases"].empty());
        std::error_code ignored;
        std::filesystem::remove_all(base, ignored);
    };
}};
