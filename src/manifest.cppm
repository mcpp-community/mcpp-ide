// Editing help for mcpp.toml and build.mcpp: completion, hover and an outline,
// from mcpp's own key registry and build-program directives (the snapshots in
// spec/, regenerated from an mcpp checkout by tools/generate-schemas.py).
//
// THIS IS TEXT, NOT MEANING. The analysis below finds where the cursor is --
// which `[table]` it is under, whether it is on a key or after `=` -- in the
// text the editor sends; it resolves nothing. Whether the manifest is valid,
// and where it is wrong, is mcpp's to say (manifest/check, located
// diagnostics). Both editors call these methods, so CLion completes what VS
// Code completes.
export module mcppide.manifest;

import std;
import nlohmann.json;
import mcppide.generated.spec;

export namespace mcppide::manifest {

using Json = nlohmann::json;

// Where the cursor is in a mcpp.toml.
struct Context {
    enum class Where { header, key, value, comment, other } where = Where::other;
    std::string table;                     // "build", "target.'cfg(os = \"linux\")'.build", ...
    std::string section;                   // the schema section the table reads as: "build"
    std::string key;                       // in a value: the key before `=`
    std::set<std::string> presentKeys;     // keys already written in this table
    int replaceStart = 0, replaceEnd = 0;  // the word the suggestion replaces, on the cursor's line
};

Context context_at(std::string_view text, int line, int character);
// The schema section a table header reads as: `target.<sel>.build` is `build`,
// `dependencies.<ns>` is `dependencies`, `targets.<name>` is `targets`.
std::string section_of(std::string_view table);

// {text, line, character} -> {items: [{label, kind, detail, documentation, insertText, range}]}
Json complete(const Json& params);
// {text, line, character} -> {contents (markdown), range} or null
Json hover(const Json& params);
// {text} -> [{name, kind (table|key), line, children}]
Json symbols(const Json& params);
// {text, line, character} -> completion of `mcpp::<directive>` in build.mcpp
Json buildscript_complete(const Json& params);

}  // namespace mcppide::manifest

namespace mcppide::manifest {

namespace {

const Json& schema() {
    static const Json s = Json::parse(generated::kTomlSchemaJson);
    return s;
}
const Json& api() {
    static const Json a = Json::parse(generated::kBuildScriptApiJson);
    return a;
}

std::vector<std::string_view> split_lines(std::string_view text) {
    std::vector<std::string_view> out;
    std::size_t pos = 0;
    while (pos <= text.size()) {
        auto end = text.find('\n', pos);
        if (end == std::string_view::npos) end = text.size();
        auto line = text.substr(pos, end - pos);
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        out.push_back(line);
        if (end == text.size()) break;
        pos = end + 1;
    }
    return out;
}

std::string_view trim(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) s.remove_suffix(1);
    return s;
}

// The position of a `#` that starts a comment (outside a string), or npos.
std::size_t comment_at(std::string_view line) {
    char quote = 0;
    for (std::size_t i = 0; i < line.size(); ++i) {
        const char c = line[i];
        if (quote) { if (c == '\\' && quote == '"') { ++i; continue; } if (c == quote) quote = 0; continue; }
        if (c == '"' || c == '\'') quote = c;
        else if (c == '#') return i;
    }
    return std::string_view::npos;
}

// `[a.b]` / `[[a.b]]` -> "a.b"; nullopt for a line that is not a header.
std::optional<std::string> header_of(std::string_view line) {
    auto t = trim(line.substr(0, std::min(line.size(), comment_at(line))));
    if (t.size() < 2 || t.front() != '[') return std::nullopt;
    const bool array = t.starts_with("[[");
    t.remove_prefix(array ? 2 : 1);
    const auto close = t.rfind(array ? "]]" : "]");
    if (close == std::string_view::npos) return std::nullopt;
    return std::string(trim(t.substr(0, close)));
}

// The dotted segments of a table name, quotes respected.
std::vector<std::string> segments(std::string_view table) {
    std::vector<std::string> out;
    std::string cur;
    char quote = 0;
    for (char c : table) {
        if (quote) { cur.push_back(c); if (c == quote) quote = 0; continue; }
        if (c == '"' || c == '\'') { quote = c; cur.push_back(c); continue; }
        if (c == '.') { out.push_back(std::string(trim(cur))); cur.clear(); continue; }
        cur.push_back(c);
    }
    if (!trim(cur).empty() || !out.empty()) out.push_back(std::string(trim(cur)));
    return out;
}

const Json* section_named(std::string_view name) {
    for (const auto& s : schema()["sections"]) if (s.value("name", "") == name) return &s;
    return nullptr;
}

const Json* key_in(const Json& section, std::string_view key) {
    if (!section.contains("keys")) return nullptr;
    for (const auto& k : section["keys"]) if (k.value("key", "") == key) return &k;
    return nullptr;
}

bool is_word(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-' || c == '.'; }

Json range_json(int line, int start, int end) {
    return Json{{"start", {{"line", line}, {"character", start}}}, {"end", {{"line", line}, {"character", end}}}};
}

std::string key_doc(const Json& section, const Json& key) {
    std::string doc = std::format("**{}** `{}`", key.value("key", ""), key.value("type", ""));
    if (key.contains("values") && key["values"].is_array()) {
        std::string vals;
        for (const auto& v : key["values"]) { if (!vals.empty()) vals += " | "; vals += "`" + v.get<std::string>() + "`"; }
        doc += "\n\n" + vals;
    }
    if (key.contains("default") && !key["default"].is_null())
        doc += std::format("\n\ndefault: `{}`", key["default"].is_string() ? key["default"].get<std::string>() : key["default"].dump());
    if (key.contains("note") && key["note"].is_string()) doc += "\n\n" + key["note"].get<std::string>();
    if (section.contains("doc") && section["doc"].is_string()) doc += std::format("\n\n[{}]({})", section.value("header", ""), section["doc"].get<std::string>());
    return doc;
}

}  // namespace

std::string section_of(std::string_view table) {
    auto seg = segments(table);
    if (seg.empty()) return {};
    // A `target.<selector>` table reads as the table after the selector.
    if (seg[0] == "target" && seg.size() >= 3) return seg[2];
    if (seg[0] == "target") return "target";
    if (seg[0] == "workspace" && seg.size() >= 2) return seg[1] == "package" ? "package" : seg[1];
    return seg[0];
}

Context context_at(std::string_view text, int line, int character) {
    Context c;
    const auto lines = split_lines(text);
    if (line < 0 || line >= static_cast<int>(lines.size())) return c;
    // The table the cursor is under, and the keys it already holds.
    int headerLine = -1;
    for (int i = line; i >= 0; --i)
        if (auto h = header_of(lines[i])) { c.table = *h; headerLine = i; break; }
    if (headerLine >= 0 && headerLine == line) {
        // On the header line itself: completing the header.
        c.where = Context::Where::header;
    }
    for (int i = headerLine + 1; i < static_cast<int>(lines.size()); ++i) {
        if (i != headerLine && header_of(lines[i])) break;
        auto l = lines[i];
        l = l.substr(0, std::min(l.size(), comment_at(l)));
        if (auto eq = l.find('='); eq != std::string_view::npos) {
            auto k = std::string(trim(l.substr(0, eq)));
            if (!k.empty() && i != line) c.presentKeys.insert(k);
        }
    }
    c.section = section_of(c.table);
    const auto cur = lines[line];
    const int col = std::clamp(character, 0, static_cast<int>(cur.size()));
    if (comment_at(cur) <= static_cast<std::size_t>(col) && comment_at(cur) != std::string_view::npos) {
        c.where = Context::Where::comment;
        return c;
    }
    // The word the cursor is in.
    int s = col, e = col;
    while (s > 0 && is_word(cur[s - 1])) --s;
    while (e < static_cast<int>(cur.size()) && is_word(cur[e])) ++e;
    c.replaceStart = s;
    c.replaceEnd = e;
    const auto before = trim(cur.substr(0, col));
    if (before.starts_with("[")) {
        c.where = Context::Where::header;
        c.replaceStart = static_cast<int>(cur.find('['));
        c.replaceEnd = static_cast<int>(cur.size());
        return c;
    }
    if (c.where == Context::Where::header) return c;
    if (auto eq = cur.substr(0, col).find('='); eq != std::string_view::npos) {
        c.where = Context::Where::value;
        c.key = std::string(trim(cur.substr(0, eq)));
        // A quoted value is replaced inside its quotes.
        auto open = cur.find_first_of("\"'", eq);
        if (open != std::string_view::npos && static_cast<int>(open) < col) {
            auto close = cur.find(cur[open], open + 1);
            c.replaceStart = static_cast<int>(open + 1);
            c.replaceEnd = close == std::string_view::npos ? static_cast<int>(cur.size()) : static_cast<int>(close);
        }
        return c;
    }
    c.where = Context::Where::key;
    return c;
}

Json complete(const Json& p) {
    const auto text = p.value("text", "");
    const int line = p.value("line", 0), character = p.value("character", 0);
    auto c = context_at(text, line, character);
    Json items = Json::array();
    const auto range = range_json(line, c.replaceStart, c.replaceEnd);
    switch (c.where) {
        case Context::Where::header:
            for (const auto& s : schema()["sections"]) {
                if (!s.value("deprecatedBy", Json(nullptr)).is_null()) continue;
                const auto header = s.value("header", "");
                items.push_back(Json{{"label", header}, {"kind", "section"}, {"detail", s.value("plane", "")},
                                     {"insertText", header}, {"range", range},
                                     {"documentation", s.contains("doc") && s["doc"].is_string() ? s["doc"] : Json("")}});
            }
            // The forms that name something: a dependency namespace, a target, a selector.
            for (auto [label, snippet, detail] : std::initializer_list<std::tuple<const char*, const char*, const char*>>{
                     {"[dependencies.<namespace>]", "[dependencies.${1:mcpplibs}]", "dependencies of one index namespace"},
                     {"[targets.<name>]", "[targets.${1:name}]", "an artifact"},
                     {"[target.'cfg(...)'.build]", "[target.'cfg(${1:os = \"linux\"})'.build]", "build settings for some targets"},
                     {"[target.'cfg(...)'.dependencies]", "[target.'cfg(${1:os = \"linux\"})'.dependencies]", "dependencies for some targets"},
                     {"[features.<name>]", "[features.${1:name}]", "a feature"},
                     {"[profile.<name>]", "[profile.${1:release}]", "a build profile"},
                 })
                items.push_back(Json{{"label", label}, {"kind", "template"}, {"detail", detail},
                                     {"insertText", snippet}, {"snippet", true}, {"range", range}});
            break;
        case Context::Where::key: {
            const auto* section = section_named(c.section);
            if (!section || section->value("openKeys", false)) break;
            if (section->contains("keys"))
                for (const auto& k : (*section)["keys"]) {
                    const auto key = k.value("key", "");
                    if (c.presentKeys.contains(key)) continue;
                    std::string insert = key + " = ";
                    const auto type = k.value("type", "");
                    if (type == "string" || type == "enum") insert += "\"$1\"";
                    else if (type == "array") insert += "[$1]";
                    else if (type == "boolean") insert += "${1|true,false|}";
                    else if (type == "table") insert += "{ $1 }";
                    else insert += "$1";
                    items.push_back(Json{{"label", key}, {"kind", "key"}, {"detail", type},
                                         {"documentation", key_doc(*section, k)}, {"insertText", insert},
                                         {"snippet", true}, {"range", range},
                                         {"deprecated", k.contains("deprecatedBy") && !k["deprecatedBy"].is_null()}});
                }
            break;
        }
        case Context::Where::value: {
            const auto* section = section_named(c.section);
            const Json* key = section ? key_in(*section, c.key) : nullptr;
            if (!key) break;
            if (key->contains("values") && (*key)["values"].is_array())
                for (const auto& v : (*key)["values"])
                    items.push_back(Json{{"label", v}, {"kind", "value"}, {"detail", c.key}, {"insertText", v}, {"range", range}});
            else if (key->value("type", "") == "boolean")
                for (const char* v : {"true", "false"})
                    items.push_back(Json{{"label", v}, {"kind", "value"}, {"detail", c.key}, {"insertText", v}, {"range", range}});
            break;
        }
        default:
            break;
    }
    return Json{{"items", items}, {"table", c.table}, {"section", c.section}};
}

Json hover(const Json& p) {
    const auto text = p.value("text", "");
    const int line = p.value("line", 0), character = p.value("character", 0);
    const auto lines = split_lines(text);
    if (line < 0 || line >= static_cast<int>(lines.size())) return nullptr;
    const auto cur = lines[line];
    if (auto h = header_of(cur)) {
        const auto* section = section_named(section_of(*h));
        if (!section) return nullptr;
        std::string doc = std::format("**{}** · {}", section->value("header", ""), section->value("plane", ""));
        if (section->contains("doc") && (*section)["doc"].is_string())
            doc += std::format("\n\n[{}]({})", section->value("header", ""), (*section)["doc"].get<std::string>());
        if (!section->value("deprecatedBy", Json(nullptr)).is_null())
            doc += std::format("\n\nReplaced by `{}`.", (*section)["deprecatedBy"].get<std::string>());
        return Json{{"contents", doc}, {"range", range_json(line, 0, static_cast<int>(cur.size()))}};
    }
    auto c = context_at(text, line, character);
    auto eq = cur.find('=');
    if (eq == std::string_view::npos || character > static_cast<int>(eq)) return nullptr;
    const auto key = std::string(trim(cur.substr(0, eq)));
    const auto* section = section_named(c.section);
    const Json* k = section ? key_in(*section, key) : nullptr;
    if (!k) return nullptr;
    const auto start = static_cast<int>(cur.find(key));
    return Json{{"contents", key_doc(*section, *k)}, {"range", range_json(line, start, start + static_cast<int>(key.size()))}};
}

Json symbols(const Json& p) {
    const auto lines = split_lines(p.value("text", ""));
    Json out = Json::array();
    Json* current = nullptr;
    for (int i = 0; i < static_cast<int>(lines.size()); ++i) {
        if (auto h = header_of(lines[i])) {
            out.push_back(Json{{"name", *h}, {"kind", "table"}, {"line", i}, {"children", Json::array()}});
            current = &out.back();
            continue;
        }
        auto l = lines[i].substr(0, std::min(lines[i].size(), comment_at(lines[i])));
        auto eq = l.find('=');
        if (eq == std::string_view::npos) continue;
        auto key = std::string(trim(l.substr(0, eq)));
        if (key.empty() || key.find(' ') != std::string::npos) continue;
        Json sym{{"name", key}, {"kind", "key"}, {"line", i}};
        if (current) (*current)["children"].push_back(sym);
        else out.push_back(sym);
    }
    return out;
}

Json buildscript_complete(const Json& p) {
    const auto lines = split_lines(p.value("text", ""));
    const int line = p.value("line", 0), character = p.value("character", 0);
    if (line < 0 || line >= static_cast<int>(lines.size())) return Json{{"items", Json::array()}};
    const auto cur = lines[line];
    const int col = std::clamp(character, 0, static_cast<int>(cur.size()));
    const auto before = cur.substr(0, col);
    const auto scope = before.rfind("mcpp::");
    if (scope == std::string_view::npos) return Json{{"items", Json::array()}};
    const auto typed = before.substr(scope + 6);
    if (std::ranges::any_of(typed, [](char ch) { return !(std::isalnum(static_cast<unsigned char>(ch)) || ch == '_'); }))
        return Json{{"items", Json::array()}};
    Json items = Json::array();
    const auto range = range_json(line, static_cast<int>(scope + 6), col);
    for (const auto& d : api()["directives"]) {
        const auto tag = d.value("tag", d.value("wire", ""));
        if (!tag.starts_with(typed)) continue;
        std::string doc = std::format("`mcpp:{}=` · slot `{}` · scope `{}` · since protocol {}", d.value("wire", ""),
                                      d.value("slot", ""), d.value("scope", ""), d.value("since", 0));
        if (d.contains("docsUrl") && d["docsUrl"].is_string()) doc += std::format("\n\n[docs]({})", d["docsUrl"].get<std::string>());
        items.push_back(Json{{"label", tag}, {"kind", "function"}, {"detail", std::format("mcpp:{}=", d.value("wire", ""))},
                             {"documentation", doc}, {"insertText", tag + "(\"$1\")"}, {"snippet", true}, {"range", range}});
    }
    if (std::string_view("action").starts_with(typed))
        items.push_back(Json{{"label", "action"}, {"kind", "snippet"}, {"detail", "mcpp::action with id, role, inputs, outputs"},
                             {"insertText", "action ${1:handle};\n${1:handle}.id = \"${2:id}\";\n${1:handle}.role = mcpp::roles::${3|source,check,prepare,object,artifact|};\n${1:handle}.submit();"},
                             {"snippet", true}, {"range", range}});
    return Json{{"items", items}};
}

}  // namespace mcppide::manifest
