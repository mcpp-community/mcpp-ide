// View models: what the panels and trees show, computed from mcpp's
// envelopes. Each function takes an envelope (or a record) and returns the
// JSON an adapter renders without deciding anything itself.
//
//   cache_inventory   `mcpp cache list --format json`      totals, by kind, top labels, ages
//   cache_preview     the inventory and a clean plan       what a cache action would remove
//   toolchains        `mcpp toolchain list --format json`  rows by target, the defaults
//   library           `mcpp search --format json`          packages, filtered and sorted
//   package_graph     `mcpp emit build-database`           sets and what each sees
//   module_graph      the same                             modules, who provides, who imports
//   project_summary   the same and the result record       targets, tests, artifacts, last build
//   targets           `mcpp toolchain list --format json`  one row per target: its toolchains, its platform
//   platform_text     a `platform` object of mcpp's        "openkal-platform 0.17.1 (built-in)"
//   stream_record     one record of a `--message-format json` stream, normalised
export module mcppide.views;

import std;
import nlohmann.json;
import mcppide.policy;

export namespace mcppide::views {

using Json = nlohmann::json;

struct CacheOptions {
    int topN = 5;
    std::vector<int> ageBucketDays = {1, 7, 30};
    std::int64_t now = 0;   // seconds since the epoch; 0: the clock
};
Json cache_inventory(const Json& cacheListEnvelope, const CacheOptions& options);
Json cache_preview(const Json& cacheListEnvelope, const policy::CleanPlan& plan, const policy::CleanOptions& clean,
                   std::int64_t now = 0);

Json toolchains(const Json& toolchainListEnvelope);

struct LibraryQuery {
    std::string keyword;
    std::string index;        // only this index; empty: every index
    std::string sort = "name";  // name | versions
    std::size_t limit = 500;
};
Json library(const Json& searchEnvelope, const LibraryQuery& query);
Json library_detail(const Json& searchEnvelope, std::string_view id);

Json package_graph(const Json& buildDatabaseEnvelope);
Json module_graph(const Json& buildDatabaseEnvelope);
Json project_summary(const Json& buildDatabaseEnvelope, const Json& lastResult);

// The targets mcpp knows, one row each, with the toolchains installed for it and, where mcpp
// reports one (design 2026-10-28 §9), the platform that configures it. `selected` is the target the
// adapter builds for ("" for the host).
Json targets(const Json& toolchainListEnvelope, std::string_view selected);
// "openkal-platform 0.17.1 (built-in)", "host conventions (built-in)"; "" for null or a non-object.
std::string platform_text(const Json& platform);

// One record of a `--message-format json` stream (a build's events, a test's records), as the
// adapters consume it: {"type": "plan" | "progress" | "diagnostic" | "artifact" | "test" | "summary" |
// "other", ...}.
Json stream_record(const Json& record);

// "1.5 GiB" -- binary units, one decimal.
std::string format_bytes(std::int64_t bytes);

}  // namespace mcppide::views

namespace mcppide::views {

namespace {
const Json& data_of(const Json& envelope) {
    static const Json empty = Json::object();
    if (envelope.is_object() && envelope.contains("data") && envelope["data"].is_object()) return envelope["data"];
    return empty;
}
std::int64_t now_seconds(std::int64_t now) {
    if (now != 0) return now;
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::system_clock::now().time_since_epoch()).count();
}
std::string lower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
}  // namespace

std::string format_bytes(std::int64_t bytes) {
    constexpr std::array<std::string_view, 5> units = {"B", "KiB", "MiB", "GiB", "TiB"};
    double v = static_cast<double>(std::max<std::int64_t>(0, bytes));
    std::size_t u = 0;
    while (v >= 1024.0 && u + 1 < units.size()) { v /= 1024.0; ++u; }
    if (u == 0) return std::format("{} B", bytes);
    return std::format("{:.1f} {}", v, units[u]);
}

Json cache_inventory(const Json& envelope, const CacheOptions& options) {
    const auto& data = data_of(envelope);
    const auto now = now_seconds(options.now);
    std::int64_t total = 0;
    std::map<std::string, std::pair<std::int64_t, std::int64_t>> byKind;   // kind -> entries, bytes
    struct Label { std::int64_t entries = 0, bytes = 0; std::optional<std::int64_t> oldest; };
    std::map<std::string, Label> byLabel;
    std::vector<int> bounds = options.ageBucketDays;
    std::ranges::sort(bounds);
    Json buckets = Json::array();
    for (std::size_t i = 0; i <= bounds.size(); ++i) {
        Json b{{"fromDays", i == 0 ? 0 : bounds[i - 1]}, {"entries", 0}, {"bytes", 0}};
        if (i < bounds.size()) b["toDays"] = bounds[i];
        buckets.push_back(b);
    }
    std::optional<std::int64_t> oldest, newest;
    Json incomplete = Json::array();
    std::int64_t count = 0;
    if (data.contains("entries") && data["entries"].is_array())
        for (const auto& e : data["entries"]) {
            if (!e.is_object() || !e.contains("bytes") || !e["bytes"].is_number()) continue;
            const auto bytes = std::max<std::int64_t>(0, e["bytes"].get<std::int64_t>());
            const auto kind = e.value("kind", "unknown");
            const auto label = e.value("label", "");
            ++count;
            total += bytes;
            byKind[kind].first += 1;
            byKind[kind].second += bytes;
            auto& l = byLabel[label];
            l.entries += 1;
            l.bytes += bytes;
            if (e.contains("accessed") && e["accessed"].is_number()) {
                const auto at = e["accessed"].get<std::int64_t>();
                if (!l.oldest || at < *l.oldest) l.oldest = at;
                if (!oldest || at < *oldest) oldest = at;
                if (!newest || at > *newest) newest = at;
                const double days = static_cast<double>(now - at) / 86400.0;
                std::size_t idx = 0;
                while (idx < bounds.size() && days >= bounds[idx]) ++idx;
                buckets[idx]["entries"] = buckets[idx]["entries"].get<std::int64_t>() + 1;
                buckets[idx]["bytes"] = buckets[idx]["bytes"].get<std::int64_t>() + bytes;
            }
            if (!e.value("complete", false)) incomplete.push_back(e);
        }
    Json kinds = Json::array();
    for (const auto& [k, v] : byKind) kinds.push_back(Json{{"kind", k}, {"entries", v.first}, {"bytes", v.second}});
    std::sort(kinds.begin(), kinds.end(), [](const Json& a, const Json& b) {
        return std::tuple(-a["bytes"].get<std::int64_t>(), -a["entries"].get<std::int64_t>(), a["kind"].get<std::string>()) <
               std::tuple(-b["bytes"].get<std::int64_t>(), -b["entries"].get<std::int64_t>(), b["kind"].get<std::string>());
    });
    std::vector<std::pair<std::string, Label>> ranked(byLabel.begin(), byLabel.end());
    std::ranges::sort(ranked, [](const auto& a, const auto& b) {
        return std::tuple(-a.second.bytes, -a.second.entries, a.first) < std::tuple(-b.second.bytes, -b.second.entries, b.first);
    });
    Json top = Json::array();
    for (std::size_t i = 0; i < ranked.size() && i < static_cast<std::size_t>(std::max(0, options.topN)); ++i) {
        Json t{{"label", ranked[i].first}, {"entries", ranked[i].second.entries}, {"bytes", ranked[i].second.bytes}};
        if (ranked[i].second.oldest) t["oldestAccessed"] = *ranked[i].second.oldest;
        top.push_back(t);
    }
    Json out{{"root", data.value("root", "")}, {"totalBytes", total}, {"totalEntries", count},
             {"totalText", format_bytes(total)}, {"byKind", kinds}, {"topLabels", top},
             {"incomplete", incomplete}, {"ageBuckets", buckets}};
    if (oldest) out["oldestAccessed"] = *oldest;
    if (newest) out["newestAccessed"] = *newest;
    return out;
}

Json cache_preview(const Json& envelope, const policy::CleanPlan& plan, const policy::CleanOptions& clean,
                   std::int64_t nowIn) {
    const auto& data = data_of(envelope);
    const auto now = now_seconds(nowIn);
    std::vector<Json> entries;
    if (data.contains("entries") && data["entries"].is_array())
        for (const auto& e : data["entries"]) if (e.is_object()) entries.push_back(e);
    std::vector<Json> chosen;
    const auto accessed = [](const Json& e) { return e.contains("accessed") && e["accessed"].is_number() ? e["accessed"].get<std::int64_t>() : 0; };
    const auto bytes = [](const Json& e) { return e.contains("bytes") && e["bytes"].is_number() ? e["bytes"].get<std::int64_t>() : 0; };
    if (plan.action == "cacheDeps") {
        for (const auto& e : entries) if (e.value("kind", "") == "pkg") chosen.push_back(e);
    } else if (plan.action == "cacheStd") {
        for (const auto& e : entries) if (e.value("kind", "") == "std") chosen.push_back(e);
    } else if (plan.action == "cacheAll") {
        chosen = entries;
    } else if (plan.action == "cachePrune") {
        const auto cutoff = now - static_cast<std::int64_t>(std::max(1, clean.pruneAgeDays)) * 86400;
        for (const auto& e : entries) if (accessed(e) != 0 && accessed(e) < cutoff) chosen.push_back(e);
    } else if (plan.action == "cacheGc") {
        // Least recently used package entries go until the rest fits the budget.
        std::vector<Json> pkgs;
        std::int64_t total = 0;
        for (const auto& e : entries) if (e.value("kind", "") == "pkg") { pkgs.push_back(e); total += bytes(e); }
        std::ranges::sort(pkgs, {}, accessed);
        const auto budget = static_cast<std::int64_t>(clean.budgetGiB * 1024.0 * 1024.0 * 1024.0);
        if (budget > 0)
            for (const auto& e : pkgs) {
                if (total <= budget) break;
                chosen.push_back(e);
                total -= bytes(e);
            }
    }
    std::int64_t freed = 0;
    for (const auto& e : chosen) freed += bytes(e);
    Json list = Json::array();
    for (const auto& e : chosen) list.push_back(Json{{"label", e.value("label", "")}, {"kind", e.value("kind", "")},
                                                     {"bytes", bytes(e)}, {"dir", e.value("dir", "")}});
    return Json{{"action", plan.action}, {"entries", list}, {"count", chosen.size()},
                {"bytes", freed}, {"bytesText", format_bytes(freed)}, {"estimated", true}};
}

Json toolchains(const Json& envelope) {
    const auto& data = data_of(envelope);
    Json rows = Json::array();
    Json defaults = Json::array();
    if (data.contains("targets") && data["targets"].is_array())
        for (const auto& t : data["targets"]) {
            if (!t.is_object()) continue;
            Json row{{"target", t.value("target", "")}, {"toolchain", t.value("toolchain", "")},
                     {"pin", t.value("pin", "")}, {"status", t.value("status", "")},
                     {"default", t.value("default", false)}, {"note", t.value("note", "")}};
            // "gcc 16.1.0" -> the spec a command takes: "gcc@16.1.0"; a target with no toolchain
            // ("—") has none to offer.
            auto tc = row["toolchain"].get<std::string>();
            const auto sp = tc.find(' ');
            if (sp != std::string::npos) tc[sp] = '@';
            row["spec"] = sp == std::string::npos ? std::string{} : tc;
            if (row["default"].get<bool>()) defaults.push_back(row);
            rows.push_back(std::move(row));
        }
    return Json{{"host", data.value("host", "")}, {"rows", rows}, {"defaults", defaults}};
}

Json library(const Json& envelope, const LibraryQuery& query) {
    const auto& data = data_of(envelope);
    std::vector<Json> packages;
    const auto needle = lower(query.keyword);
    if (data.contains("packages") && data["packages"].is_array())
        for (const auto& p : data["packages"]) {
            if (!p.is_object()) continue;
            if (!query.index.empty() && p.value("index", "") != query.index) continue;
            if (!needle.empty()) {
                const auto hay = lower(p.value("id", "") + " " + p.value("description", ""));
                if (hay.find(needle) == std::string::npos) continue;
            }
            Json row = p;
            const auto& versions = p.contains("versions") && p["versions"].is_array() ? p["versions"] : Json::array();
            row["latest"] = versions.empty() ? Json(nullptr) : versions.front();
            row["versionCount"] = versions.size();
            // What an adapter writes into mcpp.toml, through `mcpp add`.
            row["dependencyName"] = p.value("namespace", "").empty() || p.value("namespace", "") == p.value("index", "")
                                        ? p.value("name", "")
                                        : p.value("namespace", "") + "." + p.value("name", "");
            packages.push_back(std::move(row));
        }
    if (query.sort == "versions")
        std::ranges::sort(packages, [](const Json& a, const Json& b) {
            return std::pair(-a["versionCount"].get<long long>(), a.value("id", "")) <
                   std::pair(-b["versionCount"].get<long long>(), b.value("id", ""));
        });
    else
        std::ranges::sort(packages, {}, [](const Json& p) { return lower(p.value("id", "")); });
    Json out = Json::array();
    for (std::size_t i = 0; i < packages.size() && i < query.limit; ++i) out.push_back(packages[i]);
    std::set<std::string> indexes;
    if (data.contains("packages") && data["packages"].is_array())
        for (const auto& p : data["packages"]) if (p.is_object()) indexes.insert(p.value("index", ""));
    return Json{{"keyword", query.keyword}, {"total", packages.size()}, {"packages", out},
                {"indexes", Json(std::vector<std::string>(indexes.begin(), indexes.end()))}};
}

Json library_detail(const Json& envelope, std::string_view id) {
    const auto& data = data_of(envelope);
    if (data.contains("packages") && data["packages"].is_array())
        for (const auto& p : data["packages"])
            if (p.is_object() && p.value("id", "") == id) {
                auto all = library(envelope, LibraryQuery{p.value("name", ""), p.value("index", ""), "name", 1000});
                for (const auto& row : all["packages"]) if (row.value("id", "") == id) return row;
                return p;
            }
    return nullptr;
}

namespace {
const Json& sets_of(const Json& envelope) {
    static const Json empty = Json::array();
    const auto& data = data_of(envelope);
    if (data.contains("database") && data["database"].is_object() && data["database"].contains("sets") &&
        data["database"]["sets"].is_array())
        return data["database"]["sets"];
    return empty;
}
}  // namespace

Json package_graph(const Json& envelope) {
    Json nodes = Json::array(), edges = Json::array();
    std::set<std::string> seen;
    for (const auto& s : sets_of(envelope)) {
        const auto name = s.value("name", "");
        if (name.empty() || !seen.insert(name).second) continue;
        const auto& ide = s.contains("ide") && s["ide"].is_object() ? s["ide"] : Json::object();
        nodes.push_back(Json{{"id", name}, {"family", s.value("family-name", name)}, {"kind", ide.value("kind", "")},
                             {"units", s.contains("translation-units") ? s["translation-units"].size() : 0}});
        if (s.contains("visible-sets") && s["visible-sets"].is_array())
            for (const auto& v : s["visible-sets"])
                if (v.is_string() && v.get<std::string>() != name)
                    edges.push_back(Json{{"from", name}, {"to", v.get<std::string>()}});
    }
    return Json{{"nodes", nodes}, {"edges", edges}};
}

Json module_graph(const Json& envelope) {
    std::map<std::string, Json> modules;   // module name -> node
    Json edges = Json::array();
    std::set<std::pair<std::string, std::string>> seenEdges;
    for (const auto& s : sets_of(envelope)) {
        const auto set = s.value("name", "");
        if (!s.contains("translation-units") || !s["translation-units"].is_array()) continue;
        for (const auto& tu : s["translation-units"]) {
            const auto source = tu.value("source", "");
            std::vector<std::string> provided;
            if (tu.contains("provides") && tu["provides"].is_object())
                for (auto it = tu["provides"].begin(); it != tu["provides"].end(); ++it) provided.push_back(it.key());
            // A unit that provides nothing is named by its file.
            const std::string self = provided.empty() ? source : provided.front();
            for (const auto& m : provided)
                modules.try_emplace(m, Json{{"id", m}, {"kind", "module"}, {"set", set}, {"source", source}});
            if (provided.empty())
                modules.try_emplace(self, Json{{"id", self}, {"kind", "unit"}, {"set", set}, {"source", source}});
            if (tu.contains("requires") && tu["requires"].is_array())
                for (const auto& r : tu["requires"]) {
                    if (!r.is_string()) continue;
                    if (seenEdges.insert({self, r.get<std::string>()}).second)
                        edges.push_back(Json{{"from", self}, {"to", r.get<std::string>()}});
                }
        }
    }
    Json nodes = Json::array();
    for (auto& [name, node] : modules) nodes.push_back(node);
    // A module imported and provided by no unit of the build (a header unit, a
    // module of a package not yet built) is still drawn, marked external.
    std::set<std::string> known;
    for (const auto& n : nodes) known.insert(n["id"].get<std::string>());
    for (const auto& e : edges)
        if (!known.contains(e["to"].get<std::string>())) {
            known.insert(e["to"].get<std::string>());
            nodes.push_back(Json{{"id", e["to"]}, {"kind", "external"}});
        }
    return Json{{"nodes", nodes}, {"edges", edges}};
}

Json project_summary(const Json& envelope, const Json& lastResult) {
    Json targets = Json::array(), tests = Json::array(), libraries = Json::array();
    std::string toolchain;
    for (const auto& s : sets_of(envelope)) {
        const auto name = s.value("name", "");
        const auto& ide = s.contains("ide") && s["ide"].is_object() ? s["ide"] : Json::object();
        const auto kind = ide.value("kind", "");
        if (name.starts_with("mcpp:")) continue;   // the engine's own sets (std)
        Json row{{"name", name}, {"kind", kind}, {"configuration", ide.value("configuration", "")}};
        if (toolchain.empty()) toolchain = ide.value("toolchain", "");
        if (kind == "test") tests.push_back(row);
        else if (kind == "executable") targets.push_back(row);
        else libraries.push_back(row);
    }
    Json out{{"targets", targets}, {"tests", tests}, {"libraries", libraries}, {"toolchain", toolchain}};
    if (lastResult.is_object() && lastResult.contains("record")) {
        const auto& r = lastResult["record"];
        out["lastBuild"] = Json{{"status", r.value("status", "")}, {"finishedAt", r.contains("finishedAt") ? r["finishedAt"] : Json(nullptr)},
                                {"client", r.value("client", "")}, {"command", r.value("command", "")}};
        out["artifacts"] = r.contains("artifacts") ? r["artifacts"] : Json::array();
        out["compileDatabase"] = r.contains("compileDatabase") ? r["compileDatabase"] : Json(nullptr);
    }
    return out;
}

// A platform as the target table names it (`mcpp toolchain list`: `[ns.]name@version`), as the
// object a plan names one with: its name, version and where the choice came from.
Json platform_of_spec(const Json& spec) {
    if (!spec.is_string()) return spec.is_object() ? spec : Json(nullptr);
    const auto text = spec.get<std::string>();
    const auto at = text.rfind('@');
    return Json{{"name", at == std::string::npos ? text : text.substr(0, at)},
                {"version", at == std::string::npos ? Json(nullptr) : Json(text.substr(at + 1))},
                {"source", "target table"}};
}

std::string platform_text(const Json& p) {
    if (!p.is_object()) return {};
    const auto name = p.value("name", "");
    const auto version = p.contains("version") && p["version"].is_string() ? p["version"].get<std::string>() : std::string{};
    const auto source = p.value("source", "");
    std::string out = name.empty() ? std::string("host conventions") : name;
    if (!version.empty()) out += " " + version;
    if (!source.empty()) out += " (" + source + ")";
    return out;
}

Json targets(const Json& envelope, std::string_view selected) {
    const auto& data = data_of(envelope);
    const auto host = data.value("host", "");
    std::map<std::string, Json> rows;
    std::vector<std::string> order;
    if (data.contains("targets") && data["targets"].is_array())
        for (const auto& t : data["targets"]) {
            if (!t.is_object()) continue;
            const auto target = t.value("target", "");
            if (target.empty()) continue;
            auto [it, fresh] = rows.try_emplace(target, Json{{"target", target}, {"note", t.value("note", "")},
                                                             {"pin", t.value("pin", "")}, {"tier", t.value("tier", "")},
                                                             {"host", target == host}, {"toolchains", Json::array()},
                                                             {"platform", nullptr}});
            if (fresh) order.push_back(target);
            auto& row = it->second;
            // mcpp reports the platform per row of the target table; every row of one target names the same.
            if (row["platform"].is_null() && t.contains("platform")) row["platform"] = platform_of_spec(t["platform"]);
            auto tc = t.value("toolchain", "");
            const auto sp = tc.find(' ');
            if (sp != std::string::npos) tc[sp] = '@';
            if (sp != std::string::npos || !t.value("status", "").empty())
                row["toolchains"].push_back(Json{{"spec", sp == std::string::npos ? std::string{} : tc},
                                                 {"status", t.value("status", "")}, {"default", t.value("default", false)}});
        }
    Json out = Json::array();
    const std::string chosen = selected.empty() ? host : std::string(selected);
    for (const auto& name : order) {
        auto row = rows[name];
        row["platformText"] = platform_text(row["platform"]);
        row["selected"] = name == chosen;
        bool installed = false;
        for (const auto& tc : row["toolchains"]) installed = installed || tc.value("status", "") == "installed";
        row["installed"] = installed;
        out.push_back(std::move(row));
    }
    Json result{{"host", host}, {"selected", chosen}, {"rows", out}, {"hostPlatform", nullptr}};
    if (data.contains("hostPlatform") && data["hostPlatform"].is_object()) {
        result["hostPlatform"] = data["hostPlatform"];
        result["hostPlatformText"] = platform_text(data["hostPlatform"]);
    }
    return result;
}

Json stream_record(const Json& r) {
    if (!r.is_object()) return Json{{"type", "other"}, {"record", r}};
    // `mcpp test --message-format json`: one record per test, then the summary.
    if (r.contains("summary") && r["summary"].is_object()) {
        const auto& s = r["summary"];
        return Json{{"type", "summary"}, {"member", s.value("member", "")}, {"passed", s.value("passed", 0)},
                    {"failed", s.value("failed", 0)}, {"notRun", s.value("not_run", 0)},
                    {"notRunReason", s.value("not_run_reason", "")}, {"elapsedMs", s.value("elapsed_ms", 0)}};
    }
    if (r.contains("test") && r["test"].is_string()) {
        const auto raw = r.value("status", "");
        const std::string status = raw == "pass" ? "passed"
                                 : raw == "fail" ? (r.value("timed_out", false) ? "timedOut" : "failed")
                                 : raw == "skip" || raw == "not_run" ? "skipped" : "errored";
        std::string output = r.value("compile_output", "");
        output += r.value("run_output", "");
        return Json{{"type", "test"}, {"member", r.value("member", "")}, {"test", r["test"]}, {"status", status},
                    {"durationMs", r.value("duration_ms", 0)}, {"exitCode", r.contains("exit_code") ? r["exit_code"] : Json(nullptr)},
                    {"reason", r.value("reason", "")}, {"output", output}};
    }
    const auto event = r.value("event", "");
    if (r.value("kind", "") == "mcpp.event") {
        if (event == "plan") {
            // The platforms are the configuration's (mcpp's plan event: `configuration.platform`,
            // `configuration.hostPlatform`).
            const Json cfg = r.contains("configuration") ? r["configuration"] : Json(nullptr);
            auto field = [&](const char* key) {
                if (cfg.is_object() && cfg.contains(key)) return cfg[key];
                return r.contains(key) ? r[key] : Json(nullptr);
            };
            Json out{{"type", "plan"}, {"configuration", cfg},
                     {"platform", field("platform")}, {"hostPlatform", field("hostPlatform")}};
            out["platformText"] = platform_text(out["platform"]);
            out["hostPlatformText"] = platform_text(out["hostPlatform"]);
            return out;
        }
        if (event == "progress")
            return Json{{"type", "progress"}, {"finished", r.value("finished", 0)}, {"total", r.value("total", 0)}};
        if (event == "artifact" && r.contains("artifact") && r["artifact"].is_object())
            return Json{{"type", "artifact"}, {"artifact", r["artifact"]}};
        if (event == "diagnostic" && r.contains("diagnostic") && r["diagnostic"].is_object()) {
            const auto& d = r["diagnostic"];
            Json n{{"type", "diagnostic"}, {"severity", d.value("severity", "error")}, {"message", d.value("message", "")},
                   {"code", d.value("code", "")}, {"source", "mcpp"}};
            if (d.contains("path") && d["path"].is_string()) n["path"] = d["path"];
            if (d.contains("range") && d["range"].is_object()) n["range"] = d["range"];
            return n;
        }
    }
    return Json{{"type", "other"}, {"record", r}};
}

}  // namespace mcppide::views
