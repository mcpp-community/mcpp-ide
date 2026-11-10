// A test program's cases as mcpp states them (the mcpp test spec, mcpp docs/specs/test.md): the
// programs of `mcpp test --list`, each program's cases as it lists them (B3, `--cases`), and each
// case's outcome from mcpp's records, with the details of its checks (O3) and its own output (O8).
//
// NO FRAMEWORK IS KNOWN HERE (design 2026-11-10 supplement, T14). A program that implements the
// test protocol lists, selects and reports its cases, whatever framework or language wrote it; a
// program that implements none is one case, judged by its exit status. The editors read mcpp's
// records and nothing of a framework's own output, flags or report formats.

export module mcppide.testing;

import std;
import nlohmann.json;

export namespace mcppide::testing {

using Json = nlohmann::json;

// A case as its program lists it (B3, P3): its identifier, its place (0-based line; -1 when the
// program names none), the file that declares it (a program of several files has one per case), its
// kind (`unit`, `e2e`, `cts`, `bench`; empty when it states none), its tags, `fail` when it is
// expected to fail, and the bounds on its metrics ([{metric, max?, min?}]).
struct Case {
    std::string name;
    int line = -1;
    std::string source = "listed";
    std::string file;
    std::string kind;
    std::vector<std::string> tags;
    std::string expect;
    Json bounds = Json::array();
};

// A case as `test/cases` answers it: {name, source, line?, file?, kind?, tags?, expect?, bounds?}.
Json case_json(const Case& c);

// The kind of a case that states none, by mtest's convention of directories (mtest docs/layout.md):
// the first of `unit`, `e2e`, `cts`, `bench` below a `tests` directory in its path; empty otherwise.
std::string kind_of_path(std::string_view file);

struct Outcome {
    std::string name;
    std::string status;   // passed | failed | skipped
    std::int64_t durationMs = 0;
    std::string message;
};


// The cases of a test program that implements the test protocol, as a record of `mcpp test
// --message-format json` states them (mcpp docs/50 §8): `listed` (from `--cases`), each with its
// place (0-based line); and each case's outcome from `cases`, with its first failure's message and
// place.
struct ProtocolCase {
    std::string id;
    std::string file;
    int line = -1;
    std::string kind;
    std::vector<std::string> tags;
    std::string expect;
    Json bounds = Json::array();
};
std::vector<ProtocolCase> protocol_listing(const Json& record);

// A case's status as the editors know it (design 2026-11-10): `pass` passed, `fail` failed, `skip`
// skipped, `xfail` passed as an expected failure, `xpass` failed as an unexpected pass, `invalid` (a
// measurement that cannot be judged, protocol P6.3) errored.
struct CaseStatus {
    std::string status;
    bool expectedFailure = false;
    bool unexpectedPass = false;
};
CaseStatus case_status(std::string_view protocol);

// One case of a record's `cases` (or of a `case` record that ended), as the editors receive it:
// {case, status, durationMs, reason, message, location, failures: [{path, line, message, expected?,
// actual?, source?, fatal?}], and when stated checks {held, failed} (O3), output (O8), metrics,
// bounds, kind, tags, expectedFailure, unexpectedPass}. A line is 0-based; `message` is the first
// failure with its place, else the reason; `location` is the first failure's place, or null.
Json protocol_case(const Json& c);

struct ProtocolOutcome {
    Outcome outcome;
    Json location;   // {path, line} or null
    Json detail;     // protocol_case
};
std::vector<ProtocolOutcome> protocol_outcomes(const Json& record);

// A TEST PROGRAM AS `mcpp test --list` NAMES IT (design 2026-11-10): `unit` for the directory
// tests/unit/ under `[test] programs = "directory"`, `00-a/0` for a file. Its own files, the file
// that defines `main` (none when no file does), and the parts a run divides it into, each a process
// named `<program>/<stem>` (mcpp 2026.11.9.1 runs a protocol program of several files as one process
// per file). mcpp states them with `test-parts` (mcpp#917: `files`, `parts`, `main: null`); before
// it they are derived: a program whose `main` lies in a directory whose path ends in the program's
// name is that directory's sources.
struct Program {
    std::string name;
    std::string member;
    std::string main;
    std::vector<std::string> files;
    std::vector<std::pair<std::string, std::string>> parts;   // {name, file}
    std::string layout = "file";                               // "file" | "directory"
};
Program program_of(const Json& listRecord, const std::function<bool(const std::filesystem::path&)>& definesMain);
// {test, member, main, files, parts: [{name, file}], layout}
Json program_json(const Program& p);
// The program a record of a run belongs to: `partOf` when mcpp states it (mcpp#917), else the
// program of that name, else the longest whose name and a `/` begin it, else the name itself.
std::string owner_of(std::string_view name, std::string_view partOf, const std::vector<Program>& programs);
// The part of `program` a file is, or "".
std::string part_of_file(const Program& program, const std::filesystem::path& file);

}  // namespace mcppide::testing

namespace mcppide::testing {

namespace {

std::vector<std::string> strings(const Json& v) {
    std::vector<std::string> out;
    if (v.is_array()) for (const auto& x : v) if (x.is_string()) out.push_back(x.get<std::string>());
    return out;
}

std::string trim(std::string_view s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.remove_prefix(1);
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.remove_suffix(1);
    return std::string(s);
}

std::vector<std::string> lines(std::string_view text) {
    std::vector<std::string> out;
    std::size_t at = 0;
    while (at <= text.size()) {
        const auto end = text.find('\n', at);
        auto line = text.substr(at, end == std::string_view::npos ? std::string_view::npos : end - at);
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        out.emplace_back(line);
        if (end == std::string_view::npos) break;
        at = end + 1;
    }
    return out;
}

Json bound_json(std::string_view metric, std::optional<double> max, std::optional<double> min) {
    Json b{{"metric", metric}};
    if (max) b["max"] = *max;
    if (min) b["min"] = *min;
    return b;
}

// The bounds a case states (protocol P6.5: [{metric, max?, min?}]), the ones with a metric.
Json bounds_of(const Json& c) {
    Json out = Json::array();
    for (const auto& b : c.value("bounds", Json::array()))
        if (b.is_object() && b.contains("metric") && b["metric"].is_string()) out.push_back(b);
    return out;
}

// A number under either name (mcpp's records say `duration_ms`, the store `ms`), 0 when neither is one.
double number_of(const Json& c, const char* key, const char* other) {
    for (const auto* k : {key, other})
        if (auto it = c.find(k); it != c.end() && it->is_number()) return it->get<double>();
    return 0;
}

// The macros of `f`, with what `extra` adds for it.
}  // namespace

std::vector<ProtocolCase> protocol_listing(const Json& record) {
    std::vector<ProtocolCase> out;
    for (const auto& c : record.value("listed", Json::array())) {
        if (!c.is_object()) continue;
        const int line = c.value("line", 0);
        ProtocolCase one{c.value("id", ""), c.value("file", ""), line > 0 ? line - 1 : -1};
        one.kind = c.value("kind", "");
        one.tags = strings(c.value("tags", Json::array()));
        one.expect = c.value("expect", "");
        one.bounds = bounds_of(c);
        out.push_back(std::move(one));
    }
    return out;
}

CaseStatus case_status(std::string_view protocol) {
    if (protocol == "pass") return {"passed"};
    if (protocol == "skip") return {"skipped"};
    if (protocol == "xfail") return {"passed", true, false};
    if (protocol == "xpass") return {"failed", false, true};
    if (protocol == "invalid") return {"errored"};
    return {"failed"};
}

Json protocol_case(const Json& c) {
    const auto status = case_status(c.value("status", ""));
    Json out{{"case", c.value("id", "")}, {"status", status.status},
             {"durationMs", static_cast<std::int64_t>(number_of(c, "duration_ms", "ms"))},
             {"reason", c.value("reason", "")}, {"location", nullptr}, {"failures", Json::array()}};
    if (status.expectedFailure) out["expectedFailure"] = true;
    if (status.unexpectedPass) out["unexpectedPass"] = true;
    for (const auto& f : c.value("failures", Json::array())) {
        if (!f.is_object()) continue;
        const int line = f.value("line", 0);
        Json one{{"path", f.value("file", "")}, {"line", line > 0 ? line - 1 : 0}, {"message", f.value("message", "")}};
        for (const auto* key : {"expected", "actual", "source"})
            if (f.contains(key) && f[key].is_string()) one[key] = f[key];
        if (f.value("fatal", false)) one["fatal"] = true;
        out["failures"].push_back(std::move(one));
    }
    if (!out["failures"].empty()) {
        const auto& f = out["failures"][0];
        out["location"] = Json{{"path", f["path"]}, {"line", f["line"]}};
        std::string message = std::format("{}:{}: {}", f.value("path", ""), f.value("line", 0) + 1, f.value("message", ""));
        if (f.contains("expected")) message += std::format(" (expected {}, actual {})", f.value("expected", ""), f.value("actual", ""));
        out["message"] = std::move(message);
    } else {
        out["message"] = out["reason"];
    }
    if (auto k = c.find("checks"); k != c.end() && k->is_object()) out["checks"] = *k;
    if (auto o = c.find("output"); o != c.end() && o->is_string() && !o->get<std::string>().empty()) out["output"] = *o;
    if (auto m = c.find("metrics"); m != c.end() && m->is_array() && !m->empty()) out["metrics"] = *m;
    if (auto b = bounds_of(c); !b.empty()) out["bounds"] = std::move(b);
    if (auto k = c.value("kind", ""); !k.empty()) out["kind"] = k;
    if (auto t = c.find("tags"); t != c.end() && t->is_array() && !t->empty()) out["tags"] = *t;
    return out;
}

std::vector<ProtocolOutcome> protocol_outcomes(const Json& record) {
    std::vector<ProtocolOutcome> out;
    for (const auto& c : record.value("cases", Json::array())) {
        if (!c.is_object()) continue;
        ProtocolOutcome o;
        o.detail = protocol_case(c);
        o.outcome.name = o.detail.value("case", "");
        o.outcome.status = o.detail.value("status", "failed");
        o.outcome.durationMs = o.detail.value("durationMs", std::int64_t{0});
        o.outcome.message = o.detail.value("message", "");
        o.location = o.detail["location"];
        out.push_back(std::move(o));
    }
    return out;
}

Json case_json(const Case& c) {
    Json row{{"name", c.name}, {"source", c.source}};
    if (c.line >= 0) row["line"] = c.line;
    if (!c.file.empty()) row["file"] = c.file;
    if (!c.kind.empty()) row["kind"] = c.kind;
    if (!c.tags.empty()) row["tags"] = c.tags;
    if (!c.expect.empty()) row["expect"] = c.expect;
    if (!c.bounds.empty()) row["bounds"] = c.bounds;
    return row;
}

std::string kind_of_path(std::string_view file) {
    static constexpr std::array<std::string_view, 4> kinds{"unit", "e2e", "cts", "bench"};
    bool below = false;
    for (const auto& part : std::filesystem::path(file)) {
        const auto name = part.generic_string();
        if (below) return std::ranges::find(kinds, name) != kinds.end() ? name : std::string{};
        below = name == "tests";
    }
    return {};
}

Program program_of(const Json& r, const std::function<bool(const std::filesystem::path&)>& definesMain) {
    Program p;
    p.name = r.value("test", "");
    p.member = r.value("member", "");
    p.main = r.contains("main") && r["main"].is_string() ? r["main"].get<std::string>() : std::string{};
    if (r.contains("files") && r["files"].is_array()) {
        // mcpp states them (mcpp#917, `test-parts`).
        p.files = strings(r["files"]);
        for (const auto& part : r.value("parts", Json::array()))
            if (part.is_object()) p.parts.emplace_back(part.value("name", ""), part.value("file", ""));
        p.layout = p.files.size() > 1 || !p.parts.empty() ? "directory" : "file";
        return p;
    }
    if (p.main.empty()) return p;
    const std::filesystem::path main(p.main);
    const auto dir = main.parent_path();
    if (!dir.generic_string().ends_with("/" + p.name)) {
        p.files = {p.main};
        return p;
    }
    // A program of a directory: its sources, as mcpp 2026.11.9.1 forms it (docs/08).
    std::vector<std::filesystem::path> files;
    std::error_code ec;
    for (std::filesystem::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
        const auto ext = it->path().extension().string();
        if ((ext == ".cpp" || ext == ".cc" || ext == ".cxx") && it->is_regular_file(ec)) files.push_back(it->path());
    }
    std::ranges::sort(files);
    p.layout = "directory";
    p.main.clear();
    for (const auto& f : files) {
        p.files.push_back(f.generic_string());
        p.parts.emplace_back(p.name + "/" + f.stem().generic_string(), f.generic_string());
        if (p.main.empty() && definesMain && definesMain(f)) p.main = f.generic_string();
    }
    return p;
}

Json program_json(const Program& p) {
    Json parts = Json::array();
    for (const auto& [name, file] : p.parts) parts.push_back(Json{{"name", name}, {"file", file}});
    return Json{{"test", p.name}, {"member", p.member}, {"main", p.main}, {"files", p.files}, {"parts", parts}, {"layout", p.layout}};
}

std::string owner_of(std::string_view name, std::string_view partOf, const std::vector<Program>& programs) {
    if (!partOf.empty()) return std::string(partOf);
    std::string best;
    for (const auto& p : programs) {
        if (p.name == name) return p.name;
        if (name.size() > p.name.size() && name.starts_with(p.name) && name[p.name.size()] == '/' && p.name.size() > best.size())
            best = p.name;
    }
    return best.empty() ? std::string(name) : best;
}

std::string part_of_file(const Program& program, const std::filesystem::path& file) {
    const auto wanted = file.lexically_normal().generic_string();
    for (const auto& [name, f] : program.parts)
        if (std::filesystem::path(f).lexically_normal().generic_string() == wanted) return name;
    // Another spelling of the same file (Windows' short names, a link): the file system decides.
    std::error_code ec;
    for (const auto& [name, f] : program.parts)
        if (std::filesystem::equivalent(f, file, ec)) return name;
    return {};
}

}  // namespace mcppide::testing
