// mcppide.jobs over the mock mcpp: a build streams its records and ends `ok`, a failed build ends
// `failed` with its diagnostics, and a cancelled one ends `cancelled` with mcpp's record saying so.
#include "support.hpp"
import std;
import nlohmann.json;
import mcpplibs.os;
import mcppide.mcpp;
import mcppide.jobs;

namespace jobs = mcppide::jobs;
using Json = nlohmann::json;

namespace {
struct Seen {
    std::mutex lock;
    std::vector<Json> records;
    std::map<std::string, Json> finished;
};

}  // namespace

int main() {
    const auto mock = testsupport::built("mcpp-ide-mock-mcpp");
    CHECK(!mock.empty());
    // The mock reads its fixtures and its behaviour from the environment of each run.
    const mcppide::mcpp::Environment fixtures{{"MCPPIDE_FIXTURES", testsupport::fixtures().generic_string()}};
    auto protocol = mcppide::mcpp::handshake(mock, fixtures);
    CHECK(protocol.has_value() && protocol->contract);

    const auto root = std::filesystem::temp_directory_path() / std::format("mcppide-jobs-{}", mcpplibs::os::process::self()->id);
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
        CHECK(seen.finished.contains(ok));
        CHECK(seen.finished[ok]["status"] == "ok");
        CHECK(!seen.records.empty());
        if (!seen.finished[ok].contains("envelope")) {
            std::println(std::cerr, "no envelope; the summary: {}", seen.finished[ok].dump());
            for (const auto& r : seen.records) std::println(std::cerr, "record: {}", r.dump().substr(0, 200));
        }
        CHECK(seen.finished[ok].contains("envelope"));
    }

    auto failing = build;
    failing.env.push_back({"MOCK_MCPP_FAIL", "build"});
    const auto failed = runner.submit(failing);
    runner.drain();
    {
        std::lock_guard g(seen.lock);
        CHECK(seen.finished[failed]["status"] == "failed");
    }

    // A build that takes its time, cancelled while it runs.
    auto slowly = build;
    slowly.env.push_back({"MOCK_MCPP_SLOW_MS", "20000"});
    const auto slow = runner.submit(slowly);
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    CHECK(runner.cancel(slow));
    runner.drain();
    {
        std::lock_guard g(seen.lock);
        CHECK(seen.finished[slow]["status"] == "cancelled");
    }
    const auto record = testsupport::read(root / "target" / ".last-result.json");
    CHECK(record.find("\"cancelled\"") != std::string::npos);

    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::println("test_jobs: ok");
}
