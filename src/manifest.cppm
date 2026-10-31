// The rules layer for mcpp.toml: completion, hover, the three in-file jumps and the seven
// diagnostic rules, from the snapshot of mcpp's tables and keys (spec/toml-schema.json, naming the
// mcpp release and commit it was read from).
//
// THIS IS TEXT, NOT MEANING. It reads what an edit in progress already says and answers from the
// snapshot; it runs nothing, so it serves an untrusted workspace too. Whether the manifest is valid
// is mcpp's to say (manifest/check). Every rule errs towards silence: a false positive costs more
// trust than a missed hint.
//
// Positions in: a 0-based line and a UTF-16 column, as the editors count. Positions out: the same,
// except a diagnostic's, which is 1-based (`line:column`, the form a terminal prints). A port of
// mcpp-vscode 0.9's src/toml/{completion,hover,navigation,diagnostics}.ts (design 2026-10-30 part 2
// §8); tests/rules holds that implementation's answers over a corpus, and this one gives them.
export module mcppide.manifest;

import std;
import nlohmann.json;
import mcppide.i18n;
import mcppide.spec;
import mcppide.text;
import mcppide.toml;

export namespace mcppide::manifest {

using Json = nlohmann::json;

// ── completion ─────────────────────────────────────────────────────────────

struct Suggestion {
    std::string label;
    std::string kind;                         // section | template | version
    std::string detail;
    std::optional<std::string> documentation;
    std::optional<std::string> insertSnippet; // a snippet (`${1:name}`); the label when none
    int start = 0, end = 0;                   // the replaced columns on the cursor's line
};

// The suggestions at a cursor: table headers, a table's keys, an enum key's values, and the forms of
// a table whose keys the user names. `lines` are the document's up to the cursor's line, as an
// editor asks. `locale` picks the language of the details.
std::vector<Suggestion> completions(const std::vector<std::string>& lines, int line, int character,
                                    std::string_view locale = "en");

// A dependency's version under the cursor (`zlib = "1.2|"`, `zlib = { version = "1.2|" }`).
struct VersionContext {
    std::string name;
    bool insideString = false;
    int start = 0, end = 0;
};
std::optional<VersionContext> dependency_version_context(const std::vector<std::string>& lines, int line, int character);

// A version an index offers, newest first.
struct PackageVersion {
    std::string version;
    std::optional<std::string> summary;
};
// The versions of the packages named `name` in mcpp's catalogue (`mcpp search --format json`).
std::vector<PackageVersion> catalogue_versions(const Json& searchEnvelope, std::string_view name);
std::vector<Suggestion> version_suggestions(const VersionContext& at, const std::vector<PackageVersion>& versions);

// ── hover and the jumps ────────────────────────────────────────────────────

struct HoverInfo {
    std::string title, body;
    std::optional<std::string> documentation;   // the section's reference
};
std::optional<HoverInfo> hover_at(const std::vector<std::string>& lines, int line, int character);

struct Location {
    int line = 0, start = 0, end = 0;
};
// `workspace = true` to `[workspace.dependencies]`'s entry; `features = ["a"]` to `[features.a]`;
// `path = "../x"` to that manifest's `[package]`, whose line `resolve_manifest` answers (the
// file system is the caller's).
std::optional<Location> definition_at(const std::vector<std::string>& lines, int line, int character,
                                      const std::function<std::optional<int>(std::string_view)>& resolve_manifest);

// ── diagnostics ────────────────────────────────────────────────────────────

struct Diagnostic {
    std::string code, message, severity;   // severity: error | warning | info
    int line = 0, start = 0, end = 0;      // 1-based
    std::optional<std::string> relatedKey;
};
// Each rule's severity: error | warning | info | off.
struct DiagnosticSettings {
    std::string syntax = "error", unknownSection = "warning", unknownKey = "warning",
                planeSeparation = "warning", legacyKeys = "info", mcppFloor = "error", arrayTable = "error";
};
std::vector<Diagnostic> analyse(const std::vector<std::string>& lines, const DiagnosticSettings& settings);

// The outline: each table, and the keys under it (LSP DocumentSymbol).
Json document_symbols(const std::vector<std::string>& lines);

// The snapshot's provenance.
Json schema_source();

}  // namespace mcppide::manifest

namespace mcppide::manifest {

namespace {

using toml::Context;
using toml::Resolution;

const Json& schema() { return spec::toml_schema(); }

std::string str(const Json& j, std::string_view key) {
    auto it = j.find(key);
    return it != j.end() && it->is_string() ? it->get<std::string>() : std::string();
}
bool has_string(const Json& j, std::string_view key) {
    auto it = j.find(key);
    return it != j.end() && it->is_string() && !it->get_ref<const std::string&>().empty();
}

// `[package]` / `package` is `package`; an array of tables is no section.
std::string normalise_section(std::string_view headerOrName) {
    auto t = text::trim(headerOrName);
    if (t.starts_with("[[")) return {};
    if (t.starts_with("[") && t.ends_with("]")) return std::string(text::trim(t.substr(1, t.size() - 2)));
    return std::string(t);
}

const Json* section_by_name(std::string_view name) {
    const auto wanted = normalise_section(name);
    if (wanted.empty()) return nullptr;
    for (const auto& s : schema()["sections"])
        if (str(s, "name") == wanted) return &s;
    return nullptr;
}

const Json* keys_of(const Json* section) {
    if (!section) return nullptr;
    auto it = section->find("keys");
    return it != section->end() && it->is_array() ? &*it : nullptr;
}

const Json* find_key(const Json* section, std::string_view key) {
    if (const auto* keys = keys_of(section))
        for (const auto& k : *keys)
            if (str(k, "key") == key) return &k;
    return nullptr;
}

std::string tr(std::string_view key, std::string_view locale) { return i18n::translate(key, locale); }

int u16(const std::vector<std::string>& lines, int line, int byte) {
    if (line < 0 || line >= static_cast<int>(lines.size())) return byte;
    return text::utf16_column(lines[line], static_cast<std::size_t>(std::max(0, byte)));
}

// A cursor's column in bytes; past the line's end it stays past it.
int cursor_byte(const std::vector<std::string>& lines, int line, int character) {
    if (line < 0 || line >= static_cast<int>(lines.size())) return character;
    const auto& l = lines[line];
    const int length = text::utf16_length(l);
    if (character > length) return static_cast<int>(l.size()) + (character - length);
    return static_cast<int>(text::byte_offset(l, character));
}

const std::set<std::string, std::less<>> kDependencyGroups = {
    "dependencies", "dev-dependencies", "build-dependencies", "workspace.dependencies", "feature-deps",
};

// ── completion tables ──

struct HeaderSpec {
    std::string_view group, label, header, detail;
};

// The wording of the tables' headers; the list of tables is the snapshot's.
constexpr HeaderSpec kHeaderSpecs[] = {
    {"package", "[package]", "[package]", "Package metadata"},
    {"lib", "[lib]", "[lib]", "Library root-module convention"},
    {"build", "[build]", "[build]", "Build configuration"},
    {"generated_files", "[generated_files]", "[generated_files]", "Generated files (path → content)"},
    {"dependencies", "[dependencies]", "[dependencies]", "Runtime dependencies"},
    {"dev-dependencies", "[dev-dependencies]", "[dev-dependencies]", "Development/test dependencies"},
    {"build-dependencies", "[build-dependencies]", "[build-dependencies]", "Build-time dependencies (pulled at build time only, invisible at run time)"},
    {"workspace", "[workspace]", "[workspace]", "Workspace member declarations"},
    {"workspace.dependencies", "[workspace.dependencies]", "[workspace.dependencies]", "Declare dependency versions centrally; members inherit them with workspace = true"},
    {"features", "[features]", "[features]", "Feature definitions"},
    {"feature-deps", "[feature-deps.<name>]", "[feature-deps.${1:name}]", "Optional dependencies pulled in by a feature"},
    {"capabilities", "[capabilities]", "[capabilities]", "Capability bindings (provider selection)"},
    {"targets", "[targets.<name>]", "[targets.${1:name}]", "Build targets"},
    {"profile", "[profile.<name>]", "[profile.${1:name}]", "Build profiles"},
    {"runtime", "[runtime]", "[runtime]", "Host runtime capabilities"},
    {"resources", "[resources]", "[resources]", "Metadata and assets compiled into the product (PE targets only)"},
    {"toolchain", "[toolchain]", "[toolchain]", "Compiler toolchain shorthand"},
    {"xlings", "[xlings]", "[xlings]", "Build environment (supplied by xlings)"},
    {"xlings.workspace", "[xlings.workspace]", "[xlings.workspace]", "Pin tool versions"},
    {"target", "[target.<triple>]", "[target.${1:x86_64-linux-gnu}]", "Configuration per target triple"},
    {"dependencies.<namespace>", "[dependencies.<namespace>]", "[dependencies.${1:mcpplibs}]", "Dependencies of one index namespace"},
    {"features.<name>", "[features.<name>]", "[features.${1:name}]", "A feature as a table"},
    {"target.cfg.build", "[target.'cfg(...)'.build]", "[target.'cfg(${1:os = \"linux\"})'.build]", "Build settings for the targets a selector matches"},
    {"target.cfg.dependencies", "[target.'cfg(...)'.dependencies]", "[target.'cfg(${1:os = \"linux\"})'.dependencies]", "Dependencies for the targets a selector matches"},
    {"pack", "[pack]", "[pack]", "mcpp pack packaging configuration"},
    {"pack.bundle-project", "[pack.bundle-project]", "[pack.bundle-project]", "Fine-tuning of the vendored filtering policy"},
    {"indices", "[indices]", "[indices]", "Project-level index redirection"},
    {"tools.overrides", "[tools.overrides]", "[tools.overrides]", "Host tool binary overrides"},
    {"language", "[language]", "[language]", "Legacy compatibility field; new projects should use [package].standard"},
};

struct TemplateSpec {
    std::string_view label, detail, documentation, insertSnippet;
};

constexpr TemplateSpec kDependencyTemplates[] = {
    {"name = \"version\"", "SemVer version dependency",
     "Caret constraint (^) by default; ~, = and range combinations such as \">=1.0, <2.0\" are also supported.",
     "${1:name} = \"${2:1.0.0}\""},
    {"name = { path = ... }", "Path dependency (local development)", "", "${1:name} = { path = \"${2:../mylib}\" }"},
    {"name = { git = ..., tag = ... }", "Git dependency (one of tag / branch / rev)", "",
     "${1:name} = { git = \"${2:https://github.com/user/repo.git}\", tag = \"${3:v1.0.0}\" }"},
    {"name = { version = ..., features = [...] }", "Long dep spec: request a feature of that dependency", "",
     "${1:name} = { version = \"${2:1.0}\", features = [\"${3:feature}\"] }"},
    {"name = { version = ..., tools = [...] }", "Host tools produced by the dependency (must be a bin target of that package)", "",
     "${1:name} = { version = \"${2:1.0}\", tools = [\"${3:protoc}\"] }"},
};
constexpr TemplateSpec kFeatureTemplates[] = {
    {"name = [...]", "Array shorthand: implies the feature only", "", "${1:name} = [${2}]"},
    {"name = { defines = [...] }", "Table form: contributes the package's own macros when activated", "", "${1:name} = { defines = [\"${2:MACRO}\"] }"},
    {"name = { requires = [...] }", "Table form: requires a capability", "", "${1:name} = { requires = [\"${2:blas}\"] }"},
    {"name = { sources = [...] }", "Table form: feature-gated source globs", "", "${1:name} = { sources = [\"${2:src/simd/**}\"] }"},
};
constexpr TemplateSpec kGeneratedFileTemplates[] = {
    {"\"path\" = \"content\"", "Generated file (relative path → content, part of the fingerprint)", "",
     "\"${1:src/gen/wrap.cppm}\" = \"\"\"\n${2:}\n\"\"\""},
};
constexpr TemplateSpec kCapabilityTemplates[] = {
    {"capability = \"provider\"", "Capability binding (equivalent to --cap)", "", "${1:blas} = \"${2:compat.openblas}\""},
};
constexpr TemplateSpec kXlingsWorkspaceTemplates[] = {
    {"tool = \"version\"", "Pin the xlings tool version", "", "${1:node} = \"${2:24.19.0}\""},
};
constexpr TemplateSpec kToolsOverridesTemplates[] = {
    {"\"pkg:tool\" = \"path\"", "Override a host tool with an existing binary (skips the build)", "",
     "\"${1:compat.protobuf:protoc}\" = \"${2:/usr/bin/protoc}\""},
};

std::span<const TemplateSpec> templates_of(std::string_view group) {
    if (group == "features") return kFeatureTemplates;
    if (group == "generated_files") return kGeneratedFileTemplates;
    if (group == "capabilities") return kCapabilityTemplates;
    if (group == "xlings.workspace") return kXlingsWorkspaceTemplates;
    if (group == "tools.overrides") return kToolsOverridesTemplates;
    return {};
}

struct HeaderEntry {
    std::string label, header, detail;
    std::optional<std::string> documentation;
};

// The tables to suggest: the snapshot's, worded by the table above where it has them, then the
// table's forms the snapshot does not list.
std::vector<HeaderEntry> header_entries(std::string_view locale) {
    std::vector<HeaderEntry> entries;
    const auto& sections = schema()["sections"];
    if (sections.empty()) {
        for (const auto& s : kHeaderSpecs) entries.push_back({std::string(s.label), std::string(s.header), tr(s.detail, locale), {}});
        return entries;
    }
    for (const auto& section : sections) {
        const auto header = str(section, "header");
        if (section.value("selector", false)) {
            // Written only under a selector (`[target.<selector>.abi]`): the selector to fill in.
            const auto tail = header.substr(header.find("<selector>") + std::string_view("<selector>").size());
            entries.push_back({"[target.'cfg(...)'" + tail, "[target.'cfg(${1:os = \"linux\"})'" + tail,
                               "plane: " + str(section, "plane"), {}});
            continue;
        }
        const auto name = str(section, "name");
        const auto spec = std::ranges::find(kHeaderSpecs, name, &HeaderSpec::group);
        HeaderEntry e;
        if (spec != std::end(kHeaderSpecs)) {
            e = {std::string(spec->label), std::string(spec->header), tr(spec->detail, locale), {}};
        } else {
            e = {header, header, "plane: " + str(section, "plane"), {}};
        }
        if (has_string(section, "deprecatedBy")) e.documentation = std::format("Deprecated; use `{}`.", str(section, "deprecatedBy"));
        entries.push_back(std::move(e));
    }
    std::set<std::string> suggested;
    for (const auto& e : entries) suggested.insert(e.header);
    for (const auto& s : kHeaderSpecs)
        if (!suggested.contains(std::string(s.header)))
            entries.push_back({std::string(s.label), std::string(s.header), tr(s.detail, locale), {}});
    return entries;
}

std::vector<Suggestion> header_suggestions(int start, int end, std::string_view locale) {
    std::vector<Suggestion> out;
    for (auto& e : header_entries(locale))
        out.push_back({e.label, "section", e.detail, e.documentation, e.header, start, end});
    return out;
}

std::vector<Suggestion> template_suggestions(std::span<const TemplateSpec> templates, int start, int end, std::string_view locale) {
    std::vector<Suggestion> out;
    for (const auto& t : templates) {
        Suggestion s{std::string(t.label), "template", tr(t.detail, locale), std::nullopt, std::string(t.insertSnippet), start, end};
        if (!t.documentation.empty()) s.documentation = tr(t.documentation, locale);
        out.push_back(std::move(s));
    }
    return out;
}

std::string key_placeholder(const Json& key) {
    const auto type = str(key, "type");
    if (type == "boolean") return "true";
    if (type == "number") return "0";
    if (type == "array") return "[]";
    if (type == "table") return "{ }";
    if (type == "enum") {
        const auto it = key.find("values");
        return text::stringify(it != key.end() && it->is_array() && !it->empty() ? (*it)[0] : Json(""));
    }
    return "\"\"";
}

bool has_values(const Json& key) {
    auto it = key.find("values");
    return it != key.end() && it->is_array() && !it->empty();
}

std::string key_detail(const Json& key) {
    auto detail = str(key, "type");
    if (has_values(key)) {
        std::string values;
        for (const auto& v : key["values"]) values += (values.empty() ? "" : " | ") + v.get<std::string>();
        detail += ": " + values;
    }
    if (key.contains("default")) detail += std::format(" (default: {})", text::stringify(key["default"]));
    return detail;
}

std::optional<std::string> key_documentation(const Json& key) {
    std::vector<std::string> lines;
    if (has_string(key, "note")) lines.push_back(str(key, "note"));
    if (key.contains("since") && key["since"].is_string()) lines.push_back(std::format("Since {}.", key["since"].get<std::string>()));
    if (key.value("legacy", false) == true) lines.push_back("Legacy key.");
    if (lines.empty()) return std::nullopt;
    std::string out;
    for (const auto& l : lines) out += (out.empty() ? "" : " ") + l;
    return out;
}

std::vector<std::string> segment_names(const std::vector<toml::KeySegment>& segments) {
    std::vector<std::string> out;
    for (const auto& s : segments) out.push_back(s.name);
    return out;
}

// The top-level keys of `group` written before `line`, whichever of the group's tables holds them.
std::set<std::string> keys_used_before(const std::vector<std::string>& lines, int line, std::string_view group) {
    std::set<std::string> used;
    std::optional<std::string> current;
    for (const auto& node : toml::parse(lines).nodes) {
        if (const auto* s = std::get_if<toml::Section>(&node)) {
            const auto r = toml::resolve_section(segment_names(s->segments));
            current = r.kind == Resolution::Kind::known ? std::optional(r.group) : std::nullopt;
            continue;
        }
        const auto& kv = std::get<toml::KeyValue>(node);
        if (current != group || kv.range.startLine >= line) continue;
        if (!kv.keyPath.empty()) used.insert(kv.keyPath[0].name);
    }
    return used;
}

std::string join(const std::vector<std::string>& parts, std::string_view sep = ".") {
    std::string out;
    for (const auto& p : parts) out += (out.empty() ? "" : std::string(sep)) + p;
    return out;
}

// The enum key a value belongs to: the dotted path whole, else its first segment.
const Json* enum_key_of(const Json* section, const std::vector<std::string>& keyPath) {
    if (!keys_of(section) || keyPath.empty()) return nullptr;
    const Json* key = find_key(section, join(keyPath));
    if (!key) key = find_key(section, keyPath[0]);
    return key && str(*key, "type") == "enum" ? key : nullptr;
}

// The first character of a line that is not white space, or npos (`search(/\S/)`).
std::size_t first_non_space(std::string_view line) {
    std::size_t i = 0;
    while (i < line.size()) {
        const auto n = text::space_at(line, i);
        if (!n) return i;
        i += n;
    }
    return std::string_view::npos;
}

}  // namespace

std::vector<Suggestion> completions(const std::vector<std::string>& lines, int line, int character, std::string_view locale) {
    const auto context = toml::context_at(lines, line, character);
    const int cl = std::clamp(line, 0, std::max(0, static_cast<int>(lines.size()) - 1));
    auto out = [&](std::vector<Suggestion> v) {
        for (auto& s : v) {
            s.start = u16(lines, cl, s.start);
            s.end = u16(lines, cl, s.end);
        }
        return v;
    };
    if (context.kind == Context::Kind::header) {
        // `[[`: mcpp's tables are not arrays, and a suggestion would turn one into `[x]` unseen.
        if (context.isArray) return {};
        // A suggestion is the whole `[x]`: from the line's `[` when it begins the line.
        const std::string_view lineText = line >= 0 && line < static_cast<int>(lines.size()) ? std::string_view(lines[line]) : std::string_view();
        const auto bracket = lineText.find('[');
        const auto firstNonWs = first_non_space(lineText);
        const int start = bracket != std::string_view::npos && bracket == firstNonWs ? static_cast<int>(bracket) : context.replace.start;
        return out(header_suggestions(start, context.replace.end, locale));
    }
    if (context.kind == Context::Kind::key) {
        const auto& section = context.section;
        if (section.kind == Resolution::Kind::top) return out(header_suggestions(context.replace.start, context.replace.end, locale));
        if (section.kind != Resolution::Kind::known || !context.containerPath.empty()) return {};
        if (kDependencyGroups.contains(section.group))
            return out(template_suggestions(kDependencyTemplates, context.replace.start, context.replace.end, locale));
        const auto* schemaSection = section_by_name(section.group);
        // A table whose keys the user names has no fixed list.
        if (schemaSection && !schemaSection->value("openKeys", false) && keys_of(schemaSection)) {
            const auto used = keys_used_before(lines, line, section.group);
            std::vector<Suggestion> v;
            for (const auto& key : *keys_of(schemaSection)) {
                const auto name = str(key, "key");
                if (used.contains(name)) continue;
                v.push_back({name, "template", key_detail(key), key_documentation(key), name + " = " + key_placeholder(key),
                             context.replace.start, context.replace.end});
            }
            return out(std::move(v));
        }
        return out(template_suggestions(templates_of(section.group), context.replace.start, context.replace.end, locale));
    }
    // A value: only an enum key's values; a free value is not guessed.
    if (context.section.kind == Resolution::Kind::known) {
        const auto* key = enum_key_of(section_by_name(context.section.group), context.keyPath);
        if (key) {
            std::vector<Suggestion> v;
            for (const auto& value : (*key)["values"]) {
                const auto s = value.get<std::string>();
                // In a string only its content; a bare value gets its quotes.
                v.push_back({s, "template", "value of " + str(*key, "key"), std::nullopt,
                             context.insideString ? s : text::stringify(value), context.replace.start, context.replace.end});
            }
            return out(std::move(v));
        }
    }
    return {};
}

std::optional<VersionContext> dependency_version_context(const std::vector<std::string>& lines, int line, int character) {
    const auto context = toml::context_at(lines, line, character);
    if (context.kind != Context::Kind::value || context.section.kind != Resolution::Kind::known) return std::nullopt;
    if (!kDependencyGroups.contains(context.section.group) || context.keyPath.empty()) return std::nullopt;
    if (context.keyPath.size() > 1 && context.keyPath.back() != "version") return std::nullopt;
    // A version is a string, or a value not typed yet.
    if (context.valueKind && *context.valueKind != toml::ValueKind::string && *context.valueKind != toml::ValueKind::unknown)
        return std::nullopt;
    const int cl = std::clamp(line, 0, std::max(0, static_cast<int>(lines.size()) - 1));
    return VersionContext{context.keyPath[0], context.insideString, u16(lines, cl, context.replace.start),
                          u16(lines, cl, context.replace.end)};
}

namespace {

// Numeric segments by value, a missing one first, others in collation order.
int compare_versions(std::string_view a, std::string_view b) {
    auto parts = [](std::string_view s) {
        std::vector<std::string_view> out;
        std::size_t at = 0;
        for (std::size_t i = 0; i <= s.size(); ++i)
            if (i == s.size() || s[i] == '.' || s[i] == '+' || s[i] == '-') {
                out.push_back(s.substr(at, i - at));
                at = i + 1;
            }
        return out;
    };
    const auto l = parts(a), r = parts(b);
    auto numeric = [](std::string_view s) {
        return !s.empty() && std::ranges::all_of(s, [](char c) { return c >= '0' && c <= '9'; });
    };
    for (std::size_t i = 0; i < std::max(l.size(), r.size()); ++i) {
        if (i >= l.size()) return -1;
        if (i >= r.size()) return 1;
        if (numeric(l[i]) && numeric(r[i])) {
            const auto x = std::stod(std::string(l[i])), y = std::stod(std::string(r[i]));
            if (x != y) return x < y ? -1 : 1;
            continue;
        }
        if (const int o = text::locale_compare(l[i], r[i]); o != 0) return o < 0 ? -1 : 1;
    }
    return 0;
}

}  // namespace

std::vector<PackageVersion> catalogue_versions(const Json& searchEnvelope, std::string_view name) {
    std::vector<PackageVersion> out;
    std::set<std::string> seen;
    const auto& data = searchEnvelope.contains("data") ? searchEnvelope["data"] : searchEnvelope;
    if (!data.contains("packages") || !data["packages"].is_array()) return out;
    for (const auto& p : data["packages"]) {
        if (str(p, "name") != name || !p.contains("versions") || !p["versions"].is_array()) continue;
        const auto summary = std::string(text::trim(str(p, "description")));
        for (const auto& v : p["versions"]) {
            if (!v.is_string() || !seen.insert(v.get<std::string>()).second) continue;
            out.push_back({v.get<std::string>(), summary.empty() ? std::nullopt : std::optional(summary)});
        }
    }
    std::ranges::stable_sort(out, [](const auto& x, const auto& y) { return compare_versions(y.version, x.version) < 0; });
    return out;
}

std::vector<Suggestion> version_suggestions(const VersionContext& at, const std::vector<PackageVersion>& versions) {
    std::vector<Suggestion> out;
    for (const auto& v : versions)
        out.push_back({v.version, "version", v.summary ? at.name + ": " + *v.summary : "version of " + at.name, std::nullopt,
                       at.insideString ? v.version : text::stringify(Json(v.version)), at.start, at.end});
    return out;
}

// ── hover ──

namespace {

std::string unquote_or_self(std::string_view t) {
    if (t.size() >= 2 && ((t.front() == '"' && t.back() == '"') || (t.front() == '\'' && t.back() == '\'')))
        return std::string(t.substr(1, t.size() - 2));
    return std::string(t);
}

// A header's segments to a section: the whole name, the reading of a table the rules layer knows,
// else the longest leading run that names one.
const Json* section_for_header(const std::vector<std::string>& segments) {
    if (segments.empty()) return nullptr;
    if (const auto* exact = section_by_name(join(segments))) return exact;
    const auto r = toml::resolve_section(segments);
    // A table read as a group the snapshot lacks shows nothing rather than another table's keys.
    if (r.kind == Resolution::Kind::known) return section_by_name(r.group);
    for (auto length = segments.size() - 1; length >= 1; --length)
        if (const auto* s = section_by_name(join(std::vector(segments.begin(), segments.begin() + static_cast<long>(length)))))
            return s;
    return nullptr;
}

std::string values_list(const Json& key) {
    std::string out;
    for (const auto& v : key["values"]) out += std::string(out.empty() ? "" : " | ") + "`" + v.get<std::string>() + "`";
    return out;
}

std::string paragraphs(const std::vector<std::string>& lines) { return join(lines, "\n\n"); }

HoverInfo section_hover(const Json& section) {
    std::vector<std::string> lines{std::format("plane: `{}`", str(section, "plane"))};
    if (has_string(section, "deprecatedBy")) lines.push_back(std::format("legacy: replaced by `{}`", str(section, "deprecatedBy")));
    const auto* keys = keys_of(&section);
    lines.push_back(!keys ? std::string("keys: none declared in the snapshot")
                          : std::format("keys: {}{}", keys->size(), section.value("openKeys", false) ? " (open: the user picks the names)" : ""));
    HoverInfo info{str(section, "header"), paragraphs(lines), std::nullopt};
    if (section.contains("doc") && section["doc"].is_string()) info.documentation = section["doc"].get<std::string>();
    return info;
}

HoverInfo key_hover(const Json& key, const Json* section) {
    std::vector<std::string> lines{std::format("type: `{}`", str(key, "type"))};
    if (has_values(key)) lines.push_back("values: " + values_list(key));
    if (key.contains("default")) lines.push_back(std::format("default: `{}`", text::stringify(key["default"])));
    if (key.contains("since") && key["since"].is_string()) lines.push_back("since: " + key["since"].get<std::string>());
    if (has_string(key, "note")) lines.push_back(str(key, "note"));
    if (key.value("legacy", false) == true) lines.push_back("legacy: this key is deprecated");
    if (key.value("unmodelled", false) == true) lines.push_back("unmodelled: the type is inferred from the docs, not read from mcpp");
    HoverInfo info{str(key, "key"), paragraphs(lines), std::nullopt};
    if (section && section->contains("doc") && (*section)["doc"].is_string()) info.documentation = (*section)["doc"].get<std::string>();
    return info;
}

HoverInfo enum_value_hover(const Json& key, const std::string& value, const Json* section) {
    std::vector<std::string> lines{std::format("enum value of `{}`", str(key, "key"))};
    if (has_values(key)) lines.push_back("one of: " + values_list(key));
    if (key.contains("default")) lines.push_back(std::format("default: `{}`", text::stringify(key["default"])));
    if (has_string(key, "note")) lines.push_back(str(key, "note"));
    HoverInfo info{value.empty() ? str(key, "key") : value, paragraphs(lines), std::nullopt};
    if (section && section->contains("doc") && (*section)["doc"].is_string()) info.documentation = (*section)["doc"].get<std::string>();
    return info;
}

// The text of a replaced range on a line, clamped.
std::string_view token_at(std::string_view lineText, toml::ReplaceRange r) {
    const auto start = static_cast<std::size_t>(std::clamp(r.start, 0, static_cast<int>(lineText.size())));
    const auto end = static_cast<std::size_t>(std::clamp(r.end, static_cast<int>(start), static_cast<int>(lineText.size())));
    return lineText.substr(start, end - start);
}

}  // namespace

std::optional<HoverInfo> hover_at(const std::vector<std::string>& lines, int line, int character) {
    if (lines.empty()) return std::nullopt;
    const auto context = toml::context_at(lines, line, character);
    const std::string_view lineText = lines[std::clamp(line, 0, static_cast<int>(lines.size()) - 1)];
    if (context.kind == Context::Kind::header) {
        auto segments = context.segments;
        // On a segment's token: the segments before it, and the token.
        const auto token = text::trim(token_at(lineText, context.replace));
        if (!token.empty()) segments.push_back(unquote_or_self(token));
        const auto* section = section_for_header(segments);
        if (!section) return std::nullopt;
        return section_hover(*section);
    }
    if (context.kind == Context::Kind::key) {
        if (context.section.kind != Resolution::Kind::known || !context.containerPath.empty()) return std::nullopt;
        const auto* section = section_by_name(context.section.group);
        if (!keys_of(section)) return std::nullopt;
        // A dotted key's root is the schema's key (`metadata.demo` is `metadata`).
        const auto token = unquote_or_self(text::trim(token_at(lineText, context.replace)));
        const auto name = !context.keyPrefix.empty() ? context.keyPrefix[0] : token;
        const auto* key = find_key(section, name);
        if (!key) return std::nullopt;
        return key_hover(*key, section);
    }
    if (context.section.kind != Resolution::Kind::known) return std::nullopt;
    const auto* section = section_by_name(context.section.group);
    if (!keys_of(section) || context.keyPath.empty()) return std::nullopt;
    const Json* key = find_key(section, join(context.keyPath));
    if (!key) key = find_key(section, context.keyPath[0]);
    if (!key || str(*key, "type") != "enum") return std::nullopt;
    const auto value = std::string(text::trim(unquote_or_self(token_at(lineText, context.replace))));
    return enum_value_hover(*key, value, section);
}

// ── navigation ──

namespace {

bool contains(const toml::Range& r, int line, int col) {
    if (line < r.startLine || line > r.endLine) return false;
    if (line == r.startLine && col < r.startCol) return false;
    if (line == r.endLine && col > r.endCol) return false;
    return true;
}

struct Candidate {
    const toml::KeyValue* node;
    std::optional<std::string> ownerKey;
    std::vector<std::string> sectionSegments;
};

void collect_nested(const std::optional<toml::Value>& value, const std::string& owner,
                    const std::vector<std::string>& sectionSegments, std::vector<Candidate>& out);

void collect_nested_value(const toml::Value& value, const std::string& owner, const std::vector<std::string>& sectionSegments,
                          std::vector<Candidate>& out) {
    if (value.kind == toml::ValueKind::inlineTable) {
        for (const auto& entry : value.entries) {
            out.push_back({&entry, owner, sectionSegments});
            collect_nested(entry.value, owner, sectionSegments, out);
        }
        return;
    }
    if (value.kind == toml::ValueKind::array)
        for (const auto& element : value.elements) collect_nested_value(element, owner, sectionSegments, out);
}

void collect_nested(const std::optional<toml::Value>& value, const std::string& owner,
                    const std::vector<std::string>& sectionSegments, std::vector<Candidate>& out) {
    if (value) collect_nested_value(*value, owner, sectionSegments, out);
}

std::optional<std::string> group_of(const std::vector<std::string>& segments) {
    if (segments.empty()) return std::nullopt;
    const auto r = toml::resolve_section(segments);
    return r.kind == Resolution::Kind::known ? std::optional(r.group) : std::nullopt;
}

std::string dependency_name(const Candidate& c) {
    if (c.ownerKey && !c.ownerKey->empty()) return *c.ownerKey;
    if (c.sectionSegments.size() >= 2) return join(std::vector(c.sectionSegments.begin() + 1, c.sectionSegments.end()));
    return {};
}

bool relative_path(std::string_view value) {
    const auto t = text::trim(value);
    if (t.empty() || t.starts_with("/") || t.starts_with("\\") || t.starts_with("~")) return false;
    if (t.size() >= 3 && std::isalpha(static_cast<unsigned char>(t[0])) && t[1] == ':' && (t[2] == '\\' || t[2] == '/')) return false;
    // `<scheme>://`
    if (!t.empty() && std::isalpha(static_cast<unsigned char>(t[0]))) {
        std::size_t i = 1;
        while (i < t.size() && (std::isalnum(static_cast<unsigned char>(t[i])) || t[i] == '+' || t[i] == '.' || t[i] == '-')) ++i;
        if (t.substr(i).starts_with("://")) return false;
    }
    return true;
}

}  // namespace

std::optional<Location> definition_at(const std::vector<std::string>& lines, int line, int character,
                                      const std::function<std::optional<int>(std::string_view)>& resolve_manifest) {
    const int cursorLine = std::max(0, line);
    const int cursorCol = cursor_byte(lines, cursorLine, std::max(0, character));
    const auto doc = toml::parse(lines);
    std::vector<Candidate> candidates;
    {
        std::vector<std::string> sectionSegments;
        for (const auto& node : doc.nodes) {
            if (const auto* s = std::get_if<toml::Section>(&node)) {
                sectionSegments = segment_names(s->segments);
                continue;
            }
            const auto& kv = std::get<toml::KeyValue>(node);
            candidates.push_back({&kv, std::nullopt, sectionSegments});
            collect_nested(kv.value, join(segment_names(kv.keyPath)), sectionSegments, candidates);
        }
    }
    auto location_of_key = [&](const toml::KeyValue& kv) -> std::optional<Location> {
        if (kv.keyPath.empty()) return std::nullopt;
        const auto& first = kv.keyPath.front().range;
        const auto& last = kv.keyPath.back().range;
        return Location{first.startLine, u16(lines, first.startLine, first.startCol), u16(lines, first.startLine, last.endCol)};
    };
    for (const auto& c : candidates) {
        const auto& node = *c.node;
        const bool onKey = std::ranges::any_of(node.keyPath, [&](const auto& s) { return contains(s.range, cursorLine, cursorCol); });
        const bool onValue = node.value && contains(node.value->range, cursorLine, cursorCol);
        if (!onKey && !onValue) continue;
        const auto keyName = node.keyPath.empty() ? std::string() : node.keyPath.back().name;
        const auto group = group_of(c.sectionSegments);
        const bool inDependencies = group && kDependencyGroups.contains(*group);
        if (keyName == "workspace") {
            const bool isTrue = node.value && node.value->kind == toml::ValueKind::boolean && node.value->text == "true";
            if (!isTrue || !inDependencies) continue;
            const auto dependency = dependency_name(c);
            if (dependency.empty()) continue;
            bool inWorkspace = false;
            for (const auto& n : doc.nodes) {
                if (const auto* s = std::get_if<toml::Section>(&n)) {
                    inWorkspace = group_of(segment_names(s->segments)) == std::optional<std::string>("workspace.dependencies");
                    continue;
                }
                const auto& kv = std::get<toml::KeyValue>(n);
                if (inWorkspace && join(segment_names(kv.keyPath)) == dependency) {
                    if (auto at = location_of_key(kv)) return at;
                    break;
                }
            }
            continue;
        }
        if (keyName == "path") {
            if (!inDependencies || !node.value || node.value->kind != toml::ValueKind::string) continue;
            const auto relative = node.value->text.value_or("");
            if (!relative_path(relative)) continue;
            std::optional<int> resolved;
            try {
                resolved = resolve_manifest(relative);
            } catch (...) {
                resolved = std::nullopt;
            }
            if (!resolved || *resolved < 0) continue;
            // Only the line is known (the other file is not read here): its `[package]`.
            return Location{*resolved, 0, static_cast<int>(std::string_view("[package]").size())};
        }
        if (keyName == "features") {
            if (!node.value || node.value->kind != toml::ValueKind::array) continue;
            const auto element = std::ranges::find_if(node.value->elements, [&](const auto& e) { return contains(e.range, cursorLine, cursorCol); });
            if (element == node.value->elements.end() || element->kind != toml::ValueKind::string) continue;
            const auto name = std::string(text::trim(element->text.value_or("")));
            if (name.empty()) continue;
            for (const auto& n : doc.nodes) {
                const auto* s = std::get_if<toml::Section>(&n);
                if (!s || s->isArray || s->segments.size() != 2) continue;
                if (s->segments[0].name == "features" && s->segments[1].name == name)
                    return Location{s->line, u16(lines, s->line, s->range.startCol), u16(lines, s->line, s->range.endCol)};
            }
            continue;
        }
    }
    return std::nullopt;
}

// ── diagnostics ──

namespace {

enum class Mode { normal, mlDouble, mlSingle };
struct ParseState {
    Mode mode = Mode::normal;
    int depth = 0;
};

bool escaped(std::string_view t, std::size_t index) {
    std::size_t n = 0;
    for (std::size_t i = index; i > 0 && t[i - 1] == '\\'; --i) ++n;
    return n % 2 == 1;
}

// One line, the state across lines updated; the `#` that starts a comment, or npos.
std::size_t scan(std::string_view t, ParseState& state) {
    std::size_t i = 0;
    while (i < t.size()) {
        if (state.mode == Mode::mlDouble) {
            if (t.substr(i).starts_with("\"\"\"") && !escaped(t, i)) {
                state.mode = Mode::normal;
                i += 3;
                continue;
            }
            ++i;
            continue;
        }
        if (state.mode == Mode::mlSingle) {
            if (t.substr(i).starts_with("'''")) {
                state.mode = Mode::normal;
                i += 3;
                continue;
            }
            ++i;
            continue;
        }
        const char ch = t[i];
        if (ch == '#') return i;
        if (ch == '"') {
            if (t.substr(i).starts_with("\"\"\"")) {
                state.mode = Mode::mlDouble;
                i += 3;
                continue;
            }
            ++i;
            while (i < t.size()) {
                if (t[i] == '\\') { i += 2; continue; }
                if (t[i] == '"') { ++i; break; }
                ++i;
            }
            continue;
        }
        if (ch == '\'') {
            if (t.substr(i).starts_with("'''")) {
                state.mode = Mode::mlSingle;
                i += 3;
                continue;
            }
            ++i;
            while (i < t.size()) {
                if (t[i] == '\'') { ++i; break; }
                ++i;
            }
            continue;
        }
        if (ch == '[' || ch == '{') { state.depth += 1; ++i; continue; }
        if (ch == ']' || ch == '}') {
            if (state.depth > 0) state.depth -= 1;
            ++i;
            continue;
        }
        ++i;
    }
    return std::string_view::npos;
}

std::string_view code_before_comment(std::string_view t) {
    ParseState s;
    const auto stop = scan(t, s);
    return stop == std::string_view::npos ? t : t.substr(0, stop);
}

// `needle` outside quotes, or npos.
std::size_t index_of_unquoted(std::string_view t, std::string_view needle) {
    std::size_t i = 0;
    while (i < t.size()) {
        const char ch = t[i];
        if (ch == '"' || ch == '\'') {
            const std::string delim(3, ch);
            if (!t.substr(i).starts_with(delim)) {
                ++i;
                while (i < t.size()) {
                    if (ch == '"' && t[i] == '\\') { i += 2; continue; }
                    if (t[i] == ch) { ++i; break; }
                    ++i;
                }
                continue;
            }
            i += 3;
            while (i < t.size()) {
                if (t.substr(i).starts_with(delim) && !escaped(t, i)) { i += 3; break; }
                ++i;
            }
            continue;
        }
        if (t.substr(i).starts_with(needle)) return i;
        ++i;
    }
    return std::string_view::npos;
}

// A table name or dotted key split at its dots, quotes kept and respected.
std::vector<std::string> split_dots(std::string_view t) {
    std::vector<std::string> parts;
    std::string current;
    char quote = 0;
    for (const char ch : t) {
        if (quote) {
            current += ch;
            if (ch == quote) quote = 0;
            continue;
        }
        if (ch == '"' || ch == '\'') {
            quote = ch;
            current += ch;
            continue;
        }
        if (ch == '.') {
            parts.push_back(std::move(current));
            current.clear();
            continue;
        }
        current += ch;
    }
    parts.push_back(std::move(current));
    return parts;
}

bool bare_key(std::string_view t) {
    return !t.empty() && std::ranges::all_of(t, [](char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-'; });
}
bool quoted_key(std::string_view t) {
    return t.size() >= 2 && ((t.front() == '"' && t.back() == '"') || (t.front() == '\'' && t.back() == '\''));
}
bool valid_key(std::string_view t) {
    const auto parts = split_dots(t);
    return !parts.empty() && std::ranges::all_of(parts, [](const auto& p) { return bare_key(p) || quoted_key(p); });
}
std::optional<std::string> unquote(std::string_view t) {
    if (quoted_key(t)) return std::string(t.substr(1, t.size() - 2));
    return std::nullopt;
}

const Json* lookup_section(std::string_view name) {
    if (const auto* exact = section_by_name(name)) return exact;
    const auto parts = split_dots(name);
    for (auto i = parts.size() - 1; i >= 1; --i)
        if (const auto* s = section_by_name(join(std::vector(parts.begin(), parts.begin() + static_cast<long>(i))))) return s;
    return nullptr;
}

// The section whose keys a table takes: its own, or a row table's base (`[targets.<n>]`,
// `[profile.<n>]`, `[target.<selector>]`); deeper tables are another vocabulary, not checked.
const Json* key_section_of(std::string_view headerName) {
    if (const auto* exact = section_by_name(headerName)) return exact;
    const auto parts = split_dots(headerName);
    if (parts.size() == 2 && (parts[0] == "targets" || parts[0] == "profile" || parts[0] == "target")) return section_by_name(parts[0]);
    return nullptr;
}

const Json* key_info_of(const Json* section, std::string_view key) {
    if (!keys_of(section)) return nullptr;
    if (const auto* direct = find_key(section, key)) return direct;
    const auto parts = split_dots(key);
    if (parts.size() > 1) return find_key(section, parts[0]);
    return nullptr;
}

bool dependency_section(std::string_view name) {
    const auto parts = split_dots(name);
    const auto& last = parts.back();
    return last == "dependencies" || last == "dev-dependencies" || last == "build-dependencies";
}

bool xlings_section(std::string_view name) { return std::ranges::contains(split_dots(name), std::string("xlings")); }

bool tool_payload_name(std::string_view key) {
    auto bare = unquote(key).value_or(std::string(key));
    std::ranges::transform(bare, bare.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return bare.starts_with("xim:") || bare.starts_with("xpm:") || bare.starts_with("xlings:") || bare.starts_with("xvm:");
}

// `namespace.name`: /^[A-Za-z_][A-Za-z0-9_-]*(\.[A-Za-z0-9_-]+)+$/
bool mcpp_package_name(std::string_view key) {
    const auto bare = unquote(key).value_or(std::string(key));
    if (tool_payload_name(bare)) return false;
    if (bare.empty() || !(std::isalpha(static_cast<unsigned char>(bare[0])) || bare[0] == '_')) return false;
    auto word = [](char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-'; };
    std::size_t i = 1;
    while (i < bare.size() && word(bare[i])) ++i;
    int dots = 0;
    while (i < bare.size()) {
        if (bare[i] != '.') return false;
        ++i;
        const auto start = i;
        while (i < bare.size() && word(bare[i])) ++i;
        if (i == start) return false;
        ++dots;
    }
    return dots >= 1;
}

// /(^|[,{\s])<word>\s*=/
bool names_field(std::string_view t, std::string_view word) {
    for (std::size_t at = t.find(word); at != std::string_view::npos; at = t.find(word, at + 1)) {
        const bool boundary = at == 0 || t[at - 1] == ',' || t[at - 1] == '{' || text::space_at(t, at - 1) == 1
                           || (at >= 2 && text::space_at(t, at - 2) == 2) || (at >= 3 && text::space_at(t, at - 3) == 3);
        if (!boundary) continue;
        std::size_t i = at + word.size();
        while (auto n = text::space_at(t, i)) i += n;
        if (i < t.size() && t[i] == '=') return true;
    }
    return false;
}

// A version, not a path: `"1.0"`, `{ version = "1.0" }`; not `{ path = ... }`, `""`, `{ linux = "" }`.
bool declares_version(std::string_view valueText) {
    const auto t = text::trim(valueText);
    if (t.starts_with("{")) return names_field(t, "version") && !names_field(t, "path");
    const auto bare = unquote(t);
    if (!bare || bare->empty()) return false;
    return bare->find('/') == std::string::npos && !bare->starts_with(".");
}

// The arrays of tables mcpp reads (toml.cppm's kAllowedArraysOfTables, and the snapshot's own).
bool allowed_array_table(std::string_view name) {
    static const std::vector<std::vector<std::string>> allowed = [] {
        std::vector<std::vector<std::string>> out = {
            {"build", "flags"}, {"build", "sources"}, {"features", "*", "flags"}, {"runtime", "requirements"},
            {"runtime", "artifacts"}, {"runtime", "deploy"}, {"target", "*", "build", "flags"},
            {"target", "*", "build", "sources"}, {"xlings", "deps"},
        };
        for (const auto& s : schema()["sections"])
            if (str(s, "header").starts_with("[[")) out.push_back(split_dots(str(s, "name")));
        return out;
    }();
    const auto parts = split_dots(name);
    return std::ranges::any_of(allowed, [&](const auto& pattern) {
        if (pattern.size() != parts.size()) return false;
        for (std::size_t i = 0; i < parts.size(); ++i)
            if (pattern[i] != "*" && pattern[i] != parts[i]) return false;
        return true;
    });
}

// `">=<release>"`: /^>=\d+(\.\d+)+$/
bool mcpp_floor(std::string_view valueText) {
    const auto bare = unquote(text::trim(valueText));
    if (!bare || !bare->starts_with(">=")) return false;
    const std::string_view v = std::string_view(*bare).substr(2);
    int groups = 0;
    std::size_t i = 0;
    for (;;) {
        const auto start = i;
        while (i < v.size() && std::isdigit(static_cast<unsigned char>(v[i]))) ++i;
        if (i == start) return false;
        ++groups;
        if (i == v.size()) break;
        if (v[i] != '.') return false;
        ++i;
    }
    return groups >= 2;
}

}  // namespace

std::vector<Diagnostic> analyse(const std::vector<std::string>& lines, const DiagnosticSettings& settings) {
    std::vector<Diagnostic> found;
    auto push = [&](std::string_view rule, const std::string& level, std::string message, int line, int start, int end,
                    std::optional<std::string> relatedKey = std::nullopt) {
        if (level == "off") return;
        found.push_back({std::format("mcpp.manifest.{}", rule), std::move(message), level, line, start, end, std::move(relatedKey)});
    };
    ParseState state;
    std::optional<std::string> headerName;
    const Json* section = nullptr;
    for (std::size_t index = 0; index < lines.size(); ++index) {
        std::string_view raw = lines[index];
        if (!raw.empty() && raw.back() == '\r') raw.remove_suffix(1);
        const int lineNumber = static_cast<int>(index) + 1;
        // A continuation of a multi-line string or an array: part of the statement above.
        if (state.mode != Mode::normal || state.depth > 0) {
            scan(raw, state);
            continue;
        }
        const auto code = code_before_comment(raw);
        const auto t = text::trim(code);
        if (t.empty()) {
            scan(raw, state);
            continue;
        }
        const auto lead = code.size() - text::trim_start(code).size();
        const int lineStart = text::utf16_column(raw, lead) + 1;
        const int lineEnd = text::utf16_column(raw, lead + t.size()) + 1;
        const std::string tstr(t);

        if (t.starts_with("[")) {
            const bool arrayHeader = t.size() >= 5 && t.starts_with("[[") && t.ends_with("]]");
            const bool plainHeader = t.size() >= 3 && t.ends_with("]");
            if (!arrayHeader && !plainHeader) {
                push("syntax", settings.syntax, "not a [section] header: " + tstr, lineNumber, lineStart, lineEnd);
                scan(raw, state);
                continue;
            }
            const auto name = std::string(text::trim(arrayHeader ? t.substr(2, t.size() - 4) : t.substr(1, t.size() - 2)));
            headerName = name;
            section = key_section_of(name);
            if (arrayHeader) {
                if (!allowed_array_table(name))
                    push("arrayTable", settings.arrayTable, std::format("[[{}]] is an array of tables, which mcpp does not use here", name),
                         lineNumber, lineStart, lineEnd, name);
            } else if (!lookup_section(name)) {
                push("unknownSection", settings.unknownSection, std::format("unknown section [{}]", name), lineNumber, lineStart, lineEnd, name);
            } else if (section && has_string(*section, "deprecatedBy")) {
                const auto by = str(*section, "deprecatedBy");
                push("legacyKey", settings.legacyKeys, std::format("[{}] is deprecated; use {}", name, by), lineNumber, lineStart, lineEnd, by);
            }
            scan(raw, state);
            continue;
        }

        const auto equals = index_of_unquoted(t, "=");
        if (equals == std::string_view::npos || equals == 0) {
            push("syntax", settings.syntax, "not a [section] header or key = value: " + tstr, lineNumber, lineStart, lineEnd);
            scan(raw, state);
            continue;
        }
        const auto key = std::string(text::trim(t.substr(0, equals)));
        const auto value = std::string(text::trim(t.substr(equals + 1)));
        const int keyStart = lineStart;
        const int keyEnd = keyStart + text::utf16_length(key);
        if (!valid_key(key)) {
            push("syntax", settings.syntax, "invalid key: " + key, lineNumber, keyStart, keyEnd);
            scan(raw, state);
            continue;
        }
        if (value.empty()) {
            push("syntax", settings.syntax, std::format("key {} has no value", key), lineNumber, keyStart, keyEnd, key);
            scan(raw, state);
            continue;
        }
        const auto* info = key_info_of(section, key);
        if (keys_of(section) && !section->value("openKeys", false) && !info)
            push("unknownKey", settings.unknownKey, std::format("unknown key {} in {}", key, str(*section, "header")), lineNumber,
                 keyStart, keyEnd, key);
        if (info && info->value("legacy", false) == true && !(section && has_string(*section, "deprecatedBy"))) {
            const auto replacement = info->contains("note") && (*info)["note"].is_string() ? (*info)["note"].get<std::string>()
                                                                                             : std::string("no replacement documented");
            push("legacyKey", settings.legacyKeys, std::format("key {} is deprecated; {}", key, replacement), lineNumber, keyStart, keyEnd, key);
        }
        if (section && str(*section, "name") == "package" && split_dots(key)[0] == "mcpp" && !mcpp_floor(value))
            push("mcppFloor", settings.mcppFloor, "[package] mcpp must be a release floor like \">=2026.9.28.3\"", lineNumber, keyStart,
                 keyEnd, "mcpp");
        const auto bareKey = unquote(key).value_or(key);
        if (dependency_section(headerName.value_or("")) && tool_payload_name(bareKey)) {
            push("planeSeparation", settings.planeSeparation,
                 std::format("{} is a tool payload, not a library dependency; declare it in [xlings]/[xlings.workspace]", bareKey),
                 lineNumber, keyStart, keyEnd, bareKey);
        } else if (xlings_section(headerName.value_or("")) && mcpp_package_name(bareKey) && declares_version(value)) {
            push("planeSeparation", settings.planeSeparation,
                 std::format("{} is an mcpp package, not a tool payload; declare it in [dependencies]", bareKey), lineNumber, keyStart,
                 keyEnd, bareKey);
        }
        scan(raw, state);
    }
    std::ranges::stable_sort(found, [](const Diagnostic& a, const Diagnostic& b) {
        if (a.line != b.line) return a.line < b.line;
        if (a.start != b.start) return a.start < b.start;
        if (const int c = text::locale_compare(a.code, b.code); c != 0) return c < 0;
        return text::locale_compare(a.message, b.message) < 0;
    });
    return found;
}

// ── outline ──

Json document_symbols(const std::vector<std::string>& lines) {
    Json out = Json::array();
    Json* current = nullptr;
    auto range = [&](const toml::Range& r) {
        return Json{{"start", {{"line", r.startLine}, {"character", u16(lines, r.startLine, r.startCol)}}},
                    {"end", {{"line", r.endLine}, {"character", u16(lines, r.endLine, r.endCol)}}}};
    };
    for (const auto& node : toml::parse(lines).nodes) {
        if (const auto* s = std::get_if<toml::Section>(&node)) {
            if (s->segments.empty()) continue;
            const auto name = join(segment_names(s->segments));
            // LSP SymbolKind: Namespace 3, Property 7.
            out.push_back(Json{{"name", s->isArray ? "[[" + name + "]]" : "[" + name + "]"}, {"kind", 3},
                               {"range", range(s->range)}, {"selectionRange", range(s->range)}, {"children", Json::array()}});
            current = &out.back();
            continue;
        }
        const auto& kv = std::get<toml::KeyValue>(node);
        if (kv.keyPath.empty()) continue;
        Json sym{{"name", join(segment_names(kv.keyPath))}, {"kind", 7}, {"range", range(kv.range)},
                 {"selectionRange", range(toml::Range{kv.keyPath.front().range.startLine, kv.keyPath.front().range.startCol,
                                                      kv.keyPath.back().range.endLine, kv.keyPath.back().range.endCol})}};
        if (current) (*current)["children"].push_back(std::move(sym));
        else out.push_back(std::move(sym));
    }
    return out;
}

Json schema_source() {
    return Json{{"version", str(schema(), "sourceVersion")}, {"commit", str(schema(), "sourceCommit")}};
}

}  // namespace mcppide::manifest
