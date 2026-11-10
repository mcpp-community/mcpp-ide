// The mcppls handshake: the version is read from either spelling of `--version`, 0.0.18 and later
// read the result record, an older one is `outdated`, and output with no version is `unreadable`. A
// payload directory stands for the command its payload.json's `server` names: the runner and the okb
// (mcppls 0.0.18 and later), or a program.
import std;
import mcppide.testsupport;
import mcpplibs.mtest;
import mcppide.languageservice;

using testsupport::check;
using namespace mcpplibs::mtest;

using namespace mcppide;
namespace ls = mcppide::languageservice;

E2E languageservice{"languageservice", [] {
    check(ls::version_in("0.0.16\n") == "0.0.16");
    check(ls::version_in("mcppls 0.0.18\n") == "0.0.18");
    check(ls::version_in("mcppls 0.0.19,\n") == "0.0.19");
    check(ls::version_in("mcppls\n").empty());
    check(ls::classify("/x/mcppls", "PATH", "0.0.16\n").state == ls::State::outdated);
    check(ls::classify("/x/mcppls", "PATH", "mcppls 0.0.18\n").state == ls::State::ok);
    check(ls::classify("/x/mcppls", "PATH", "0.1.0").state == ls::State::ok);
    // mcppls 0.21 numbers a series and a patch (0.0.21 is released as 0.21.0): a version like any other.
    check(ls::version_in("mcppls 0.21.0\n") == "0.21.0");
    check(ls::classify("/x/mcppls", "PATH", "mcppls 0.21.0\n").state == ls::State::ok);
    check(ls::classify("/x/mcppls", "PATH", "usage: mcppls").state == ls::State::unreadable);
    // A candidate that does not exist is passed over; with nothing found the state is `missing` or
    // whatever the machine's own mcppls answers -- never an error.
    const auto s = ls::probe({"/no/such/mcppls"});
    check(s.program != std::filesystem::path("/no/such/mcppls"));
    check(ls::state_name(ls::State::outdated) == "outdated");

    const auto payload = testsupport::temp() / std::format("mcppide-payload-{}", std::random_device{}());
    std::filesystem::create_directories(payload / "bin");
    auto write = [&](const std::filesystem::path& file, std::string_view text) { std::ofstream(file, std::ios::binary) << text; };
    write(payload / "bin" / "okrun", "runner");
    write(payload / "bin" / "mcppls.okb", "okb");
    write(payload / "payload.json", R"({"platform": "linux-x64", "server": {"path": "bin/mcppls.okb", "runner": "bin/okrun"}})");
    const std::vector<std::filesystem::path> imaged{payload / "bin" / "okrun", payload / "bin" / "mcppls.okb"};
    check(ls::payload_command(payload) == imaged);
    check(ls::command_of(payload) == imaged);
    check(ls::command_of(payload / "bin" / "okrun") == std::vector<std::filesystem::path>{payload / "bin" / "okrun"});
    std::filesystem::remove(payload / "bin" / "mcppls.okb");
    check(ls::payload_command(payload).empty());   // what it names is not there
    write(payload / "bin" / "mcppls", "program");
    write(payload / "payload.json", R"({"server": {"path": "bin/mcppls"}})");
    check(ls::payload_command(payload) == std::vector<std::filesystem::path>{payload / "bin" / "mcppls"});
    // What payload.json names stays inside the payload.
    const std::filesystem::path outside = payload.string() + "-outside";
    write(outside, "program");
    write(payload / "payload.json", R"({"server": {"path": "bin/mcppls", "runner": "../)" + outside.filename().string() + R"("}})");
    check(ls::payload_command(payload).empty());
    write(payload / "payload.json", R"({"server": {"path": ")" + (payload / "bin" / "mcppls").generic_string() + R"("}})");
    check(ls::payload_command(payload).empty());
    std::filesystem::remove(outside);
    write(payload / "payload.json", "not json");
    check(ls::payload_command(payload).empty());
    check(ls::command_of(payload / "missing").empty());
    // mcppls S6: version 5 names each platform's runner; version 4 serves the one platform it names. A
    // payload that does not serve this system is refused, naming the platforms it does serve.
    const auto host = ls::host_platform();
    check(host == "linux-x64" || host == "linux-arm64" || host == "darwin-arm64" || host == "darwin-x64" ||
          host == "win32-x64" || host == "win32-arm64");
    std::filesystem::create_directories(payload / "libexec" / "okrun" / "this");
    write(payload / "libexec" / "okrun" / "this" / "okrun", "runner");
    write(payload / "bin" / "mcppls.okb", "okb");
    write(payload / "payload.json", std::format(R"({{"payload-version": 5, "server": {{"path": "bin/mcppls.okb", "version": "0.0.20"}},
        "kit": {{"name": "k", "path": "kit"}}, "files": {{}},
        "platforms": {{"{}": {{"runner": "libexec/okrun/this/okrun", "engines": {{}}, "files": {{}}}},
                      "haiku-x64": {{"runner": "libexec/okrun/haiku/okrun", "engines": {{}}, "files": {{}}}}}}}})", host));
    const std::vector<std::filesystem::path> fromEntry{payload / "libexec" / "okrun" / "this" / "okrun", payload / "bin" / "mcppls.okb"};
    check(ls::payload_command(payload) == fromEntry);
    const auto other = ls::payload_server(payload, "plan9-x64");
    check(other.command.empty() && other.problem.contains("serves") && other.problem.contains("haiku-x64") && other.problem.contains("plan9-x64"));
    write(payload / "payload.json", R"({"payload-version": 4, "platform": "plan9-x64", "server": {"path": "bin/mcppls.okb", "runner": "bin/okrun"}})");
    write(payload / "bin" / "okrun", "runner");
    check(ls::payload_command(payload).empty());
    check(ls::payload_server(payload).problem.contains("plan9-x64"));
    write(payload / "payload.json", std::format(R"({{"payload-version": 4, "platform": "{}", "server": {{"path": "bin/mcppls.okb", "runner": "bin/okrun"}}}})", host));
    check(ls::payload_command(payload) == imaged);
    write(payload / "payload.json", R"({"payload-version": 6, "server": {"path": "bin/mcppls.okb"}})");
    check(ls::payload_server(payload).problem.contains("payload-version 6"));
    // The manifests of the published payloads (tests/e2e/payloads: mcppls 0.0.19's for linux-x64, version 4,
    // and 0.0.20's, version 5, as mcppls's S6 examples hold them, their file lists cut short).
    "the published payloads"_test = [&] {
        const auto published = [&](std::string_view name) {
            std::ifstream in(testsupport::repo() / "tests" / "e2e" / "payloads" / std::string(name), std::ios::binary);
            return std::string(std::istreambuf_iterator<char>(in), {});
        };
        const auto served = testsupport::temp() / std::format("mcppide-published-{}", std::random_device{}());
        std::filesystem::create_directories(served / "bin");
        for (const auto* system : {"linux-x86_64", "linux-aarch64", "macos-aarch64", "windows-x86_64"}) {
            std::filesystem::create_directories(served / "libexec" / "okrun" / system);
            write(served / "libexec" / "okrun" / system / (std::string(system).starts_with("windows") ? "okrun.exe" : "okrun"), "runner");
        }
        write(served / "bin" / "mcppls.okb", "okb");
        write(served / "bin" / "okrun", "runner");
        write(served / "payload.json", published("mcppls-0.0.20.json"));
        const std::pair<std::string_view, std::string_view> runners[] = {
            {"linux-x64", "linux-x86_64/okrun"}, {"linux-arm64", "linux-aarch64/okrun"},
            {"darwin-arm64", "macos-aarch64/okrun"}, {"win32-x64", "windows-x86_64/okrun.exe"}};
        for (const auto& [platform, runner] : runners) {
            const auto s = ls::payload_server(served, platform);
            check(s.command == std::vector<std::filesystem::path>({served / "libexec" / "okrun" / std::filesystem::path(runner), served / "bin" / "mcppls.okb"}));
        }
        check(ls::payload_server(served, "win32-arm64").problem.contains("darwin-arm64, linux-arm64, linux-x64, win32-x64"));
        write(served / "payload.json", published("mcppls-0.0.19-linux-x64.json"));
        check(ls::payload_server(served, "linux-x64").command == std::vector<std::filesystem::path>({served / "bin" / "okrun", served / "bin" / "mcppls.okb"}));
        check(ls::payload_server(served, "darwin-arm64").problem.contains("serves linux-x64"));
        std::filesystem::remove_all(served);
    };
    // A refused payload named by the adapter is said, when nothing else is found.
    write(payload / "payload.json", R"({"payload-version": 5, "server": {"path": "bin/mcppls.okb"}, "platforms": {"plan9-x64": {"runner": "bin/okrun"}}})");
    const auto refused = ls::candidates({payload.string()});
    check(!refused.empty() && refused.front().command.empty() && refused.front().problem.contains("plan9-x64"));
    // The adapter's payload is the candidate asked first, ahead of PATH and xlings.
    write(payload / "payload.json", R"({"server": {"path": "bin/mcppls"}})");
    const auto found = ls::candidates({payload.string()});
    check(!found.empty() && found.front().source == "editor" && found.front().command.back() == payload / "bin" / "mcppls");
    std::filesystem::remove_all(payload);
}};
