// The documents the editors keep open, and the rules layer's answers about them in LSP's shapes
// (design 2026-10-30 part 2 §8): the session is the language server of mcpp.toml and build.mcpp, and
// an editor connects to it as to any LSP server -- VS Code's language client, CLion's LSP API,
// Neovim's and Zed's clients. The editor sends the whole text on each change; positions are a
// 0-based line and a UTF-16 column.
//
// What the editor turns off stays off here: each feature reads its `mcpp.toml.*` or
// `mcpp.buildScript.*` setting, and while C++ Modules (mcppls) serves build.mcpp the answers it gives
// better are left to it (the editor says whether it does).
export module mcppide.documents;

import std;
import nlohmann.json;
import mcppide.buildscript;
import mcppide.i18n;
import mcppide.manifest;
import mcppide.text;

export namespace mcppide::documents {

using Json = nlohmann::json;

enum class Kind { manifest, buildscript, other };

// mcpp.toml, build.mcpp, or a file the rules layer does not read.
Kind kind_of(const std::filesystem::path& path);
// A `file:` URI's path (percent-decoded; `file:///C:/x` is `C:/x`); empty for another scheme.
std::filesystem::path path_of(std::string_view uri);
std::string uri_of(const std::filesystem::path& path);

struct Document {
    std::string uri;
    std::filesystem::path path;
    Kind kind = Kind::other;
    int version = 0;
    std::vector<std::string> lines;
};

// The open documents, safe across the session's threads.
class Store {
public:
    void open(const std::string& uri, int version, std::string_view text);
    // The whole text (the server asks for full synchronisation).
    void change(const std::string& uri, int version, std::string_view text);
    void close(const std::string& uri);
    std::optional<Document> get(const std::string& uri) const;
    std::vector<Document> all() const;

private:
    mutable std::mutex mutex_;
    std::map<std::string, Document> documents_;
};

// What the settings turn on, and in what language.
struct Options {
    bool tomlCompletion = true, tomlHover = true, tomlNavigation = true, tomlDiagnostics = true;
    manifest::DiagnosticSettings tomlSeverity;
    bool indexCompletion = false;
    bool bsIntelligence = true, bsDiagnostics = true, bsSnippets = true, bsKnownModules = true;
    std::string bsSeverity = "warning";
    bool mcpplsServes = false;   // C++ Modules serves build.mcpp
    bool trusted = true;
    std::string locale = "en";
};
Options options_of(const Json& settings, bool trusted, bool mcpplsServes, std::string_view locale);

// The capabilities `initialize` answers.
Json server_capabilities();

// LSP Diagnostic[] for a document.
Json diagnostics(const Document& d, const Options& o);

// A CompletionList. `catalogue` is mcpp's package catalogue (`mcpp search --format json`) when one is
// at hand; a dependency version asked without one sets `needCatalogue` and answers an incomplete
// list, which the editor asks again.
Json completion(const Document& d, int line, int character, const Options& o, const Json* catalogue, bool& needCatalogue);
Json hover(const Document& d, int line, int character, const Options& o);
// A Location, or null. `path = "../x"` reads that directory's mcpp.toml for its `[package]`.
Json definition(const Document& d, int line, int character, const Options& o);
Json symbols(const Document& d);

}  // namespace mcppide::documents

namespace mcppide::documents {

Kind kind_of(const std::filesystem::path& path) {
    const auto name = path.filename().string();
    if (name == "mcpp.toml") return Kind::manifest;
    if (name == "build.mcpp") return Kind::buildscript;
    return Kind::other;
}

std::filesystem::path path_of(std::string_view uri) {
    if (!uri.starts_with("file://")) return {};
    auto rest = uri.substr(7);
    // `file://host/share` is a UNC path; `file:///x` has an empty authority.
    std::string authority;
    if (!rest.starts_with("/")) {
        const auto slash = rest.find('/');
        authority = std::string(rest.substr(0, slash));
        rest = slash == std::string_view::npos ? std::string_view() : rest.substr(slash);
    }
    std::string decoded;
    for (std::size_t i = 0; i < rest.size(); ++i) {
        if (rest[i] == '%' && i + 2 < rest.size()) {
            unsigned value = 0;
            if (std::from_chars(rest.data() + i + 1, rest.data() + i + 3, value, 16).ec == std::errc{}) {
                decoded += static_cast<char>(value);
                i += 2;
                continue;
            }
        }
        decoded += rest[i];
    }
    // `/C:/x` is the drive path `C:/x`.
    if (decoded.size() >= 3 && decoded[0] == '/' && std::isalpha(static_cast<unsigned char>(decoded[1])) && decoded[2] == ':')
        decoded.erase(0, 1);
    if (!authority.empty() && authority != "localhost") return std::filesystem::path("//" + authority + decoded);
    return std::filesystem::path(decoded);
}

std::string uri_of(const std::filesystem::path& path) {
    auto p = path.generic_string();
    std::string out = "file://";
    if (!p.starts_with("/")) out += '/';   // a drive path
    for (const unsigned char c : p) {
        if (std::isalnum(c) || c == '/' || c == '-' || c == '_' || c == '.' || c == '~' || c == ':') out += static_cast<char>(c);
        else out += std::format("%{:02X}", c);
    }
    return out;
}

void Store::open(const std::string& uri, int version, std::string_view text) {
    const auto path = path_of(uri);
    std::lock_guard lock(mutex_);
    documents_[uri] = Document{uri, path, kind_of(path), version, text::lines_of(text)};
}

void Store::change(const std::string& uri, int version, std::string_view text) {
    std::lock_guard lock(mutex_);
    auto it = documents_.find(uri);
    if (it == documents_.end()) {
        const auto path = path_of(uri);
        documents_[uri] = Document{uri, path, kind_of(path), version, text::lines_of(text)};
        return;
    }
    it->second.version = version;
    it->second.lines = text::lines_of(text);
}

void Store::close(const std::string& uri) {
    std::lock_guard lock(mutex_);
    documents_.erase(uri);
}

std::optional<Document> Store::get(const std::string& uri) const {
    std::lock_guard lock(mutex_);
    auto it = documents_.find(uri);
    if (it == documents_.end()) return std::nullopt;
    return it->second;
}

std::vector<Document> Store::all() const {
    std::lock_guard lock(mutex_);
    std::vector<Document> out;
    for (const auto& [_, d] : documents_) out.push_back(d);
    return out;
}

namespace {

bool flag(const Json& s, std::string_view key, bool fallback) {
    auto it = s.find(key);
    return it != s.end() && it->is_boolean() ? it->get<bool>() : fallback;
}

std::string word(const Json& s, std::string_view key, std::string_view fallback) {
    auto it = s.find(key);
    if (it == s.end() || !it->is_string()) return std::string(fallback);
    const auto v = it->get<std::string>();
    return v == "error" || v == "warning" || v == "info" || v == "off" ? v : std::string(fallback);
}

Json position(int line, int character) { return Json{{"line", line}, {"character", character}}; }
Json range(int line, int start, int end) { return Json{{"start", position(line, start)}, {"end", position(line, end)}}; }

int severity_number(std::string_view s) { return s == "error" ? 1 : s == "warning" ? 2 : 3; }

Json markdown(std::string value) { return Json{{"kind", "markdown"}, {"value", std::move(value)}}; }

// LSP CompletionItemKind.
constexpr int kFolder = 19, kSnippet = 15, kValue = 12, kModule = 9, kFunction = 3;

}  // namespace

Options options_of(const Json& s, bool trusted, bool mcpplsServes, std::string_view locale) {
    Options o;
    // mcpp-vscode 0.9's `mcpp.tomlCompletion` is the same switch under its earlier name.
    o.tomlCompletion = flag(s, "mcpp.toml.completion", flag(s, "mcpp.tomlCompletion", true));
    o.tomlHover = flag(s, "mcpp.toml.hover", true);
    o.tomlNavigation = flag(s, "mcpp.toml.navigation", true);
    o.tomlDiagnostics = flag(s, "mcpp.toml.diagnostics.enabled", true);
    o.tomlSeverity.syntax = word(s, "mcpp.toml.diagnostics.syntax", "error");
    o.tomlSeverity.unknownSection = word(s, "mcpp.toml.diagnostics.unknownSection", "warning");
    o.tomlSeverity.unknownKey = word(s, "mcpp.toml.diagnostics.unknownKey", "warning");
    o.tomlSeverity.planeSeparation = word(s, "mcpp.toml.diagnostics.planeSeparation", "warning");
    o.tomlSeverity.legacyKeys = word(s, "mcpp.toml.diagnostics.legacyKeys", "info");
    o.indexCompletion = flag(s, "mcpp.toml.indexCompletion", false);
    o.bsIntelligence = flag(s, "mcpp.buildScript.intelligence", true);
    o.bsDiagnostics = o.bsIntelligence && flag(s, "mcpp.buildScript.diagnostics", true);
    o.bsSnippets = o.bsIntelligence && flag(s, "mcpp.buildScript.snippets", true);
    o.bsKnownModules = o.bsIntelligence && flag(s, "mcpp.buildScript.imports.knownModules", true);
    o.bsSeverity = word(s, "mcpp.buildScript.diagnostics.severity", "warning");
    if (o.bsSeverity == "error") o.bsSeverity = "warning";
    o.mcpplsServes = mcpplsServes;
    o.trusted = trusted;
    o.locale = std::string(locale);
    return o;
}

Json server_capabilities() {
    return Json{
        {"textDocumentSync", Json{{"openClose", true}, {"change", 1}}},
        // `[` starts a header, `"` a version, `:` the `mcpp::` scope, `.` a module name.
        {"completionProvider", Json{{"triggerCharacters", Json::array({"[", "\"", ":", "."})}, {"resolveProvider", false}}},
        {"hoverProvider", true},
        {"definitionProvider", true},
        {"documentSymbolProvider", true},
    };
}

Json diagnostics(const Document& d, const Options& o) {
    Json out = Json::array();
    if (d.kind == Kind::manifest && o.tomlDiagnostics) {
        for (const auto& x : manifest::analyse(d.lines, o.tomlSeverity)) {
            // The rules count from 1, as `line:column` prints; LSP from 0.
            Json j{{"range", range(x.line - 1, x.start - 1, x.end - 1)}, {"severity", severity_number(x.severity)},
                   {"code", x.code}, {"source", "mcpp"}, {"message", x.message}};
            if (x.relatedKey) j["data"] = Json{{"relatedKey", *x.relatedKey}};
            out.push_back(std::move(j));
        }
    }
    if (d.kind == Kind::buildscript && o.bsDiagnostics && o.bsSeverity != "off") {
        for (const auto& x : buildscript::analyse(d.lines, o.bsSeverity, !o.mcpplsServes))
            out.push_back(Json{{"range", range(x.line - 1, x.start - 1, x.end - 1)}, {"severity", severity_number(x.severity)},
                               {"code", x.code}, {"source", "mcpp"}, {"message", x.message}});
    }
    return out;
}

Json completion(const Document& d, int line, int character, const Options& o, const Json* catalogue, bool& needCatalogue) {
    needCatalogue = false;
    Json items = Json::array();
    bool incomplete = false;
    if (d.kind == Kind::manifest && o.tomlCompletion && line >= 0 && line < static_cast<int>(d.lines.size())) {
        // The lines up to the cursor's, as the editor asks.
        const std::vector<std::string> upTo(d.lines.begin(), d.lines.begin() + line + 1);
        auto suggestions = manifest::completions(upTo, line, character, o.locale);
        if (suggestions.empty() && o.indexCompletion && o.trusted)
            if (const auto at = manifest::dependency_version_context(upTo, line, character)) {
                if (catalogue) suggestions = manifest::version_suggestions(*at, manifest::catalogue_versions(*catalogue, at->name));
                else needCatalogue = incomplete = true;
            }
        for (const auto& s : suggestions) {
            Json item{{"label", s.label}, {"kind", s.kind == "section" ? kFolder : s.kind == "version" ? kValue : kSnippet},
                      {"detail", s.detail}, {"insertTextFormat", 2},
                      {"textEdit", Json{{"range", range(line, s.start, s.end)}, {"newText", s.insertSnippet.value_or(s.label)}}}};
            if (s.documentation) item["documentation"] = markdown(*s.documentation);
            // Versions keep their order, newest first.
            if (s.kind == "version") item["sortText"] = std::format("{:05}", items.size());
            items.push_back(std::move(item));
        }
    }
    if (d.kind == Kind::buildscript && o.bsIntelligence && line >= 0 && line < static_cast<int>(d.lines.size())) {
        buildscript::Switches sw{o.bsSnippets, o.bsKnownModules, o.mcpplsServes};
        if (const auto found = buildscript::completion(d.lines[line], character, sw))
            for (const auto& i : *found) {
                Json item{{"label", i.label}, {"kind", i.kind == "module" ? kModule : i.kind == "function" ? kFunction : kSnippet},
                          {"detail", i.detail}};
                if (i.documentation) item["documentation"] = markdown(*i.documentation);
                if (i.filterText) item["filterText"] = *i.filterText;
                if (i.insertText) {
                    item["insertText"] = *i.insertText;
                    item["insertTextFormat"] = 2;
                }
                items.push_back(std::move(item));
            }
    }
    return Json{{"isIncomplete", incomplete}, {"items", items}};
}

Json hover(const Document& d, int line, int character, const Options& o) {
    if (d.kind == Kind::manifest && o.tomlHover) {
        const auto info = manifest::hover_at(d.lines, line, character);
        if (!info) return nullptr;
        auto value = std::format("**{}**\n\n{}", info->title, info->body);
        if (info->documentation)
            value += std::format("\n\n[{}]({})", i18n::translate("Manifest reference", o.locale), *info->documentation);
        return Json{{"contents", markdown(std::move(value))}};
    }
    if (d.kind == Kind::buildscript && o.bsIntelligence) {
        const auto text = buildscript::hover(d.lines, line, character, o.mcpplsServes);
        if (!text) return nullptr;
        return Json{{"contents", markdown(*text)}};
    }
    return nullptr;
}

Json definition(const Document& d, int line, int character, const Options& o) {
    if (d.kind != Kind::manifest || !o.tomlNavigation) return nullptr;
    std::filesystem::path target;
    // The line of `[package]` in the manifest of the directory `relative` names.
    auto resolve = [&](std::string_view relative) -> std::optional<int> {
        const auto file = (d.path.parent_path() / std::filesystem::path(std::string(relative)) / "mcpp.toml").lexically_normal();
        std::ifstream in(file, std::ios::binary);
        if (!in) return std::nullopt;
        const auto lines = text::lines_of(std::string(std::istreambuf_iterator<char>(in), {}));
        for (std::size_t i = 0; i < lines.size(); ++i) {
            const auto t = text::trim(lines[i]);
            if (t.size() >= 2 && t.front() == '[' && t.back() == ']' && text::trim(t.substr(1, t.size() - 2)) == "package") {
                target = file;
                return static_cast<int>(i);
            }
        }
        return std::nullopt;
    };
    const auto at = manifest::definition_at(d.lines, line, character, resolve);
    if (!at) return nullptr;
    return Json{{"uri", target.empty() ? d.uri : uri_of(target)}, {"range", range(at->line, at->start, at->end)}};
}

Json symbols(const Document& d) {
    if (d.kind != Kind::manifest) return Json::array();
    return manifest::document_symbols(d.lines);
}

}  // namespace mcppide::documents
