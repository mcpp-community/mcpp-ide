// What every backend test shares: a check that says where it failed, and the
// paths of the fixtures and the mock mcpp.
#pragma once
#include <cstdio>
#include <cstdlib>

#define CHECK(cond)                                                                 \
    do {                                                                            \
        if (!(cond)) {                                                              \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            std::exit(1);                                                           \
        }                                                                           \
    } while (0)

#include <filesystem>
#include <fstream>
#include <string>

namespace testsupport {
// The repository, from this file's own path.
inline std::filesystem::path repo() { return std::filesystem::path(__FILE__).parent_path().parent_path(); }
inline std::filesystem::path fixtures() { return repo() / "protocol" / "fixtures"; }
inline std::string read(const std::filesystem::path& p) {
    std::ifstream in(p, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), {});
}
// A program the build produced (the backend, the mock mcpp): newest first.
inline std::filesystem::path built(const std::string& name) {
    std::filesystem::path best;
    std::filesystem::file_time_type bestTime{};
    std::error_code ec;
    for (std::filesystem::recursive_directory_iterator it(repo() / "target", ec), end; !ec && it != end; it.increment(ec)) {
        const auto file = it->path().filename().string();
        if (file != name && file != name + ".exe") continue;
        if (it->path().parent_path().filename() != "bin") continue;
        const auto t = std::filesystem::last_write_time(it->path(), ec);
        if (best.empty() || t > bestTime) { best = it->path(); bestTime = t; }
    }
    return best;
}
}  // namespace testsupport
