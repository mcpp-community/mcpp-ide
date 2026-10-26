// Cleanup, as plans rather than as calls (ported from mcpp-vscode's
// src/cli/clean.ts, whose table both adapters now read from here).
//
// Every destructive action an adapter can run is described here: the exact
// argv, how much confirmation it needs, and whether a preview must be shown
// first. One table means an adapter cannot invent a sixth level of danger.
//
// | level | what                                                     | confirmation                       |
// |-------|----------------------------------------------------------|------------------------------------|
// | 0     | read-only (`cache list`, `cache verify`)                 | none                               |
// | 1     | the project's body (`mcpp clean`)                        | modal, with the measured size      |
// | 2     | budget / targeted (`cache gc`, `prune`, `--deps`/`--std`) | preview, then modal                |
// | 3     | everything (`cache clean --all`)                         | preview, modal, acknowledgement    |
export module mcppide.policy;

import std;
import nlohmann.json;

export namespace mcppide::policy {

using Json = nlohmann::json;

struct CleanOptions {
    int staleDays = 3;        // `mcpp clean --stale --older-than <n>d`
    double budgetGiB = 0;     // `mcpp cache gc --max-size <n>GiB`; 0: no budget
    int pruneAgeDays = 30;    // `mcpp cache prune --older-than <n>d`
};

struct CleanPlan {
    std::string action;
    std::string group;                  // "clean" | "cache"
    std::vector<std::string> argv;      // mcpp's arguments
    int level = 0;
    bool preview = false;               // a `--dry-run` pass shown before asking
    bool acknowledge = false;           // a second, explicit acknowledgement
    std::string titleKey;               // English keys of the shared i18n bundle
    std::string detailKey;
    bool requiresTrust = false;         // refused in an untrusted workspace
    // The preview: mcpp's own `--dry-run` list when it has one (`clean
    // --stale`), else the backend's reading of the cache inventory (`mcpp
    // cache` has no dry run; views::cache_preview answers what would go).
    std::vector<std::string> previewArgv;
    std::string previewSource;          // "mcpp" | "inventory" | ""
};

inline constexpr std::array<std::string_view, 10> kActions = {
    "project", "stale", "cacheGc", "cachePrune", "cacheDeps",
    "cacheStd", "cacheAll", "cacheLegacy", "cacheVerify", "cacheList"};

std::optional<CleanPlan> plan(std::string_view action, const CleanOptions& options);
Json to_json(const CleanPlan& p);
// Whether `confirmation` (what the adapter collected) satisfies the plan.
bool confirmed(const CleanPlan& p, const Json& confirmation);

}  // namespace mcppide::policy

namespace mcppide::policy {

namespace {
std::string format_gib(double v) {
    if (v == std::floor(v)) return std::format("{}GiB", static_cast<long long>(v));
    return std::format("{:.1f}GiB", v);
}
}  // namespace

std::optional<CleanPlan> plan(std::string_view action, const CleanOptions& options) {
    CleanPlan p;
    p.action = std::string(action);
    const int stale = std::max(0, options.staleDays);
    const int prune = std::max(1, options.pruneAgeDays);
    if (action == "project") {
        p.group = "clean"; p.argv = {"clean"}; p.level = 1;
        p.titleKey = "Remove the whole target/ directory?";
        p.detailKey = "This deletes every build directory of this project. The next build starts from scratch. The shared build cache is not touched.";
        p.requiresTrust = true;
    } else if (action == "stale") {
        p.group = "clean";
        p.argv = {"clean", "--stale", "--older-than", std::format("{}d", stale)};
        p.previewArgv = {"clean", "--stale", "--older-than", std::format("{}d", stale), "--dry-run"};
        p.level = 2; p.preview = true; p.previewSource = "mcpp";
        p.titleKey = "Remove build directories mcpp no longer considers current?";
        p.detailKey = "Directories written in the last {0} day(s) are kept. mcpp decides which ones are stale; the preview above is its own list.";
        p.requiresTrust = true;
    } else if (action == "cacheGc") {
        p.group = "cache";
        p.argv = options.budgetGiB > 0 ? std::vector<std::string>{"cache", "gc", "--max-size", format_gib(options.budgetGiB)}
                                       : std::vector<std::string>{"cache", "gc"};
        p.level = 2; p.preview = true;
        p.titleKey = "Collect the shared build cache to a budget?";
        p.detailKey = "mcpp drops least-recently-used entries until the cache fits. Projects that used a dropped entry rebuild it.";
        p.requiresTrust = true;
    } else if (action == "cachePrune") {
        p.group = "cache";
        p.argv = {"cache", "prune", "--older-than", std::format("{}d", prune)};
        p.level = 2; p.preview = true;
        p.titleKey = "Drop cache entries that have not been used for a while?";
        p.detailKey = "Entries unused for longer than {0} day(s) are removed. Projects that used one rebuild it.";
        p.requiresTrust = true;
    } else if (action == "cacheDeps") {
        p.group = "cache"; p.argv = {"cache", "clean", "--deps"};
        p.level = 2; p.preview = true;
        p.titleKey = "Drop every package entry from the shared cache?";
        p.detailKey = "Every mcpp project on this machine rebuilds its dependencies afterwards. Standard library module entries are kept.";
        p.requiresTrust = true;
    } else if (action == "cacheStd") {
        p.group = "cache"; p.argv = {"cache", "clean", "--std"};
        p.level = 2; p.preview = true;
        p.titleKey = "Drop every standard library module entry from the shared cache?";
        p.detailKey = "Every mcpp project on this machine re-prepares the standard library module afterwards. Package entries are kept.";
        p.requiresTrust = true;
    } else if (action == "cacheAll") {
        p.group = "cache"; p.argv = {"cache", "clean", "--all"};
        p.level = 3; p.preview = true; p.acknowledge = true;
        p.titleKey = "Drop the entire shared build cache?";
        p.detailKey = "Package and standard library entries go. Every mcpp project on this machine rebuilds from scratch afterwards.";
        p.requiresTrust = true;
    } else if (action == "cacheLegacy") {
        p.group = "cache"; p.argv = {"cache", "clean", "--legacy"};
        p.level = 1;
        p.titleKey = "Remove the cache directory of earlier mcpp releases?";
        p.detailKey = "Only the directory older mcpp releases wrote is removed; the current cache is kept.";
        p.requiresTrust = true;
    } else if (action == "cacheVerify") {
        p.group = "cache"; p.argv = {"cache", "verify"}; p.level = 0;
        p.titleKey = "Verify the shared build cache";
        p.detailKey = "Read-only: every entry is checked against its recorded contents.";
    } else if (action == "cacheList") {
        p.group = "cache"; p.argv = {"cache", "list", "--format", "json"}; p.level = 0;
        p.titleKey = "List the shared build cache";
        p.detailKey = "Read-only.";
    } else {
        return std::nullopt;
    }
    if (p.preview && p.previewSource.empty()) p.previewSource = "inventory";
    return p;
}

Json to_json(const CleanPlan& p) {
    return Json{{"action", p.action},       {"group", p.group},          {"argv", p.argv},
                {"level", p.level},         {"preview", p.preview},      {"acknowledge", p.acknowledge},
                {"titleKey", p.titleKey},   {"detailKey", p.detailKey},  {"requiresTrust", p.requiresTrust},
                {"previewArgv", p.previewArgv}, {"previewSource", p.previewSource}};
}

bool confirmed(const CleanPlan& p, const Json& c) {
    if (p.level == 0) return true;
    if (!c.is_object() || !c.value("confirmed", false)) return false;
    if (p.preview && !c.value("previewShown", false)) return false;
    if (p.acknowledge && !c.value("acknowledged", false)) return false;
    return true;
}

}  // namespace mcppide::policy
