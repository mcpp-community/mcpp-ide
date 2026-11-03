// Projects: which mcpp.toml a file belongs to, and what changed on disk that
// an editor shows.
//
// A FILE BELONGS TO THE NEAREST mcpp.toml ABOVE IT; that manifest's project is
// the package, and an outer manifest whose `[workspace]` table names members is
// the workspace the package builds in. The backend reads only that one table
// header -- whether a manifest is a workspace root -- and asks mcpp everything
// else (mcpp's `emit build-database` names the members and their targets).
//
// WHAT IS WATCHED. Each project's `target/.last-result.json` (the result
// record mcpp writes after every build, whoever ran it: the IDE, a terminal,
// an agent) and its `mcpp.toml`. Polled, because the record is replaced by a
// rename, which some platforms' change notifications do not report for a file
// that did not exist before, and because two files per project cost nothing.
export module mcppide.projects;

import std;
import nlohmann.json;

export namespace mcppide::projects {

using Json = nlohmann::json;

struct Project {
    std::filesystem::path root;            // the package's directory
    std::filesystem::path workspaceRoot;   // the workspace's directory, or root
    bool isWorkspaceRoot = false;
};

// The nearest project above `path` (a file or a directory), no higher than
// `boundary` when it is given.
std::optional<Project> project_for(const std::filesystem::path& path, const std::filesystem::path& boundary = {});
// Every project under a folder, at most `depth` directories down, skipping
// target/ and hidden directories.
std::vector<Project> discover(const std::filesystem::path& folder, int depth = 4);
bool declares_workspace(const std::filesystem::path& manifest);

// The record `target/.last-result.json` names, or null.
Json last_result(const std::filesystem::path& root);

// What an editor reads of a project to describe it (mcpp-vscode's project view, its graphs and
// libraries): the text of mcpp.toml and mcpp.lock (null when absent) and how many source files lie
// under the root, counted as mcpp-vscode 0.9.0 counted them -- the C and C++ suffixes, at most 8
// directories down and 20000 entries, not into target/, build/, out/, dist/, node_modules/, .git/,
// .mcpp/, .vscode/ or .cache/, not through symbolic links.
Json files(const std::filesystem::path& root);
std::int64_t count_source_files(const std::filesystem::path& root);

class Watcher {
public:
    using Changed = std::function<void(const std::filesystem::path& root, std::string_view what, const Json& detail)>;
    explicit Watcher(Changed changed, std::chrono::milliseconds period = std::chrono::milliseconds(500));
    ~Watcher();
    void watch(const std::filesystem::path& root);
    void unwatch(const std::filesystem::path& root);
    std::vector<std::filesystem::path> watched() const;
    // One poll, now (the tests).
    void poll();

private:
    struct Stamps {
        std::optional<std::filesystem::file_time_type> result;
        std::optional<std::filesystem::file_time_type> manifest;
    };
    Changed changed_;
    std::chrono::milliseconds period_;
    mutable std::mutex mutex_;
    std::map<std::filesystem::path, Stamps> roots_;
    std::jthread thread_;
};

}  // namespace mcppide::projects

namespace mcppide::projects {

namespace {
std::optional<std::filesystem::file_time_type> stamp(const std::filesystem::path& p) {
    std::error_code ec;
    auto t = std::filesystem::last_write_time(p, ec);
    if (ec) return std::nullopt;
    return t;
}
}  // namespace

bool declares_workspace(const std::filesystem::path& manifest) {
    std::ifstream in(manifest, std::ios::binary);
    for (std::string line; std::getline(in, line);) {
        std::string_view v(line);
        while (!v.empty() && (v.front() == ' ' || v.front() == '\t')) v.remove_prefix(1);
        if (v.starts_with("[workspace]") || v.starts_with("[workspace ]")) return true;
    }
    return false;
}

std::optional<Project> project_for(const std::filesystem::path& path, const std::filesystem::path& boundary) {
    std::error_code ec;
    auto dir = std::filesystem::is_directory(path, ec) ? path : path.parent_path();
    dir = std::filesystem::weakly_canonical(dir, ec);
    const auto stop = boundary.empty() ? std::filesystem::path{} : std::filesystem::weakly_canonical(boundary, ec);
    std::optional<Project> found;
    for (auto d = dir;; d = d.parent_path()) {
        if (std::filesystem::is_regular_file(d / "mcpp.toml", ec)) {
            if (!found) {
                found = Project{d, d, declares_workspace(d / "mcpp.toml")};
                if (found->isWorkspaceRoot) break;
            } else if (declares_workspace(d / "mcpp.toml")) {
                found->workspaceRoot = d;
                break;
            }
        }
        if (d == stop || d == d.parent_path()) break;
    }
    return found;
}

std::vector<Project> discover(const std::filesystem::path& folder, int depth) {
    std::vector<Project> out;
    std::error_code ec;
    std::function<void(const std::filesystem::path&, int)> walk = [&](const std::filesystem::path& d, int left) {
        if (std::filesystem::is_regular_file(d / "mcpp.toml", ec))
            if (auto p = project_for(d, folder)) out.push_back(*p);
        if (left == 0) return;
        for (std::filesystem::directory_iterator it(d, ec), end; !ec && it != end; it.increment(ec)) {
            if (!it->is_directory(ec)) continue;
            const auto name = it->path().filename().string();
            if (name.empty() || name.front() == '.' || name == "target" || name == "node_modules") continue;
            walk(it->path(), left - 1);
        }
    };
    walk(folder, depth);
    std::ranges::sort(out, {}, [](const Project& p) { return p.root; });
    out.erase(std::unique(out.begin(), out.end(), [](const Project& a, const Project& b) { return a.root == b.root; }),
              out.end());
    return out;
}

namespace {
std::optional<std::string> slurp_text(const std::filesystem::path& p) {
    std::ifstream in(p, std::ios::binary);
    if (!in) return std::nullopt;
    return std::string(std::istreambuf_iterator<char>(in), {});
}
}  // namespace

std::int64_t count_source_files(const std::filesystem::path& root) {
    static constexpr std::array<std::string_view, 14> kSuffixes = {".c", ".cc", ".cpp", ".cxx", ".c++", ".cppm", ".ixx",
                                                                   ".mpp", ".h", ".hh", ".hpp", ".hxx", ".ipp", ".inl"};
    static constexpr std::array<std::string_view, 9> kSkip = {"target", "build", "out", "dist", "node_modules", ".git",
                                                              ".mcpp", ".vscode", ".cache"};
    constexpr int kMaxDepth = 8;
    constexpr std::int64_t kMaxEntries = 20000;
    std::int64_t count = 0, seen = 0;
    // Depth first, as the extension walked: a directory's entries, each directory entered in turn.
    std::function<void(const std::filesystem::path&, int)> visit = [&](const std::filesystem::path& dir, int depth) {
        if (depth > kMaxDepth || seen >= kMaxEntries) return;
        std::error_code ec;
        std::filesystem::directory_iterator it(dir, ec), end;
        if (ec) return;
        for (; it != end; it.increment(ec)) {
            if (ec) return;
            if (++seen > kMaxEntries) return;
            const auto& e = *it;
            if (e.is_symlink(ec)) continue;
            const auto name = e.path().filename().string();
            if (e.is_directory(ec)) {
                if (std::ranges::find(kSkip, name) == kSkip.end()) visit(e.path(), depth + 1);
            } else if (e.is_regular_file(ec)) {
                auto ext = e.path().extension().string();
                std::ranges::transform(ext, ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                if (std::ranges::find(kSuffixes, ext) != kSuffixes.end()) ++count;
            }
        }
    };
    visit(root, 0);
    return count;
}

Json files(const std::filesystem::path& root) {
    auto text = [](const std::optional<std::string>& t) { return t ? Json(*t) : Json(nullptr); };
    return Json{{"root", root.generic_string()},
                {"manifest", text(slurp_text(root / "mcpp.toml"))},
                {"lock", text(slurp_text(root / "mcpp.lock"))},
                {"sourceFiles", count_source_files(root)}};
}

Json last_result(const std::filesystem::path& root) {
    std::ifstream in(root / "target" / ".last-result.json", std::ios::binary);
    if (!in) return nullptr;
    auto last = Json::parse(in, nullptr, false);
    if (last.is_discarded() || !last.contains("data") || !last["data"].contains("result")) return nullptr;
    const auto& d = last["data"];
    std::filesystem::path record = d["result"].is_string() ? d["result"].get<std::string>() : std::string{};
    std::ifstream rin(record, std::ios::binary);
    auto doc = rin ? Json::parse(rin, nullptr, false) : Json(nullptr);
    Json out{{"status", d.value("status", "")}, {"path", record.generic_string()}};
    if (!doc.is_discarded() && doc.is_object() && doc.contains("data")) out["record"] = doc["data"];
    return out;
}

Watcher::Watcher(Changed changed, std::chrono::milliseconds period)
    : changed_(std::move(changed)), period_(period) {
    thread_ = std::jthread([this](std::stop_token st) {
        while (!st.stop_requested()) {
            poll();
            std::mutex m;
            std::unique_lock lock(m);
            std::condition_variable_any cv;
            cv.wait_for(lock, st, period_, [] { return false; });
        }
    });
}

Watcher::~Watcher() {
    thread_.request_stop();
    if (thread_.joinable()) thread_.join();
}

void Watcher::watch(const std::filesystem::path& root) {
    std::lock_guard lock(mutex_);
    if (roots_.contains(root)) return;
    roots_[root] = Stamps{stamp(root / "target" / ".last-result.json"), stamp(root / "mcpp.toml")};
}

void Watcher::unwatch(const std::filesystem::path& root) {
    std::lock_guard lock(mutex_);
    roots_.erase(root);
}

std::vector<std::filesystem::path> Watcher::watched() const {
    std::lock_guard lock(mutex_);
    std::vector<std::filesystem::path> out;
    for (const auto& [root, s] : roots_) out.push_back(root);
    return out;
}

void Watcher::poll() {
    std::vector<std::pair<std::filesystem::path, std::string>> events;
    {
        std::lock_guard lock(mutex_);
        for (auto& [root, s] : roots_) {
            const auto r = stamp(root / "target" / ".last-result.json");
            if (r != s.result) { s.result = r; events.push_back({root, "result"}); }
            const auto m = stamp(root / "mcpp.toml");
            if (m != s.manifest) { s.manifest = m; events.push_back({root, "manifest"}); }
        }
    }
    for (const auto& [root, what] : events)
        changed_(root, what, what == "result" ? last_result(root) : Json(nullptr));
}

}  // namespace mcppide::projects
