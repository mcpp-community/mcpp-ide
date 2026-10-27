// A stand-in for mcpp in the tests: answers mcpp's control contract from the
// golden fixtures (protocol/fixtures, captured from a real mcpp by
// `mcpp-ide-dev capture`). `@ROOT@` in a fixture becomes the directory the
// command runs in, `@HOME@` the home.
//
//   MCPPIDE_FIXTURES     the fixtures directory (default: the repository's protocol/fixtures)
//   MOCK_MCPP_FAIL       "build": the build fails with the compiler's diagnostic;
//                        "manifest": it fails on the manifest (also read from the
//                        project's `.mock-mcpp-fail`)
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

// What mcpp prints, as bytes: the standard streams of mcpplibs.os translate nothing (a C++ stream on
// Windows writes every \n as \r\n), and each write is complete when it returns.
void out(std::string_view text) {
    (void)mcpplibs::os::stdio::write_output(text);
    (void)mcpplibs::os::stdio::flush_output();
}
void err(std::string_view text) { (void)mcpplibs::os::stdio::write_error(text); }

std::string replace_all(std::string s, std::string_view from, std::string_view to) {
    for (auto pos = s.find(from); pos != std::string::npos; pos = s.find(from, pos + to.size())) s.replace(pos, from.size(), to);
    return s;
}

// The fixtures: MCPPIDE_FIXTURES, else the repository's, found above this program (it is built
// under target/ of the repository).
std::filesystem::path fixtures_dir() {
    if (auto d = env("MCPPIDE_FIXTURES"); !d.empty()) return d;
    std::error_code ec;
    for (auto d = mcpplibs::os::env::image().value_or(std::filesystem::path{}).parent_path(); !d.empty(); d = d.parent_path()) {
        if (std::filesystem::is_directory(d / "protocol" / "fixtures", ec)) return d / "protocol" / "fixtures";
        if (d == d.parent_path()) break;
    }
    return {};
}

// A path as it stands inside a JSON string of a fixture: `/`-separated, and escaped. (A Windows
// runner's HOME is `C:\Users\...`, whose backslashes are escapes inside JSON.)
std::string json_path(const std::filesystem::path& p) {
    std::string out;
    for (char c : p.generic_string()) {
        if (c == '\\' || c == '"') out += '\\';
        out += c;
    }
    return out;
}

std::string fixture(std::string_view name) {
    const auto dir = fixtures_dir();
    auto text = slurp(std::filesystem::path(dir) / name);
    std::error_code ec;
    text = replace_all(std::move(text), "@ROOT@", json_path(std::filesystem::current_path(ec)));
    text = replace_all(std::move(text), "@HOME@", json_path(env("HOME")));
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
    std::_Exit(143);
}

// The failure to play: MOCK_MCPP_FAIL, else the content of `.mock-mcpp-fail` in the project (an
// editor's test cannot change the environment of a backend it did not start).
std::string failure() {
    if (auto f = env("MOCK_MCPP_FAIL"); !f.empty()) return f;
    std::error_code ec;
    auto text = slurp(std::filesystem::current_path(ec) / ".mock-mcpp-fail");
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) text.pop_back();
    return text;
}

int build(bool message) {
    const auto fail = failure();
    std::string stream = fixture(fail == "build" ? "build-failed.ndjson"
                                 : fail == "manifest" ? "build-manifest-error.ndjson" : "build-ok.ndjson");
    err("   Compiling app v0.1.0 (.)\n");
    if (const auto slow = env("MOCK_MCPP_SLOW_MS"); !slow.empty()) {
        write_record("running");
        // A request to end (SIGTERM, a console control event) is taken over and answered as mcpp
        // answers it: the record says `cancelled` (mcpplibs.os.interrupt).
        if (mcpplibs::os::interrupt::wait(std::chrono::milliseconds(std::stoi(slow)))) on_term();
    }
    if (message) out(stream);
    if (fail.empty()) { write_record("ok"); err("    Finished dev [unoptimized + debuginfo] in 0.01s\n"); return 0; }
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
    if (first(0) == "--version") { out(fixture("version.txt")); return 0; }
    if (first(0) == "--protocol-version") { out(fixture("protocol-version.json")); return 0; }
    if (first(0) == "build") return build(has("--message-format"));
    if (first(0) == "test") { out(fixture(has("--list") ? "test-list.ndjson" : "test.ndjson")); return 0; }
    if (first(0) == "run" && has("--print-launch")) { out(fixture("launch.json")); return 0; }
    if (first(0) == "search") { out(fixture("search.json")); return 0; }
    if (first(0) == "index" && first(1) == "list") { out(fixture("index-list.json")); return 0; }
    if (first(0) == "pack" && has("--list-formats")) { out(fixture("pack-formats.json")); return 0; }
    if (first(0) == "toolchain" && first(1) == "list") { out(fixture("toolchain-list.json")); return 0; }
    if (first(0) == "cache" && first(1) == "list") { out(fixture("cache-list.json")); return 0; }
    if (first(0) == "emit") {
        if (failure() == "manifest") {
            out(replace_all(fixture("build-manifest-error.ndjson"), "\"mcpp.build\"", "\"mcpp.build-database\""));
            return 1;
        }
        out(fixture("build-database.json"));
        return 0;
    }
    if (first(0) == "self" && first(1) == "env") { out(fixture("self-env.json")); return 0; }
    // Everything else is a command a person reads: say what ran.
    std::string line;
    for (const auto& a : args) { if (!line.empty()) line += ' '; line += a; }
    err(std::format("mock mcpp: {}\n", line));
    return 0;
}
