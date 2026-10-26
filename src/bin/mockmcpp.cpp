// A stand-in for mcpp in the tests: answers mcpp's control contract from the
// golden fixtures (protocol/fixtures, captured from a real mcpp by
// tools/capture-fixtures.sh). `@ROOT@` in a fixture becomes the directory the
// command runs in, `@HOME@` the home.
//
//   MCPPIDE_FIXTURES     the fixtures directory (required)
//   MOCK_MCPP_FAIL       "build": the build fails with the compiler's diagnostic;
//                        "manifest": it fails on the manifest
//   MOCK_MCPP_SLOW_MS    the build waits this long before it ends, so a test can
//                        cancel it; SIGTERM then leaves the record `cancelled`
//   MOCK_MCPP_LOG        every argv is appended here, one line each
import std;
import mcpplibs.os;

namespace {

std::string env(const char* name) {
    const char* v = std::getenv(name);
    return v ? std::string(v) : std::string{};
}

std::string slurp(const std::filesystem::path& p) {
    std::ifstream in(p, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), {});
}

std::string replace_all(std::string s, std::string_view from, std::string_view to) {
    for (auto pos = s.find(from); pos != std::string::npos; pos = s.find(from, pos + to.size())) s.replace(pos, from.size(), to);
    return s;
}

std::string fixture(std::string_view name) {
    const auto dir = env("MCPPIDE_FIXTURES");
    auto text = slurp(std::filesystem::path(dir) / name);
    std::error_code ec;
    text = replace_all(std::move(text), "@ROOT@", std::filesystem::current_path(ec).generic_string());
    text = replace_all(std::move(text), "@HOME@", env("HOME"));
    return text;
}

void write_record(std::string_view status) {
    std::error_code ec;
    const auto root = std::filesystem::current_path(ec);
    const auto out = root / "target" / "mock-config";
    std::filesystem::create_directories(out, ec);
    auto record = fixture("result-ok.json");
    record = replace_all(record, "\"status\": \"ok\"", std::format("\"status\": \"{}\"", status));
    std::ofstream(out / "result.json", std::ios::binary) << record;
    std::ofstream(root / "target" / ".last-result.json", std::ios::binary)
        << std::format(R"({{"schemaVersion":1,"kind":"mcpp.last-result","kindVersion":1,"data":{{"result":"{}","status":"{}","finishedAt":null}}}})",
                       (out / "result.json").generic_string(), status);
}

// As mcpp does: a build asked to end leaves its record `cancelled`.
void on_term() {
    write_record("cancelled");
    std::quick_exit(143);
}

int build(bool message) {
    const auto fail = env("MOCK_MCPP_FAIL");
    std::string stream = fixture(fail == "build" ? "build-failed.ndjson"
                                 : fail == "manifest" ? "build-manifest-error.ndjson" : "build-ok.ndjson");
    std::println(std::cerr, "   Compiling app v0.1.0 (.)");
    if (const auto slow = env("MOCK_MCPP_SLOW_MS"); !slow.empty()) {
        write_record("running");
        // A request to end (SIGTERM, a console control event) is taken over and answered as mcpp
        // answers it: the record says `cancelled` (mcpplibs.os.interrupt).
        if (mcpplibs::os::interrupt::wait(std::chrono::milliseconds(std::stoi(slow)))) on_term();
    }
    if (message) std::cout << stream << std::flush;
    if (fail.empty()) { write_record("ok"); std::println(std::cerr, "    Finished dev [unoptimized + debuginfo] in 0.01s"); return 0; }
    if (fail == "build") write_record("failed");
    return 1;
}

}  // namespace

int main(int argc, char** argv) {
    std::vector<std::string> args(argv + 1, argv + argc);
    if (const auto log = env("MOCK_MCPP_LOG"); !log.empty()) {
        std::ofstream out(log, std::ios::app);
        for (const auto& a : args) out << a << ' ';
        out << '\n';
    }
    auto has = [&](std::string_view a) { return std::ranges::find(args, a) != args.end(); };
    auto first = [&](std::size_t i) { return i < args.size() ? args[i] : std::string{}; };
    if (args.empty()) return 2;
    if (first(0) == "--version") { std::cout << fixture("version.txt"); return 0; }
    if (first(0) == "--protocol-version") { std::cout << fixture("protocol-version.json"); return 0; }
    if (first(0) == "build") return build(has("--message-format"));
    if (first(0) == "test") { std::cout << fixture("test.ndjson"); return 0; }
    if (first(0) == "run" && has("--print-launch")) { std::cout << fixture("launch.json"); return 0; }
    if (first(0) == "search") { std::cout << fixture("search.json"); return 0; }
    if (first(0) == "index" && first(1) == "list") { std::cout << fixture("index-list.json"); return 0; }
    if (first(0) == "pack" && has("--list-formats")) { std::cout << fixture("pack-formats.json"); return 0; }
    if (first(0) == "toolchain" && first(1) == "list") { std::cout << fixture("toolchain-list.json"); return 0; }
    if (first(0) == "cache" && first(1) == "list") { std::cout << fixture("cache-list.json"); return 0; }
    if (first(0) == "emit") {
        if (env("MOCK_MCPP_FAIL") == "manifest") {
            std::cout << replace_all(fixture("build-manifest-error.ndjson"), "\"mcpp.build\"", "\"mcpp.build-database\"");
            return 1;
        }
        std::cout << fixture("build-database.json");
        return 0;
    }
    if (first(0) == "self" && first(1) == "env") { std::cout << fixture("self-env.json"); return 0; }
    // Everything else is a command a person reads: say what ran.
    std::string line;
    for (const auto& a : args) { if (!line.empty()) line += ' '; line += a; }
    std::println(std::cerr, "mock mcpp: {}", line);
    return 0;
}
