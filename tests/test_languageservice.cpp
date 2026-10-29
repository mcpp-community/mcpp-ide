// The mcppls handshake: the version is read from either spelling of `--version`, 0.0.18 and later
// read the result record, an older one is `outdated`, and output with no version is `unreadable`. A
// payload directory stands for the command its payload.json's `server` names: the runner and the okb
// (mcppls 0.0.18 and later), or a program.
#include "support.hpp"
import std;
import mcppide.languageservice;

using namespace mcppide;
namespace ls = mcppide::languageservice;

int main() {
    CHECK(ls::version_in("0.0.16\n") == "0.0.16");
    CHECK(ls::version_in("mcppls 0.0.18\n") == "0.0.18");
    CHECK(ls::version_in("mcppls 0.0.19,\n") == "0.0.19");
    CHECK(ls::version_in("mcppls\n").empty());
    CHECK(ls::classify("/x/mcppls", "PATH", "0.0.16\n").state == ls::State::outdated);
    CHECK(ls::classify("/x/mcppls", "PATH", "mcppls 0.0.18\n").state == ls::State::ok);
    CHECK(ls::classify("/x/mcppls", "PATH", "0.1.0").state == ls::State::ok);
    CHECK(ls::classify("/x/mcppls", "PATH", "usage: mcppls").state == ls::State::unreadable);
    // A candidate that does not exist is passed over; with nothing found the state is `missing` or
    // whatever the machine's own mcppls answers -- never an error.
    const auto s = ls::probe({"/no/such/mcppls"});
    CHECK(s.program != std::filesystem::path("/no/such/mcppls"));
    CHECK(ls::state_name(ls::State::outdated) == "outdated");

    const auto payload = std::filesystem::temp_directory_path() / std::format("mcppide-payload-{}", std::random_device{}());
    std::filesystem::create_directories(payload / "bin");
    auto write = [&](const std::filesystem::path& file, std::string_view text) { std::ofstream(file, std::ios::binary) << text; };
    write(payload / "bin" / "okrun", "runner");
    write(payload / "bin" / "mcppls.okb", "okb");
    write(payload / "payload.json", R"({"platform": "linux-x64", "server": {"path": "bin/mcppls.okb", "runner": "bin/okrun"}})");
    const std::vector<std::filesystem::path> imaged{payload / "bin" / "okrun", payload / "bin" / "mcppls.okb"};
    CHECK(ls::payload_command(payload) == imaged);
    CHECK(ls::command_of(payload) == imaged);
    CHECK(ls::command_of(payload / "bin" / "okrun") == std::vector<std::filesystem::path>{payload / "bin" / "okrun"});
    std::filesystem::remove(payload / "bin" / "mcppls.okb");
    CHECK(ls::payload_command(payload).empty());   // what it names is not there
    write(payload / "bin" / "mcppls", "program");
    write(payload / "payload.json", R"({"server": {"path": "bin/mcppls"}})");
    CHECK(ls::payload_command(payload) == std::vector<std::filesystem::path>{payload / "bin" / "mcppls"});
    // What payload.json names stays inside the payload.
    const std::filesystem::path outside = payload.string() + "-outside";
    write(outside, "program");
    write(payload / "payload.json", R"({"server": {"path": "bin/mcppls", "runner": "../)" + outside.filename().string() + R"("}})");
    CHECK(ls::payload_command(payload).empty());
    write(payload / "payload.json", R"({"server": {"path": ")" + (payload / "bin" / "mcppls").generic_string() + R"("}})");
    CHECK(ls::payload_command(payload).empty());
    std::filesystem::remove(outside);
    write(payload / "payload.json", "not json");
    CHECK(ls::payload_command(payload).empty());
    CHECK(ls::command_of(payload / "missing").empty());
    // The adapter's payload is the candidate asked first, ahead of PATH and xlings.
    write(payload / "payload.json", R"({"server": {"path": "bin/mcppls"}})");
    const auto found = ls::candidates({payload.string()});
    CHECK(!found.empty() && found.front().source == "editor" && found.front().command.back() == payload / "bin" / "mcppls");
    std::filesystem::remove_all(payload);
    std::println("test_languageservice: ok");
}
