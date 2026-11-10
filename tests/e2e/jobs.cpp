// mcppide.jobs over the mock mcpp: a build streams its records and ends `ok`, a failed build ends
// `failed` with its diagnostics, and a cancelled one ends `cancelled` with mcpp's record saying so.
import std;
import mcppide.testsupport;
import mcpplibs.mtest;
import nlohmann.json;
import mcpplibs.os;
import mcppide.mcpp;
import mcppide.jobs;

using testsupport::check;
using namespace mcpplibs::mtest;

namespace jobs = mcppide::jobs;
using Json = nlohmann::json;

namespace {
struct Seen {
    std::mutex lock;
    std::vector<Json> records;
    std::map<std::string, Json> finished;
};

}  // namespace

E2E jobQueue{"jobs", [] {
    const auto mock = testsupport::built("mcpp-ide-mock-mcpp");
    check(!mock.empty());
    // The mock reads its fixtures and its behaviour from the environment of each run.
    const mcppide::mcpp::Environment fixtures{{"MCPPIDE_FIXTURES", testsupport::fixtures().generic_string()}};
    auto protocol = mcppide::mcpp::handshake(mock, fixtures);
    check(protocol.has_value() && protocol->contract);

    const auto root = testsupport::temp() / std::format("mcppide-jobs-{}", mcpplibs::os::process::self()->id);
    std::filesystem::create_directories(root);
    Seen seen;
    jobs::Sink sink;
    sink.record = [&](std::string_view, const Json& r) { std::lock_guard g(seen.lock); seen.records.push_back(r); };
    sink.finished = [&](std::string_view id, const Json& s) { std::lock_guard g(seen.lock); seen.finished[std::string(id)] = s; };
    jobs::Jobs runner(mcppide::mcpp::Client(mock, *protocol), sink);

    jobs::Request build{.kind = jobs::Kind::build, .root = root, .args = {"build", "--message-format", "json"}, .mode = jobs::Mode::ndjson};
    build.env = fixtures;
    const auto ok = runner.submit(build);
    runner.drain();
    {
        std::lock_guard g(seen.lock);
        check(seen.finished.contains(ok));
        check(seen.finished[ok]["status"] == "ok");
        check(!seen.records.empty());
        if (!seen.finished[ok].contains("envelope")) {
            std::println(std::cerr, "no envelope; the summary: {}", seen.finished[ok].dump());
            for (const auto& r : seen.records) std::println(std::cerr, "record: {}", r.dump().substr(0, 200));
        }
        check(seen.finished[ok].contains("envelope"));
    }

    auto failing = build;
    failing.env.push_back({"MOCK_MCPP_FAIL", "build"});
    const auto failed = runner.submit(failing);
    runner.drain();
    {
        std::lock_guard g(seen.lock);
        check(seen.finished[failed]["status"] == "failed");
    }

    // A build that takes its time, cancelled while it runs.
    auto slowly = build;
    slowly.env.push_back({"MOCK_MCPP_SLOW_MS", "20000"});
    const auto slow = runner.submit(slowly);
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    check(runner.cancel(slow));
    runner.drain();
    {
        std::lock_guard g(seen.lock);
        check(seen.finished[slow]["status"] == "cancelled");
    }
    // mcpp leaves its record `cancelled` when the system delivers the request to end (SIGTERM). On
    // Windows the program is ended outright (openkal-windows: TerminateProcess), so the record is
    // the last one mcpp wrote, never `ok`.
    const auto record = testsupport::read(root / "target" / ".last-result.json");
    if (mcpplibs::os::host() == mcpplibs::os::platform::windows) check(record.find("\"ok\"") == std::string::npos);
    else check(record.find("\"cancelled\"") != std::string::npos);

    // Tests run beside each other (design 2026-11-10 supplement, T14): a test asked for while
    // another of the project runs starts at once, and cancelling one leaves the other running.
    jobs::Request first{.kind = jobs::Kind::test, .root = root, .args = {"test", "--message-format", "json"}, .mode = jobs::Mode::ndjson};
    first.env = fixtures;
    first.env.push_back({"MOCK_MCPP_SLOW_MS", "20000"});
    auto second = first;
    const auto a = runner.submit(first);
    const auto b = runner.submit(second);
    bool both = false;
    for (int i = 0; i < 200 && !both; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        const auto list = runner.list();
        both = std::ranges::count_if(list, [](const Json& j) { return j.value("state", "") == "running"; }) == 2;
    }
    check(both);
    check(runner.cancel(a));
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    {
        const auto list = runner.list();
        check(std::ranges::any_of(list, [&](const Json& j) { return j.value("id", "") == b && j.value("state", "") == "running"; }));
    }
    check(runner.cancel(b));
    runner.drain();
    {
        std::lock_guard g(seen.lock);
        check(seen.finished[a]["status"] == "cancelled" && seen.finished[b]["status"] == "cancelled");
    }

    std::error_code ec;
    std::filesystem::remove_all(root, ec);
}};
