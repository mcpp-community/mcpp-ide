// mcppide.testsupport: what every backend test shares -- a check that ends its case where it failed,
// and the paths of the fixtures and the built programs. A module, as the backend's own units are: no
// test includes a header (mcpp-ide-dev check-sources). The tests are mtest's (tests/unit, tests/e2e,
// tests/cts, tests/bench; mtest docs/layout.md).
export module mcppide.testsupport;

import std;
import mcpplibs.mtest;

export namespace testsupport {

// A check is mtest's fatal expectation: a failure is reported at the check's own place, and the
// case ends there (the checks after it read what it checked).
inline void check(bool cond, std::source_location at = std::source_location::current()) {
    mcpplibs::mtest::expect(mcpplibs::mtest::fatal(cond), at);
}

// A name in the generic spelling: `/` between components. The tests run as ok images too, whose
// names are `/`-separated on every system, while a Windows host spells this file's own path and the
// temporary directory with `\`.
inline std::filesystem::path generic(std::string text) {
    for (auto& c : text) if (c == '\\') c = '/';
    return std::filesystem::path(text);
}
// The repository, from this file's own path (testsupport/src/testsupport.cppm). A build that names a
// source from its package's root (the ok images') gives a relative name; the tests run in the
// repository, so it is taken from there. Either way the answer is absolute: a test starts programs in
// directories of its own.
inline std::filesystem::path repo() {
    const auto named = generic(std::source_location::current().file_name());
    if (named.is_absolute()) return named.parent_path().parent_path().parent_path();
    return generic(std::filesystem::current_path().string());
}
// The temporary directory, in the generic spelling.
inline std::filesystem::path temp() { return generic(std::filesystem::temp_directory_path().string()); }
// A path as the `file:` URI an LSP client names it by.
inline std::string file_uri(const std::filesystem::path& p) {
    const auto g = p.generic_string();
    return "file://" + std::string(g.starts_with("/") ? "" : "/") + g;
}
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
