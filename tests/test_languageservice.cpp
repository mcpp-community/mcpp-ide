// The mcppls handshake: the version is read from either spelling of `--version`, 0.0.18 and later
// read the result record, an older one is `outdated`, and output with no version is `unreadable`.
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
    std::println("test_languageservice: ok");
}
