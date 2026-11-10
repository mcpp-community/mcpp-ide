// The runs of a project's tests, as mcpp keeps them (design 2026-11-10): mcpp 2026.11.9.1 writes
// each `mcpp test` run into the store mtest specifies (mtest spec P12), under `target/mtest` or
// `[test] store`. The backend reads that store; it runs nothing. A store that is absent is no error:
// the history is empty and says so.
//
// What the store holds depends on the mcpp that wrote it. 2026.11.9.1 writes each process of a
// program (its start, duration, worker and build) and each case (its status, duration, reason and
// failures); with `test-metrics` (mcpp#917) a case has its kind, tags, start, metrics and bounds as
// well. Every field is read when present, and a view that needs an absent one is left out.
export module mcppide.teststore;

import std;
import nlohmann.json;

export namespace mcppide::teststore {

using Json = nlohmann::json;

struct ProgramRun {
    std::string name;       // the program, once attributed (attribute); the process's own name before
    std::string part;       // the part of the program the process ran, when it is one
    std::string status;     // pass | fail | build-fail | not-run | built
    double ms = 0;
    double startedMs = 0;
    int worker = 0;
    double buildMs = 0;
};

struct CaseRun {
    std::string program;
    std::string part;
    std::string id;
    std::string kind;
    std::string status;     // pass | fail | skip | xfail | xpass | invalid
    double ms = 0;
    std::optional<double> startedMs;
    std::string reason;
    Json failures = Json::array();
    Json metrics = Json::array();
    Json bounds = Json::array();
    Json tags = Json::array();
};

struct Run {
    std::string id;
    std::string started;
    std::string status;
    double ms = 0;
    int jobs = 0;
    Json counts = Json::object();
    std::vector<ProgramRun> programs;
    std::vector<CaseRun> cases;
};

// The directory of a project's store: `[test] store` of its manifest (a path from the project's
// root), else target/mtest.
std::filesystem::path store_dir(const std::filesystem::path& root);

// The last `count` runs of the store at `dir`, oldest first. Runs already read are kept in `cache`
// (by id), so a store read again reads only its new runs.
void attribute(std::vector<Run>& runs, const std::function<std::string(std::string_view)>& owner) {
    for (auto& r : runs) {
        for (auto& p : r.programs) {
            auto program = owner(p.name);
            if (program != p.name) p.part = std::exchange(p.name, std::move(program));
        }
        for (auto& c : r.cases) {
            auto program = owner(c.program);
            if (program != c.program) c.part = std::exchange(c.program, std::move(program));
        }
    }
}

std::vector<Run> last_runs(const std::filesystem::path& dir, std::size_t count, std::map<std::string, Run>& cache);

// One run's files read (exposed for the tests).
Run read_run(const std::filesystem::path& runDir, const Json& indexRecord);

// The programs the store's processes and cases belong to: a process of a program of several files is
// named by its part (`unit/a`); `owner` names its program (`unit`), and the part is kept beside it.
void attribute(std::vector<Run>& runs, const std::function<std::string(std::string_view)>& owner);

// The runs of the window, and each case's results across them (the program's, or the one case).
// {runs: [{id, started, status, ms, counts}], cases: [{program, case, kind, results: [{run, status,
// ms, metrics}]}]}
Json history(const std::vector<Run>& runs, std::string_view program, std::string_view caseId);

// What the window says, for an editor's view of the tests (design 2026-11-10):
// {run, byKind, failures, slowest, flaky, metrics, schedule, available: {kinds, metrics, cases}}.
Json insights(const std::vector<Run>& runs);

}  // namespace mcppide::teststore

namespace mcppide::teststore {

namespace {

std::string read_file(const std::filesystem::path& p) {
    std::ifstream in(p, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), {});
}

std::vector<Json> ndjson(const std::filesystem::path& p) {
    std::vector<Json> out;
    std::ifstream in(p, std::ios::binary);
    for (std::string line; in && std::getline(in, line);) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        auto j = Json::parse(line, nullptr, false);
        if (j.is_object()) out.push_back(std::move(j));
    }
    return out;
}

double number(const Json& j, const char* key) {
    if (auto it = j.find(key); it != j.end() && it->is_number()) return it->get<double>();
    return 0;
}

Json array(const Json& j, const char* key) {
    if (auto it = j.find(key); it != j.end() && it->is_array()) return *it;
    return Json::array();
}

bool failing(std::string_view status) {
    return status == "fail" || status == "xpass" || status == "invalid" || status == "build-fail";
}

std::string key_of(const CaseRun& c) { return c.program + '\x1f' + c.id; }

double median(std::vector<double> v) {
    if (v.empty()) return 0;
    std::ranges::sort(v);
    const auto n = v.size();
    return n % 2 ? v[n / 2] : (v[n / 2 - 1] + v[n / 2]) / 2;
}

// The bound of `metric` among a case's bounds, as {max?, min?}, or null.
Json bound_of(const Json& bounds, std::string_view metric) {
    for (const auto& b : bounds)
        if (b.is_object() && b.value("metric", "") == metric) {
            Json out = Json::object();
            if (b.contains("max") && b["max"].is_number()) out["max"] = b["max"];
            if (b.contains("min") && b["min"].is_number()) out["min"] = b["min"];
            return out;
        }
    return nullptr;
}

// How far the last value is from its bound, as a share of the bound (negative: past it), or null.
Json margin_of(const Json& bound, double last) {
    if (!bound.is_object()) return nullptr;
    if (bound.contains("max")) {
        const double max = bound["max"].get<double>();
        return max != 0 ? Json((max - last) / std::abs(max)) : Json(nullptr);
    }
    if (bound.contains("min")) {
        const double min = bound["min"].get<double>();
        return min != 0 ? Json((last - min) / std::abs(min)) : Json(nullptr);
    }
    return nullptr;
}

// `program`, and `part` when the row is of a part of it.
void name_row(Json& row, const std::string& program, const std::string& part) {
    row["program"] = program;
    if (!part.empty() && part != program) row["part"] = part;
}

Json run_brief(const Run& r) {
    return Json{{"id", r.id}, {"started", r.started}, {"status", r.status}, {"ms", r.ms}, {"jobs", r.jobs}, {"counts", r.counts}};
}

Json failures_of(const std::vector<Run>& runs) {
    Json out = Json::array();
    const auto& last = runs.back();
    for (const auto& c : last.cases) {
        if (!failing(c.status)) continue;
        // The run since which it has failed: walk back while it did.
        std::string since = last.id;
        for (auto it = runs.rbegin() + 1; it != runs.rend(); ++it) {
            const auto found = std::ranges::find_if(it->cases, [&](const CaseRun& o) { return key_of(o) == key_of(c); });
            if (found == it->cases.end() || !failing(found->status)) break;
            since = it->id;
        }
        Json f{{"case", c.id}, {"status", c.status}, {"reason", c.reason}, {"since", since}, {"new", since == last.id}};
        name_row(f, c.program, c.part);
        if (!c.failures.empty() && c.failures[0].is_object()) {
            const auto& first = c.failures[0];
            f["message"] = first.value("message", "");
            f["path"] = first.value("file", "");
            const int line = first.value("line", 0);
            f["line"] = line > 0 ? line - 1 : 0;
            for (const auto* key : {"expected", "actual"})
                if (first.contains(key) && first[key].is_string()) f[key] = first[key];
        }
        out.push_back(std::move(f));
    }
    // A program that failed without a case reporting it (it did not compile, or speaks no protocol).
    for (const auto& p : last.programs) {
        if (!failing(p.status)) continue;
        const auto process = p.part.empty() ? p.name : p.part;
        const bool reported = std::ranges::any_of(last.cases, [&](const CaseRun& c) {
            return (c.part.empty() ? c.program : c.part) == process && failing(c.status);
        });
        if (reported) continue;
        Json f{{"case", ""}, {"status", p.status}, {"since", last.id}, {"new", true}};
        name_row(f, p.name, p.part);
        out.push_back(std::move(f));
    }
    return out;
}

Json slowest_of(const std::vector<Run>& runs, std::size_t limit) {
    std::map<std::string, std::vector<double>> durations;
    for (const auto& r : runs)
        for (const auto& c : r.cases) durations[key_of(c)].push_back(c.ms);
    auto cases = runs.back().cases;
    std::ranges::sort(cases, std::greater{}, &CaseRun::ms);
    Json out = Json::array();
    for (const auto& c : cases | std::views::take(limit)) {
        Json row{{"case", c.id}, {"ms", c.ms}, {"medianMs", median(durations[key_of(c)])}, {"runs", durations[key_of(c)].size()}};
        name_row(row, c.program, c.part);
        out.push_back(std::move(row));
    }
    return out;
}

Json flaky_of(const std::vector<Run>& runs) {
    struct Seen { std::string program, part, id, last; int passed = 0, failed = 0, flips = 0; };
    std::map<std::string, Seen> seen;
    for (const auto& r : runs)
        for (const auto& c : r.cases) {
            if (c.status != "pass" && !failing(c.status)) continue;
            auto& s = seen[key_of(c)];
            s.program = c.program;
            s.part = c.part;
            s.id = c.id;
            const auto now = failing(c.status) ? "fail" : "pass";
            if (!s.last.empty() && s.last != now) ++s.flips;
            s.last = now;
            ++(failing(c.status) ? s.failed : s.passed);
        }
    Json out = Json::array();
    for (const auto& [key, s] : seen)
        if (s.passed > 0 && s.failed > 0) {
            Json row{{"case", s.id}, {"passed", s.passed}, {"failed", s.failed}, {"flips", s.flips}};
            name_row(row, s.program, s.part);
            out.push_back(std::move(row));
        }
    std::stable_sort(out.begin(), out.end(), [](const Json& a, const Json& b) { return a.value("flips", 0) > b.value("flips", 0); });
    return out;
}

struct Series {
    std::string program, part, id, name, unit, better;
    Json bounds = Json::array();
    Json values = Json::array();
};

// A case's metrics of one run added to their series (kept in the order they were first seen).
void add_metrics(std::map<std::string, Series>& series, std::vector<std::string>& order, const Run& r, const CaseRun& c) {
    for (const auto& m : c.metrics) {
        if (!m.is_object() || !m.contains("value") || !m["value"].is_number()) continue;
        const auto name = m.value("name", "");
        const auto key = key_of(c) + '\x1f' + name;
        auto [it, fresh] = series.try_emplace(key);
        if (fresh) order.push_back(key);
        auto& s = it->second;
        s.program = c.program;
        s.part = c.part;
        s.id = c.id;
        s.name = name;
        s.unit = m.value("unit", s.unit);
        s.better = m.value("better", s.better);
        if (!c.bounds.empty()) s.bounds = c.bounds;
        s.values.push_back(Json{{"run", r.id}, {"value", m["value"]}});
    }
}

Json metrics_of(const std::vector<Run>& runs) {
    std::map<std::string, Series> series;
    std::vector<std::string> order;
    for (const auto& r : runs)
        for (const auto& c : r.cases) add_metrics(series, order, r, c);
    Json out = Json::array();
    for (const auto& key : order) {
        const auto& s = series[key];
        const double last = s.values.back()["value"].get<double>();
        const auto bound = bound_of(s.bounds, s.name);
        Json row{{"case", s.id}, {"name", s.name}, {"unit", s.unit}, {"values", s.values},
                 {"last", last}, {"bound", bound}, {"margin", margin_of(bound, last)}};
        name_row(row, s.program, s.part);
        if (!s.better.empty()) row["better"] = s.better;
        out.push_back(std::move(row));
    }
    return out;
}

// The schedule of the last run: a lane per worker, each process with its start and duration, and the
// critical path -- the processes of the lane that ends last (a worker's processes run one after
// another, P12.3), the build the run waited for before them.
Json schedule_of(const Run& r) {
    std::map<int, Json> lanes;
    std::map<int, double> ends;
    double build = 0;
    for (const auto& p : r.programs) {
        Json item{{"startMs", p.startedMs}, {"ms", p.ms}, {"status", p.status}, {"buildMs", p.buildMs}};
        name_row(item, p.name, p.part);
        lanes[p.worker].push_back(std::move(item));
        ends[p.worker] = std::max(ends[p.worker], p.startedMs + p.ms);
        build = std::max(build, p.buildMs);
    }
    Json out{{"lanes", Json::array()}, {"criticalPath", Json::array()}, {"ms", r.ms}, {"buildMs", build}};
    int longest = -1;
    for (const auto& [worker, end] : ends)
        if (longest < 0 || end > ends[longest]) longest = worker;
    for (auto& [worker, items] : lanes) {
        std::stable_sort(items.begin(), items.end(), [](const Json& a, const Json& b) { return a.value("startMs", 0.0) < b.value("startMs", 0.0); });
        out["lanes"].push_back(Json{{"worker", worker}, {"items", items}});
        if (worker == longest)
            for (const auto& i : items) out["criticalPath"].push_back(i["program"]);
    }
    return out;
}

}  // namespace

std::filesystem::path store_dir(const std::filesystem::path& root) {
    // `[test] store = "<dir>"`: read from the manifest's text (its section and key, as mcpp writes them).
    std::istringstream text(read_file(root / "mcpp.toml"));
    bool inTest = false;
    for (std::string line; std::getline(text, line);) {
        const auto first = line.find_first_not_of(" \t");
        if (first == std::string::npos || line[first] == '#') continue;
        if (line[first] == '[') {
            inTest = line.compare(first, 6, "[test]") == 0;
            continue;
        }
        if (!inTest || line.compare(first, 5, "store") != 0) continue;
        const auto open = line.find('"'), close = line.rfind('"');
        if (open != std::string::npos && close > open) return root / line.substr(open + 1, close - open - 1);
    }
    return root / "target" / "mtest";
}

Run read_run(const std::filesystem::path& runDir, const Json& indexRecord) {
    Run r;
    const auto runJson = Json::parse(read_file(runDir / "run.json"), nullptr, false);
    const auto& rec = runJson.is_object() ? runJson : indexRecord;
    r.id = rec.value("id", indexRecord.value("id", ""));
    r.started = rec.value("started", "");
    r.status = rec.value("status", "");
    r.ms = number(rec, "ms");
    r.jobs = static_cast<int>(number(rec, "jobs"));
    if (rec.contains("counts") && rec["counts"].is_object()) r.counts = rec["counts"];
    for (const auto& p : ndjson(runDir / "programs.ndjson"))
        r.programs.push_back(ProgramRun{.name = p.value("name", ""), .status = p.value("status", ""), .ms = number(p, "ms"),
                                        .startedMs = number(p, "started_ms"), .worker = static_cast<int>(number(p, "worker")),
                                        .buildMs = number(p, "build_ms")});
    for (const auto& c : ndjson(runDir / "cases.ndjson")) {
        CaseRun one{.program = c.value("program", ""), .id = c.value("id", ""), .kind = c.value("kind", ""),
                    .status = c.value("status", ""), .ms = number(c, "ms")};
        if (auto it = c.find("started_ms"); it != c.end() && it->is_number()) one.startedMs = it->get<double>();
        one.reason = c.value("reason", "");
        one.failures = array(c, "failures");
        one.metrics = array(c, "metrics");
        one.bounds = array(c, "bounds");
        one.tags = array(c, "tags");
        r.cases.push_back(std::move(one));
    }
    return r;
}

std::vector<Run> last_runs(const std::filesystem::path& dir, std::size_t count, std::map<std::string, Run>& cache) {
    std::vector<Json> index = ndjson(dir / "index.ndjson");
    // An identifier sorts by time (P12.2).
    std::ranges::sort(index, {}, [](const Json& j) { return j.value("id", ""); });
    const auto from = index.size() > count ? index.size() - count : 0;
    std::vector<Run> out;
    for (std::size_t i = from; i < index.size(); ++i) {
        const auto id = index[i].value("id", "");
        if (id.empty()) continue;
        auto it = cache.find(id);
        if (it == cache.end()) {
            std::error_code ec;
            if (!std::filesystem::is_directory(dir / "runs" / id, ec)) continue;
            it = cache.emplace(id, read_run(dir / "runs" / id, index[i])).first;
        }
        out.push_back(it->second);
    }
    return out;
}

Json history(const std::vector<Run>& runs, std::string_view program, std::string_view caseId) {
    Json out{{"runs", Json::array()}, {"cases", Json::array()}};
    std::map<std::string, Json> byCase;
    std::vector<std::string> order;
    for (const auto& r : runs) {
        out["runs"].push_back(run_brief(r));
        for (const auto& c : r.cases) {
            if (!program.empty() && c.program != program) continue;
            if (!caseId.empty() && c.id != caseId) continue;
            Json row{{"case", c.id}, {"kind", c.kind}, {"results", Json::array()}};
            name_row(row, c.program, "");
            auto [it, fresh] = byCase.try_emplace(key_of(c), std::move(row));
            if (fresh) order.push_back(key_of(c));
            Json result{{"run", r.id}, {"status", c.status}, {"ms", c.ms}, {"metrics", c.metrics}};
            if (!c.part.empty()) result["part"] = c.part;
            it->second["results"].push_back(std::move(result));
        }
    }
    for (const auto& k : order) out["cases"].push_back(std::move(byCase[k]));
    return out;
}

Json insights(const std::vector<Run>& runs) {
    Json out{{"runs", runs.size()}, {"run", nullptr}, {"byKind", Json::object()}, {"failures", Json::array()},
             {"slowest", Json::array()}, {"flaky", Json::array()}, {"metrics", Json::array()}, {"schedule", nullptr},
             {"available", Json{{"kinds", false}, {"metrics", false}, {"cases", false}}}};
    if (runs.empty()) return out;
    const auto& last = runs.back();
    out["run"] = run_brief(last);
    for (const auto& c : last.cases) {
        auto& k = out["byKind"][c.kind];
        if (!k.is_object()) k = Json::object();
        k[c.status] = k.value(c.status, 0) + 1;
    }
    out["available"]["cases"] = !last.cases.empty();
    out["available"]["kinds"] = std::ranges::any_of(last.cases, [](const CaseRun& c) { return !c.kind.empty(); });
    out["available"]["metrics"] = std::ranges::any_of(runs, [](const Run& r) {
        return std::ranges::any_of(r.cases, [](const CaseRun& c) { return !c.metrics.empty(); });
    });
    out["failures"] = failures_of(runs);
    out["slowest"] = slowest_of(runs, 10);
    out["flaky"] = flaky_of(runs);
    out["metrics"] = metrics_of(runs);
    out["schedule"] = schedule_of(last);
    return out;
}

}  // namespace mcppide::teststore
