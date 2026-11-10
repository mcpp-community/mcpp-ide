// A test program's cases (mcpp design 2026-10-30 part 2 §7): which framework a program uses, its
// cases as its source names them and as the program lists them, the arguments that run some of
// them and write a report, and the report read back.
//
// mcpp's own unit is the program (`mcpp test <name>`, judged by its exit code; F17). A case is the
// framework's, so everything here is the framework's convention, from spec/test-frameworks.json,
// and reaches the program after `--`, as written.
//
// A PROGRAM BELONGS TO A FRAMEWORK WHEN BOTH HOLD: its plan holds one of the framework's packages,
// and its source calls one of the framework's macros. A program that depends on a framework and
// writes its own `main` must not be asked to list its cases: GoogleTest's `--gtest_list_tests`
// would run such a program's tests there and then.
export module mcppide.testing;

import std;
import nlohmann.json;
import mcppide.scan;
import mcppide.spec;

export namespace mcppide::testing {

using Json = nlohmann::json;

struct Framework {
    std::string id, title;
    std::vector<std::string> packages, macros;
    std::string name;             // "suite.case" | "literal"
    std::string scenarioPrefix;   // what SCENARIO puts before its name
    std::vector<std::string> list;
    std::string listFormat;       // "gtest-list" | "catch2-json" | "lines"
    std::vector<std::string> filter, report;
    std::string join, escape, reportFormat;
    // The version of the mtest test protocol the framework's programs implement (0: none). Their
    // cases are their `"name"_test` literals in the source, listed by `mcpp test --cases`, run by
    // `mcpp test --case` and reported in mcpp's records (mcpp's feature `test-protocol`).
    int protocol = 0;
};

// spec/test-frameworks.json, in its order.
const std::vector<Framework>& frameworks();
const Framework* framework(std::string_view id);

// The macro names to look for: every framework's, and those `mcpp.test.macros` adds
// ({"gtest": ["MY_TEST"]}).
std::vector<std::string> macros(const Json& extra);

// The framework of a program whose plan holds `packages` and whose source scanned as `found`; none
// when no framework has both.
const Framework* framework_of(const std::vector<std::string>& packages, const scan::Result& found, const Json& extra);

struct Case {
    std::string name;     // the framework's full name: Suite.Name, or the test's name
    int line = -1;        // 0-based, in the program's source; -1 when only the program listed it
    std::string source;   // "static" (its source) | "listed" (the program listed it)
    // Of a program that implements the test protocol (mtest's manifest, or the program's listing):
    // the file that declares it (a program of several files has one per case), its kind (`unit`,
    // `e2e`, `cts`, `bench`; empty when neither it nor its directory states one), its tags, `fail`
    // when it is expected to fail, and the bounds on its metrics ([{metric, max?, min?}]).
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

// The cases the source names, in order.
std::vector<Case> static_cases(const Framework& f, const scan::Result& found, const Json& extra);

// The names the program's list command printed.
std::vector<std::string> listed(const Framework& f, std::string_view output);

// The name a listed case has in its source: GoogleTest's instantiated and typed names lose the
// instance prefix and the parameter (`Inst/Suite.Name/0` and `Suite/0.Name` are `Suite.Name`).
std::string base_name(const Framework& f, std::string_view name);

// The cases after a listing: a listed name whose base is a static case's gives that case its
// listed name (the first occurrence; a second name of one base is a case of its own at the same
// line); a name with no static case is added without a line.
std::vector<Case> merge(const std::vector<Case>& statics, const std::vector<std::string>& names, const Framework& f);

// What follows `--` to run `cases` (all of them when empty) and write the report to `report`.
std::vector<std::string> run_args(const Framework& f, const std::vector<std::string>& cases, const std::filesystem::path& report);
// The filter alone: what a debugged program receives to run `cases`.
std::vector<std::string> filter_args(const Framework& f, const std::vector<std::string>& cases);

struct Outcome {
    std::string name;
    std::string status;   // passed | failed | skipped
    std::int64_t durationMs = 0;
    std::string message;
};

// Each case's own output in its program's output, by the framework's boundaries (design 2026-11-07,
// D16): GoogleTest prints `[ RUN      ] Suite.Name` before a case and `[       OK ]`, `[  FAILED  ]` or
// `[  SKIPPED ]` with the name after it, and the case's output is those lines and what is between
// them. Empty for a framework whose console names no case boundary (Catch2 and doctest report a case
// in their report; a failure's expression and place are its message).
std::map<std::string, std::string> case_outputs(const Framework& f, std::string_view output);

// A report the framework wrote.
std::vector<Outcome> read_report(const Framework& f, std::string_view text);

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
// actual?}], and when stated metrics, bounds, kind, tags, expectedFailure, unexpectedPass}. A line is
// 0-based; `message` is the first failure with its place, else the reason; `location` is the first
// failure's place, or null.
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
std::vector<std::string> macros_of(const Framework& f, const Json& extra) {
    auto out = f.macros;
    if (extra.is_object() && extra.contains(f.id))
        for (const auto& m : strings(extra[f.id])) out.push_back(m);
    return out;
}

std::string name_of(const Framework& f, const scan::Call& call) {
    if (f.name == "suite.case")
        return call.arguments.size() >= 2 ? call.arguments[0] + "." + call.arguments[1] : std::string{};
    for (const auto& a : call.arguments)
        if (!a.empty() && a.front() == '"') {
            const auto value = scan::literal_value(a);
            return call.name == "SCENARIO" ? f.scenarioPrefix + value : value;
        }
    return {};
}

std::int64_t milliseconds(std::string_view seconds) {
    double v = 0;
    std::from_chars(seconds.data(), seconds.data() + seconds.size(), v);
    return static_cast<std::int64_t>(v * 1000.0 + 0.5);
}

// The value of an XML attribute in `tag` (the text between `<` and `>`), with the five entities.
std::string attribute(std::string_view tag, std::string_view name) {
    const auto key = std::string(" ") + std::string(name) + "=\"";
    const auto at = tag.find(key);
    if (at == std::string_view::npos) return {};
    const auto from = at + key.size();
    const auto end = tag.find('"', from);
    std::string raw(tag.substr(from, end == std::string_view::npos ? std::string_view::npos : end - from));
    std::string out;
    for (std::size_t i = 0; i < raw.size(); ++i) {
        if (raw[i] != '&') { out.push_back(raw[i]); continue; }
        const auto semi = raw.find(';', i);
        if (semi == std::string::npos) { out.push_back('&'); continue; }
        const auto entity = std::string_view(raw).substr(i + 1, semi - i - 1);
        out += entity == "lt" ? "<" : entity == "gt" ? ">" : entity == "quot" ? "\"" : entity == "apos" ? "'" : entity == "amp" ? "&" : "";
        i = semi;
    }
    return out;
}

std::vector<Outcome> gtest_report(const Json& doc) {
    std::vector<Outcome> out;
    for (const auto& suite : doc.value("testsuites", Json::array())) {
        const auto suiteName = suite.value("name", "");
        for (const auto& t : suite.value("testsuite", Json::array())) {
            Outcome o;
            o.name = suiteName + "." + t.value("name", "");
            o.durationMs = milliseconds(t.value("time", "0s"));
            const auto failures = t.value("failures", Json::array());
            if (t.value("result", "") == "SKIPPED" || t.value("status", "") == "NOTRUN") o.status = "skipped";
            else o.status = failures.empty() ? "passed" : "failed";
            for (const auto& f : failures) {
                if (!o.message.empty()) o.message += "\n";
                o.message += f.value("failure", "");
            }
            out.push_back(std::move(o));
        }
    }
    return out;
}

std::vector<Outcome> catch2_report(const Json& doc) {
    std::vector<Outcome> out;
    const auto run = doc.value("test-run", Json::object());
    for (const auto& tc : run.value("test-cases", Json::array())) {
        Outcome o;
        o.name = tc.value("test-info", Json::object()).value("name", "");
        const auto totals = tc.value("totals", Json::object());
        const auto assertions = totals.value("assertions", Json::object());
        const auto failed = assertions.value("failed", 0);
        const auto skipped = totals.value("test-cases", Json::object()).value("skipped", 0) + assertions.value("skipped", 0);
        o.status = failed > 0 ? "failed" : skipped > 0 ? "skipped" : "passed";
        // The failed assertions' expressions, as the reporter wrote them.
        std::function<void(const Json&)> walk = [&](const Json& node) {
            if (node.is_object()) {
                if (node.value("kind", "") == "assertion" && node.value("status", true) == false) {
                    if (!o.message.empty()) o.message += "\n";
                    const auto loc = node.value("source-location", Json::object());
                    o.message += std::format("{}:{}: {}", loc.value("filename", ""), loc.value("line", 0), node.value("expression", node.value("message", "")));
                }
                for (const auto& v : node) walk(v);
            } else if (node.is_array()) {
                for (const auto& v : node) walk(v);
            }
        };
        walk(tc.value("runs", Json::array()));
        if (o.status == "failed" && o.message.empty()) o.message = std::format("{} assertion(s) failed", failed);
        out.push_back(std::move(o));
    }
    return out;
}

std::vector<Outcome> junit_report(std::string_view text) {
    std::vector<Outcome> out;
    std::size_t at = 0;
    while ((at = text.find("<testcase", at)) != std::string_view::npos) {
        const auto close = text.find('>', at);
        if (close == std::string_view::npos) break;
        const auto tag = text.substr(at, close - at);
        Outcome o;
        o.name = attribute(tag, "name");
        o.durationMs = milliseconds(attribute(tag, "time"));
        o.status = attribute(tag, "status") == "skipped" ? "skipped" : "passed";
        const bool empty = close > 0 && text[close - 1] == '/';
        if (!empty) {
            const auto end = text.find("</testcase>", close);
            const auto body = text.substr(close + 1, end == std::string_view::npos ? std::string_view::npos : end - close - 1);
            if (const auto f = body.find("<failure"); f != std::string_view::npos) {
                o.status = "failed";
                const auto fclose = body.find('>', f);
                o.message = attribute(body.substr(f, fclose - f), "message");
            } else if (body.find("<error") != std::string_view::npos) {
                o.status = "failed";
            } else if (body.find("<skipped") != std::string_view::npos) {
                o.status = "skipped";
            }
            at = end == std::string_view::npos ? text.size() : end;
        } else {
            at = close;
        }
        out.push_back(std::move(o));
    }
    return out;
}

}  // namespace

const std::vector<Framework>& frameworks() {
    static const std::vector<Framework> all = [] {
        std::vector<Framework> out;
        for (const auto& f : spec::test_frameworks().value("frameworks", Json::array())) {
            Framework v;
            v.id = f.value("id", "");
            v.title = f.value("title", v.id);
            v.packages = strings(f.value("packages", Json::array()));
            v.macros = strings(f.value("macros", Json::array()));
            v.name = f.value("name", "literal");
            v.scenarioPrefix = f.value("scenarioPrefix", "");
            v.list = strings(f.value("list", Json::array()));
            v.listFormat = f.value("listFormat", "lines");
            v.filter = strings(f.value("filter", Json::array()));
            v.report = strings(f.value("report", Json::array()));
            v.join = f.value("join", ",");
            v.escape = f.value("escape", "");
            v.reportFormat = f.value("reportFormat", "");
            v.protocol = f.value("protocol", 0);
            out.push_back(std::move(v));
        }
        return out;
    }();
    return all;
}

const Framework* framework(std::string_view id) {
    for (const auto& f : frameworks()) if (f.id == id) return &f;
    return nullptr;
}

std::vector<std::string> macros(const Json& extra) {
    std::vector<std::string> out;
    for (const auto& f : frameworks())
        for (const auto& m : macros_of(f, extra))
            if (std::ranges::find(out, m) == out.end()) out.push_back(m);
    return out;
}

const Framework* framework_of(const std::vector<std::string>& packages, const scan::Result& found, const Json& extra) {
    for (const auto& f : frameworks()) {
        const bool linked = std::ranges::any_of(f.packages, [&](const std::string& p) {
            return std::ranges::any_of(packages, [&](const std::string& in) { return in == p || in.starts_with(p + ":"); });
        });
        if (!linked) continue;
        if (f.protocol && !found.cases.empty()) return &f;
        const auto names = macros_of(f, extra);
        if (std::ranges::any_of(found.calls, [&](const scan::Call& c) { return std::ranges::find(names, c.name) != names.end(); }))
            return &f;
    }
    return nullptr;
}

std::vector<Case> static_cases(const Framework& f, const scan::Result& found, const Json& extra) {
    std::vector<Case> out;
    if (f.protocol) {
        for (const auto& c : found.cases) {
            Case one{c.id, c.line, "static"};
            one.kind = c.kind;
            one.tags = c.tags;
            for (const auto& b : c.bounds) one.bounds.push_back(bound_json(b.metric, b.max, b.min));
            out.push_back(std::move(one));
        }
        return out;
    }
    const auto names = macros_of(f, extra);
    for (const auto& call : found.calls) {
        if (std::ranges::find(names, call.name) == names.end()) continue;
        auto name = name_of(f, call);
        if (!name.empty()) out.push_back(Case{std::move(name), call.line, "static"});
    }
    return out;
}

std::vector<std::string> listed(const Framework& f, std::string_view output) {
    std::vector<std::string> out;
    if (f.listFormat == "gtest-list") {
        std::string suite;
        for (const auto& raw : lines(output)) {
            const std::string_view line(raw);
            const auto comment = line.find("  #");
            const auto text = trim(comment == std::string_view::npos ? line : line.substr(0, comment));
            if (text.empty()) continue;
            if (!line.empty() && line.front() != ' ') {
                if (text.back() == '.') suite = text;
                continue;
            }
            if (!suite.empty()) out.push_back(suite + text);
        }
    } else if (f.listFormat == "catch2-json") {
        const auto start = output.find('{');
        const Json doc = Json::parse(start == std::string_view::npos ? std::string_view{} : output.substr(start), nullptr, false);
        if (doc.is_object())
            for (const auto& t : doc.value("listings", Json::object()).value("tests", Json::array()))
                if (t.contains("name") && t["name"].is_string()) out.push_back(t["name"].get<std::string>());
    } else {
        for (const auto& raw : lines(output)) {
            const auto text = trim(raw);
            if (text.empty() || text.starts_with("[doctest]") || text.starts_with("===")) continue;
            out.push_back(raw.starts_with("  Scenario: ") ? raw : text);
        }
    }
    return out;
}

std::string base_name(const Framework& f, std::string_view name) {
    if (f.name != "suite.case") return std::string(name);
    const auto dot = name.find('.');
    if (dot == std::string_view::npos) return std::string(name);
    auto suite = name.substr(0, dot);
    auto test = name.substr(dot + 1);
    // TEST_P: Inst/Suite.Name/0; TYPED_TEST: Suite/0.Name; TYPED_TEST_P: Inst/Suite/0.Name.
    if (const auto slash = test.find('/'); slash != std::string_view::npos) test = test.substr(0, slash);
    const auto parts = [](std::string_view s) {
        std::vector<std::string_view> v;
        std::size_t at = 0;
        while (true) {
            const auto next = s.find('/', at);
            v.push_back(s.substr(at, next == std::string_view::npos ? std::string_view::npos : next - at));
            if (next == std::string_view::npos) break;
            at = next + 1;
        }
        return v;
    }(suite);
    // The suite is the last part that is not a number.
    for (auto it = parts.rbegin(); it != parts.rend(); ++it)
        if (!it->empty() && !std::ranges::all_of(*it, [](char c) { return std::isdigit(static_cast<unsigned char>(c)); })) {
            suite = *it;
            break;
        }
    return std::format("{}.{}", suite, test);
}

std::vector<Case> merge(const std::vector<Case>& statics, const std::vector<std::string>& names, const Framework& f) {
    std::vector<Case> out = statics;
    std::vector<bool> taken(out.size(), false);
    for (const auto& name : names) {
        const auto base = base_name(f, name);
        bool placed = false;
        for (std::size_t i = 0; i < statics.size() && !placed; ++i) {
            if (statics[i].name != base) continue;
            if (!taken[i]) {
                out[i].name = name;
                out[i].source = "listed";
                taken[i] = true;
            } else {
                out.push_back(Case{name, statics[i].line, "listed"});
            }
            placed = true;
        }
        if (!placed) out.push_back(Case{name, -1, "listed"});
    }
    return out;
}

std::vector<std::string> run_args(const Framework& f, const std::vector<std::string>& cases, const std::filesystem::path& report) {
    auto out = filter_args(f, cases);
    for (const auto& a : f.report) {
        std::string arg = a;
        if (const auto at = arg.find("{}"); at != std::string::npos) arg.replace(at, 2, report.string());
        out.push_back(std::move(arg));
    }
    return out;
}

std::vector<std::string> filter_args(const Framework& f, const std::vector<std::string>& cases) {
    std::vector<std::string> out;
    if (!cases.empty()) {
        // A GoogleTest case named as its source names it stands for its instances too:
        // Inst/Suite.Name/N (TEST_P) and Suite/N.Name (TYPED_TEST).
        std::vector<std::string> patterns;
        for (const auto& c : cases) {
            patterns.push_back(c);
            if (f.name == "suite.case" && c.find('/') == std::string::npos)
                if (const auto dot = c.find('.'); dot != std::string::npos) {
                    patterns.push_back("*/" + c + "/*");
                    patterns.push_back(c.substr(0, dot) + "/*." + c.substr(dot + 1));
                }
        }
        std::string joined;
        for (const auto& c : patterns) {
            if (!joined.empty()) joined += f.join;
            for (char ch : c) {
                if (f.escape.find(ch) != std::string::npos) joined.push_back('\\');
                joined.push_back(ch);
            }
        }
        for (const auto& a : f.filter) {
            std::string arg = a;
            if (const auto at = arg.find("{}"); at != std::string::npos) arg.replace(at, 2, joined);
            out.push_back(std::move(arg));
        }
    }
    return out;
}

std::map<std::string, std::string> case_outputs(const Framework& f, std::string_view output) {
    std::map<std::string, std::string> out;
    if (f.id != "gtest") return out;
    std::string current, text;
    std::size_t at = 0;
    while (at < output.size()) {
        auto end = output.find('\n', at);
        const auto stop = end == std::string_view::npos ? output.size() : end + 1;
        const auto line = output.substr(at, stop - at);
        at = stop;
        // `[ RUN      ] Suite.Name`; the result lines name it again, then ` (N ms)`.
        const auto tagged = [&](std::string_view tag) -> std::optional<std::string> {
            if (!line.starts_with(tag)) return std::nullopt;
            auto name = line.substr(tag.size());
            while (!name.empty() && (name.back() == '\n' || name.back() == '\r')) name.remove_suffix(1);
            if (const auto paren = name.find(" ("); paren != std::string_view::npos) name = name.substr(0, paren);
            return std::string(name);
        };
        if (auto started = tagged("[ RUN      ] ")) {
            current = *started;
            text = std::string(line);
            continue;
        }
        if (current.empty()) continue;
        text += line;
        for (std::string_view tag : {"[       OK ] ", "[  FAILED  ] ", "[  SKIPPED ] "})
            if (auto ended = tagged(tag); ended && *ended == current) {
                out[current] = std::move(text);
                current.clear();
                text.clear();
                break;
            }
    }
    // A case the program ended in (a crash): what it printed until then.
    if (!current.empty()) out[current] = std::move(text);
    return out;
}

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
        for (const auto* key : {"expected", "actual"})
            if (f.contains(key) && f[key].is_string()) one[key] = f[key];
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

std::vector<Outcome> read_report(const Framework& f, std::string_view text) {
    if (f.reportFormat == "junit") return junit_report(text);
    const Json doc = Json::parse(text, nullptr, false);
    if (!doc.is_object()) return {};
    if (f.reportFormat == "gtest-json") return gtest_report(doc);
    if (f.reportFormat == "catch2-json") return catch2_report(doc);
    return {};
}

}  // namespace mcppide::testing
