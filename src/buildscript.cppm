// The rules layer for build.mcpp: the SPEC-007 rule checks, completion of `mcpp::` names and of the
// modules a build program may import, the API's snippets, and hover -- from the snapshot of mcpp's
// build-program directives (spec/buildscript-api.json) and nothing else. No compiler runs and no
// workspace is read, so it serves an untrusted workspace and cannot disagree with the build over a
// stale database.
//
// While C++ Modules (mcppls) serves the file it compiles the file against the modules it really
// imports, and what it answers better is left to it: the `unknownSymbol` rule, the module names
// after `import` and their hover. The rule checks and the snippets stay: no compiler says those.
// The layer never says "module not found": a build program's imports are resolved by mcpp's module
// graph (docs/specs/build-plugins.md §9), which this layer only recognises, never validates.
//
// It is line-oriented, not a parser: no preprocessor, no types, no evaluation of a condition. The
// one cross-line heuristic is the action block, from `mcpp::action` to `.submit(`, up to 40 lines.
// Every rule errs towards silence. Diagnostic positions are 1-based (`line:column`); a column counts
// UTF-16 units. A port of mcpp-vscode 0.9's src/buildscript/{analysis,api,modules,snippets,providers}.ts
// (design 2026-10-30 part 2 §8); tests/rules holds that implementation's answers.
export module mcppide.buildscript;

import std;
import nlohmann.json;
import mcppide.spec;
import mcppide.text;

export namespace mcppide::buildscript {

using Json = nlohmann::json;

// The five action roles (`mcpp::roles::*`), in the engine's order.
inline constexpr std::array<std::string_view, 5> kActionRoles = {"source", "check", "object", "artifact", "prepare"};

// A directive of the snapshot by its wire name (`link-search`), or null.
const Json* directive(std::string_view wire);

struct KnownModule {
    std::string name, description, docsUrl;
};
// std, std.compat, mcpp, mcpp.core and the reserved `mcpp.<namespace>.*` patterns, in the order a
// list shows them.
const std::vector<KnownModule>& known_modules();
// The module, a concrete name matching a pattern answered with that name; none for any other.
std::optional<KnownModule> known_module(std::string_view name);

// An `import` site: the module name's range (0-based line, UTF-16 columns).
struct ImportSite {
    std::string module;
    int line = 0, start = 0, end = 0;
};
std::vector<ImportSite> scan_imports(const std::vector<std::string>& lines);

struct Diagnostic {
    std::string code, message, severity;
    int line = 0, start = 0, end = 0;   // 1-based
};
// Every diagnostic the text earns, at `severity`. `unknownSymbols`: rule 1, off while mcppls serves.
std::vector<Diagnostic> analyse(const std::vector<std::string>& lines, std::string_view severity, bool unknownSymbols = true);

struct Snippet {
    std::string filterText, label, detail;
    std::optional<std::string> documentation;
    std::string insertText;
};
std::vector<Snippet> snippets();

// A completion item: kind module | function | snippet.
struct Item {
    std::string label, kind, detail;
    std::optional<std::string> documentation, filterText, insertText;
};
struct Switches {
    bool snippets = true;       // mcpp.buildScript.snippets
    bool knownModules = true;   // mcpp.buildScript.imports.knownModules
    bool served = false;        // mcppls serves the file
};
// What completes at the cursor: none outside `import ` and `mcpp::`.
std::optional<std::vector<Item>> completion(std::string_view line, int character, const Switches& s = {});
// The hover at a cursor (markdown), none where there is nothing to say.
std::optional<std::string> hover(const std::vector<std::string>& lines, int line, int character, bool served = false);

// The snapshot's provenance.
Json api_source();

}  // namespace mcppide::buildscript

namespace mcppide::buildscript {

namespace {

const Json& api() { return spec::buildscript_api(); }

std::string str(const Json& j, std::string_view key) {
    auto it = j.find(key);
    return it != j.end() && it->is_string() ? it->get<std::string>() : std::string();
}

std::string underscored(std::string s) {
    std::ranges::replace(s, '-', '_');
    return s;
}

std::string joined(const std::vector<std::string>& v, std::string_view sep) {
    std::string out;
    for (const auto& s : v) out += (out.empty() ? "" : std::string(sep)) + s;
    return out;
}

std::string roles_list(std::string_view sep = ", ") {
    std::string out;
    for (auto r : kActionRoles) out += (out.empty() ? "" : std::string(sep)) + std::string(r);
    return out;
}

// The typed-API names docs/30 lists: a name here is not flagged (a wrong one costs a missed hint).
constexpr std::string_view kTypedApiNames[] = {
    "abi_tool", "accel", "action", "cflag", "cxx_runtime", "cxx_stdlib", "cxxflag", "decision", "define", "dep_bin",
    "dep_dir", "dep_linkage", "deploy", "device_sources", "fact", "floor", "generated", "graph_file", "has_feature",
    "host", "include_dir", "include_dir_after", "link_flag", "link_lib", "link_script", "link_search", "manifest_dir",
    "min_platform_version", "msvc_crt_linkage", "msvc_instance_dir", "ninja_program", "out_dir",
    "pack_debug_symbols_dir", "pack_format", "pack_stage_dir", "pack_strip", "package_authors", "package_description",
    "package_license", "package_repo", "package_version", "phase", "pkg_config_libdir", "plugins", "profile",
    "provision", "provides_pack_format", "report", "rerun_if_changed", "rerun_if_changed_glob", "rerun_if_env_changed",
    "roles", "runner", "runtime_search_dir", "source", "stage_dir", "sysroot_dir", "target", "target_arch",
    "target_env", "target_os", "tool", "tool_env", "toolchain", "toolchain_binutils_dir", "toolchain_dir",
    "toolchain_sysroot", "tools", "toolset_identity", "warning", "windows_entry", "windows_subsystem", "xpkg_dir",
    "xpkg_pending", "xpkg_program", "xpkg_request", "xpkg_source",
};

const std::set<std::string, std::less<>>& known_symbols() {
    static const auto names = [] {
        std::set<std::string, std::less<>> out(std::begin(kTypedApiNames), std::end(kTypedApiNames));
        for (auto r : kActionRoles) out.insert(std::string(r));
        for (const auto& d : api()["directives"]) {
            out.insert(str(d, "wire"));
            out.insert(underscored(str(d, "wire")));
        }
        return out;
    }();
    return names;
}

bool is_role(std::string_view s) { return std::ranges::find(kActionRoles, s) != kActionRoles.end(); }

constexpr std::string_view kMcppDocs = "https://github.com/mcpp-community/mcpp/blob/main/docs/30-build-mcpp.md";

// A C++ module name: /^[A-Za-z_][A-Za-z0-9_]*(?:\.[A-Za-z_][A-Za-z0-9_]*)*$/
bool module_name(std::string_view s) {
    static const std::regex re(R"(^[A-Za-z_][A-Za-z0-9_]*(?:\.[A-Za-z_][A-Za-z0-9_]*)*$)");
    return std::regex_match(s.begin(), s.end(), re);
}

int u16(std::string_view line, std::size_t byte) { return text::utf16_column(line, byte); }

// ── masking ──

struct ScanState {
    bool blockComment = false;
    std::optional<std::string> rawDelim;
};

struct StringLiteral {
    std::string value;      // the characters it denotes
    std::size_t start = 0;  // the opening quote
    std::size_t end = 0;    // one past the closing quote
    std::string raw;        // as written
};

struct MaskedLine {
    std::string code;       // comments and literals blanked, offsets kept
    std::vector<StringLiteral> strings;
};

std::string unescape(std::string_view raw) {
    std::string out;
    for (std::size_t i = 0; i < raw.size(); ++i) {
        if (raw[i] != '\\') { out += raw[i]; continue; }
        if (i + 1 >= raw.size()) { out += '\\'; ++i; continue; }
        const char next = raw[i + 1];
        out += next == 'n' ? '\n' : next == 't' ? '\t' : next == 'r' ? '\r' : next;
        ++i;
    }
    return out;
}

// `R"delim(` at the start of `s`: the delimiter.
std::optional<std::pair<std::string, std::size_t>> raw_open(std::string_view s) {
    static const std::regex re(R"(^R"([^(\s]{0,16})\()");
    std::match_results<std::string_view::const_iterator> m;
    if (!std::regex_search(s.begin(), s.end(), m, re)) return std::nullopt;
    return std::pair{m[1].str(), static_cast<std::size_t>(m[0].length())};
}

bool ident_char(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; }

// The analysis's masking: the line with every comment and literal character a space, and the
// literals. `/*` itself stays (only what follows it is blanked), as the analysis has always read it.
MaskedLine mask_for_analysis(std::string_view line, ScanState& state) {
    std::string out(line);
    std::vector<StringLiteral> strings;
    auto blank = [&](std::size_t from, std::size_t to) {
        for (std::size_t i = from; i < std::min(to, line.size()); ++i) out[i] = ' ';
    };
    std::size_t i = 0;
    if (state.rawDelim) {
        const auto terminator = ")" + *state.rawDelim + "\"";
        const auto end = line.find(terminator);
        if (end == std::string_view::npos) return {std::string(line.size(), ' '), strings};
        blank(0, end + terminator.size());
        i = end + terminator.size();
        state.rawDelim.reset();
    }
    while (i < line.size()) {
        if (state.blockComment) {
            const auto end = line.find("*/", i);
            if (end == std::string_view::npos) {
                blank(i, line.size());
                break;
            }
            blank(i, end + 2);
            i = end + 2;
            state.blockComment = false;
            continue;
        }
        const auto pair = line.substr(i, 2);
        if (pair == "//") {
            blank(i, line.size());
            break;
        }
        if (pair == "/*") {
            state.blockComment = true;
            i += 2;
            continue;
        }
        if (line[i] == 'R' && i + 1 < line.size() && line[i + 1] == '"') {
            if (auto open = raw_open(line.substr(i))) {
                const auto terminator = ")" + open->first + "\"";
                const auto end = line.find(terminator, i + open->second);
                if (end == std::string_view::npos) {
                    state.rawDelim = open->first;
                    blank(i, line.size());
                    break;
                }
                blank(i, end + terminator.size());
                i = end + terminator.size();
                continue;
            }
        }
        // `'` after an identifier character is a digit separator (1'000).
        if (line[i] == '\'' && i > 0 && ident_char(line[i - 1])) {
            ++i;
            continue;
        }
        if (line[i] == '"' || line[i] == '\'') {
            const char quote = line[i];
            std::size_t j = i + 1;
            while (j < line.size()) {
                if (line[j] == '\\') { j += 2; continue; }
                if (line[j] == quote) { ++j; break; }
                ++j;
            }
            const auto stop = std::min(j, line.size());
            const auto contentEnd = std::max(i + 1, stop - 1);
            strings.push_back({unescape(line.substr(i + 1, contentEnd - (i + 1))), i, stop, std::string(line.substr(i, stop - i))});
            blank(i, stop);
            i = stop;
            continue;
        }
        ++i;
    }
    return {out, strings};
}

// The import scanner's masking: comments and literals, `/*` included, each blanked character one
// space per UTF-16 unit and a digit separator dropped -- the text whose offsets the scanner has
// always reported.
std::string mask_for_imports(std::string_view line, ScanState& state) {
    std::string out;
    auto blank = [&](std::size_t from, std::size_t to) {
        to = std::min(to, line.size());
        if (from < to) out += std::string(static_cast<std::size_t>(text::utf16_length(line.substr(from, to - from))), ' ');
    };
    std::size_t i = 0;
    if (state.rawDelim) {
        const auto terminator = ")" + *state.rawDelim + "\"";
        const auto end = line.find(terminator, i);
        if (end == std::string_view::npos) {
            blank(0, line.size());
            return out;
        }
        blank(0, end + terminator.size());
        i = end + terminator.size();
        state.rawDelim.reset();
    }
    while (i < line.size()) {
        if (state.blockComment) {
            const auto end = line.find("*/", i);
            if (end == std::string_view::npos) {
                blank(i, line.size());
                return out;
            }
            blank(i, end + 2);
            i = end + 2;
            state.blockComment = false;
            continue;
        }
        const auto pair = line.substr(i, 2);
        if (pair == "//") {
            blank(i, line.size());
            return out;
        }
        if (pair == "/*") {
            state.blockComment = true;
            out += "  ";
            i += 2;
            continue;
        }
        if (line[i] == 'R' && i + 1 < line.size() && line[i + 1] == '"') {
            if (auto open = raw_open(line.substr(i))) {
                const auto terminator = ")" + open->first + "\"";
                const auto end = line.find(terminator, i + open->second);
                if (end == std::string_view::npos) {
                    state.rawDelim = open->first;
                    blank(i, line.size());
                    return out;
                }
                const auto stop = end + terminator.size();
                blank(i, stop);
                i = stop;
                continue;
            }
        }
        if (line[i] == '\'' && i > 0 && ident_char(line[i - 1])) {
            ++i;
            continue;
        }
        if (line[i] == '"' || line[i] == '\'') {
            const char quote = line[i];
            std::size_t j = i + 1;
            while (j < line.size()) {
                if (line[j] == '\\') { j += 2; continue; }
                if (line[j] == quote) { ++j; break; }
                ++j;
            }
            const auto stop = std::min(j, line.size());
            blank(i, stop);
            i = stop;
            continue;
        }
        out += line[i];
        ++i;
    }
    return out;
}

using Match = std::match_results<std::string::const_iterator>;

std::optional<Match> search(const std::string& s, const std::regex& re) {
    Match m;
    if (std::regex_search(s, m, re)) return m;
    return std::nullopt;
}

std::string_view trim(std::string_view s) { return text::trim(s); }

}  // namespace

const Json* directive(std::string_view wire) {
    for (const auto& d : api()["directives"])
        if (str(d, "wire") == wire) return &d;
    return nullptr;
}

const std::vector<KnownModule>& known_modules() {
    static const std::vector<KnownModule> all = [] {
        const std::string std_docs = std::string(kMcppDocs) + "#import-std-mcpp-2026821";
        const std::string core_docs = std::string(kMcppDocs) + "#the-interfaces-name-mcppcore-protocol-14";
        const std::string plugin_docs = std::string(kMcppDocs) + "#the-names-of-a-plugins-modules";
        return std::vector<KnownModule>{
            {"std",
             "The C++ standard library module. mcpp stages the same `std` its own build uses, keyed on toolchain × standard × "
             "dialect (`docs/30-build-mcpp.md`, 2026.8.2.1+).",
             std_docs},
            {"std.compat",
             "The C++ standard library plus the C library names in the global namespace; `import std;` and `import std.compat;` "
             "may be used together.",
             std_docs},
            {"mcpp",
             "The mcpp engine interface, bundled in the mcpp binary so it always matches that engine's protocol. The permanent "
             "equivalent spelling of `mcpp.core` (SPEC-007 R9.1).",
             core_docs},
            {"mcpp.core",
             "The mcpp engine interface (L1). Only grows; a symbol leaves only after six months of deprecation, and a meaning "
             "change is a new symbol (SPEC-007 R9.2).",
             core_docs},
            {"mcpp.plugins.*",
             "The official general-purpose library (L2), provided by the `plugins-core` feature and built only on the L1 engine "
             "interface, e.g. `mcpp.plugins.tool` (SPEC-007 §9).",
             plugin_docs},
            {"mcpp.deps.*",
             "Official dependency-adapter plugins (L3), e.g. a package manager or external build system brought into the graph "
             "(SPEC-007 §0, §9).",
             plugin_docs},
            {"mcpp.rules.*", "Official rule packages (L3): code generation and device-language toolchains (SPEC-007 §0, §9).",
             plugin_docs},
            {"mcpp.dist.*",
             "Official distribution members (L3): they turn a link artifact into something installable, e.g. a WiX or APK "
             "payload (SPEC-007 §0, §9).",
             plugin_docs},
            {"mcpp.tools.*", "Official tool namespaces (L3), e.g. `mcpp.tools.island` helpers described in docs/31 (SPEC-007 §9).",
             plugin_docs},
        };
    }();
    return all;
}

std::optional<KnownModule> known_module(std::string_view name) {
    for (const auto& m : known_modules())
        if (!m.name.ends_with(".*") && m.name == name) return m;
    for (const auto& m : known_modules()) {
        if (!m.name.ends_with(".*")) continue;
        const auto prefix = std::string_view(m.name).substr(0, m.name.size() - 1);   // the trailing dot kept
        if (name.starts_with(prefix) && module_name(name.substr(prefix.size())))
            return KnownModule{std::string(name), m.description, m.docsUrl};
    }
    return std::nullopt;
}

std::vector<ImportSite> scan_imports(const std::vector<std::string>& lines) {
    // `import X;`, `export import X;`: a member call (`obj.import(...)`) or an identifier ending in
    // `import` is not one; header units, header imports and partitions are not module names.
    static const std::regex re(R"((?:^|[\s;{}])import\s+([A-Za-z_][A-Za-z0-9_]*(?:\.[A-Za-z_][A-Za-z0-9_]*)*)\s*(?=;))");
    std::vector<ImportSite> sites;
    ScanState state;
    for (std::size_t l = 0; l < lines.size(); ++l) {
        const auto code = mask_for_imports(lines[l], state);
        for (auto it = std::sregex_iterator(code.begin(), code.end(), re); it != std::sregex_iterator(); ++it) {
            const auto& m = *it;
            const auto name = m[1].str();
            const auto whole = m[0].str();
            const auto start = static_cast<std::size_t>(m.position(0)) + whole.rfind(name);
            sites.push_back({name, static_cast<int>(l), u16(code, start), u16(code, start + name.size())});
        }
    }
    return sites;
}

namespace {

struct RoleSite {
    std::string text;
    bool literal = false;
    std::size_t line = 0, start = 0, end = 0;
};

struct ActionBlock {
    std::size_t line = 0, actionStart = 0, actionEnd = 0;
    std::optional<RoleSite> role;
    bool hasRoleField = false, hasOutputDir = false;
};

constexpr std::size_t kActionWindow = 40;

// The literal written right after `after` (only spaces between), if any.
const StringLiteral* literal_after(const MaskedLine& m, std::size_t after) {
    for (const auto& s : m.strings)
        if (s.start >= after && trim(std::string_view(m.code).substr(after, s.start - after)).empty()) return &s;
    return nullptr;
}

std::vector<ActionBlock> find_action_blocks(const std::vector<MaskedLine>& masked) {
    static const std::regex action(R"(\bmcpp::action\b)"), submit(R"(\.submit\s*\()"), outputDir(R"(\.output_dir\s*\()"),
        roleConstant(R"(mcpp::roles::([A-Za-z_]\w*))"), roleAssign(R"(\brole\s*=)");
    std::vector<ActionBlock> blocks;
    for (std::size_t i = 0; i < masked.size(); ++i) {
        const auto token = search(masked[i].code, action);
        if (!token) continue;
        const auto limit = std::min(masked.size() - 1, i + kActionWindow);
        auto end = limit;
        for (auto j = i; j <= limit; ++j)
            if (std::regex_search(masked[j].code, submit)) {
                end = j;
                break;
            }
        ActionBlock block;
        block.line = i;
        block.actionStart = static_cast<std::size_t>(token->position(0));
        block.actionEnd = block.actionStart + static_cast<std::size_t>(token->length(0));
        for (auto j = i; j <= end; ++j) {
            const auto& [code, strings] = masked[j];
            if (!block.hasOutputDir && std::regex_search(code, outputDir)) block.hasOutputDir = true;
            if (const auto c = search(code, roleConstant); c && !block.role) {
                block.hasRoleField = true;
                const auto start = static_cast<std::size_t>(c->position(0));
                block.role = RoleSite{(*c)[1].str(), false, j, start, start + static_cast<std::size_t>(c->length(0))};
            }
            if (!block.role) {
                if (const auto a = search(code, roleAssign)) {
                    block.hasRoleField = true;
                    const auto after = static_cast<std::size_t>(a->position(0) + a->length(0));
                    if (const auto* lit = literal_after(masked[j], after))
                        block.role = RoleSite{lit->value, true, j, lit->start, lit->end};
                }
            }
        }
        blocks.push_back(std::move(block));
    }
    return blocks;
}

// The narrowest span of `needle` in a literal, else the literal.
std::pair<std::size_t, std::size_t> literal_span(const StringLiteral& literal, std::string_view needle) {
    if (!needle.empty())
        if (const auto at = literal.raw.find(needle); at != std::string::npos)
            return {literal.start + at, literal.start + at + needle.size()};
    return {literal.start, literal.end};
}

std::optional<std::string> group1(const std::string& s, const std::regex& re) {
    if (const auto m = search(s, re)) return (*m)[1].str();
    return std::nullopt;
}

}  // namespace

std::vector<Diagnostic> analyse(const std::vector<std::string>& lines, std::string_view severity, bool unknownSymbols) {
    std::vector<MaskedLine> masked;
    ScanState state;
    for (const auto& l : lines) masked.push_back(mask_for_analysis(l, state));

    std::vector<Diagnostic> found;
    auto report = [&](std::string_view rule, std::string message, std::size_t line, std::size_t start, std::size_t end) {
        const std::string_view text = line < lines.size() ? std::string_view(lines[line]) : std::string_view();
        found.push_back({std::format("mcpp.buildscript.{}", rule), std::move(message), std::string(severity), static_cast<int>(line) + 1,
                         u16(text, start) + 1, u16(text, end) + 1});
    };
    const auto roles = roles_list();

    // 1. A `mcpp::<name>` the engine does not have, and no import of the file brings in.
    if (unknownSymbols) {
        static const std::regex imported(R"(\bimport\s+mcpp\.([A-Za-z_][A-Za-z0-9_]*))"), symbol(R"(\bmcpp::([A-Za-z_][A-Za-z0-9_]*))");
        std::set<std::string> namespaces;
        for (const auto& m : masked)
            for (auto it = std::sregex_iterator(m.code.begin(), m.code.end(), imported); it != std::sregex_iterator(); ++it)
                namespaces.insert((*it)[1].str());
        for (std::size_t i = 0; i < masked.size(); ++i)
            for (auto it = std::sregex_iterator(masked[i].code.begin(), masked[i].code.end(), symbol); it != std::sregex_iterator(); ++it) {
                const auto name = (*it)[1].str();
                if (known_symbols().contains(name) || namespaces.contains(name)) continue;
                const auto at = static_cast<std::size_t>(it->position(0));
                report("unknownSymbol",
                       std::format("unknown mcpp::{}: not a directive, action role or typed-API name in the mcpp build-script snapshot "
                                   "(SPEC-007 §2; docs/30-build-mcpp.md)", name),
                       i, at, at + static_cast<std::size_t>(it->length(0)));
            }
    }

    // 2. `role = "<role>"` where the engine's constant exists (R3.6).
    const auto blocks = find_action_blocks(masked);
    static const std::regex roleAssign(R"(\brole\s*=)");
    for (std::size_t i = 0; i < masked.size(); ++i)
        for (auto it = std::sregex_iterator(masked[i].code.begin(), masked[i].code.end(), roleAssign); it != std::sregex_iterator(); ++it) {
            const auto after = static_cast<std::size_t>(it->position(0) + it->length(0));
            const auto* lit = literal_after(masked[i], after);
            if (!lit) continue;
            if (is_role(lit->value))
                report("roleLiteral",
                       std::format("role is spelled as the string \"{0}\"; use mcpp::roles::{0} so an engine that does not know the role "
                                   "refuses to compile it (SPEC-007 R3.6)", lit->value),
                       i, lit->start, lit->end);
            else
                report("actionNeedsRole", std::format("unknown action role \"{}\"; the five roles are {} (SPEC-007 R3.6)", lit->value, roles),
                       i, lit->start, lit->end);
        }

    // 3 + 7, the typed form: a block with no role; a prepare block with no output_dir.
    for (const auto& b : blocks) {
        if (!b.hasRoleField) {
            report("actionNeedsRole",
                   std::format("this mcpp::action declares no role; name one of {} (mcpp::roles::…) — a missing role is read as \"source\" "
                               "(SPEC-007 R3.2, R3.6)", roles),
                   b.line, b.actionStart, b.actionEnd);
            continue;
        }
        if (!b.role || b.role->text != "prepare" || b.hasOutputDir) continue;
        report("prepareNeedsOutputDir",
               "a prepare action must declare the directory its command fills with output_dir(…): the engine refuses an action "
               "without one (SPEC-007 R3.3)",
               b.role->line, b.role->start, b.role->end);
    }

    // 3 + 6 + 7, the raw `mcpp:action=` payload (the frozen printf surface; a role as a string is
    // allowed there). A payload that does not parse is skipped.
    static const std::regex shellEnv(R"((?:^|\s)([A-Za-z_][A-Za-z0-9_]*=\S*))"), shellCd(R"((?:^|\s)(cd\s+\S+\s*&&))");
    constexpr std::string_view importAction = "mcpp:action=";
    for (std::size_t i = 0; i < masked.size(); ++i)
        for (const auto& lit : masked[i].strings) {
            const auto at = lit.value.find(importAction);
            if (at == std::string::npos) continue;
            const auto payload = Json::parse(trim(std::string_view(lit.value).substr(at + importAction.size())), nullptr, false);
            if (payload.is_discarded() || !payload.is_object()) continue;
            const auto role = payload.find("role");
            if (role == payload.end() || !role->is_string()) {
                report("actionNeedsRole", std::format("this mcpp:action payload declares no role; the five roles are {} (SPEC-007 R3.6)", roles),
                       i, lit.start, lit.end);
            } else if (!is_role(role->get<std::string>())) {
                report("actionNeedsRole",
                       std::format("unknown action role \"{}\"; the five roles are {} (SPEC-007 R3.6)", role->get<std::string>(), roles), i,
                       lit.start, lit.end);
            } else if (role->get<std::string>() == "prepare" && !(payload.contains("output_dir") && payload["output_dir"].is_string())) {
                report("prepareNeedsOutputDir", "a prepare action must declare output_dir: the engine refuses an action without one (SPEC-007 R3.3)",
                       i, lit.start, lit.end);
            }
            if (payload.contains("command") && payload["command"].is_array())
                for (const auto& entry : payload["command"]) {
                    if (!entry.is_string()) continue;
                    const auto e = entry.get<std::string>();
                    auto needle = group1(e, shellEnv);
                    if (!needle) needle = group1(e, shellCd);
                    if (needle) {
                        report("shellSyntaxInAction",
                               std::format("the command spells shell syntax (\"{}\"); declare it with env(…) and cwd(…) instead (SPEC-007 R3.8)",
                                           *needle),
                               i, lit.start, lit.end);
                        break;
                    }
                }
        }

    // 4 + 5: flags R2.1 and R4.4 send to directives.
    static const std::regex cxxCall(R"((?:^|[^\w])(?:cxxflag|cflag)\s*\()"), linkCall(R"((?:^|[^\w])link_flag\s*\()"),
        rawPath(R"((?:^|\s)(-[IL]\S*))");
    constexpr std::string_view rpath = "-Wl,-rpath";
    for (std::size_t i = 0; i < masked.size(); ++i) {
        const auto& [code, strings] = masked[i];
        const bool isCxxFlagCall = std::regex_search(code, cxxCall);
        const bool isLinkFlagCall = std::regex_search(code, linkCall);
        for (const auto& lit : strings) {
            const auto& v = lit.value;
            const bool isLinkFlag = isLinkFlagCall || v.find("mcpp:link-flag=") != std::string::npos;
            const bool isCxxFlag = isCxxFlagCall || v.find("mcpp:cxxflag=") != std::string::npos || v.find("mcpp:cflag=") != std::string::npos;
            if (isLinkFlag && v.find(rpath) != std::string::npos) {
                const auto [s, e] = literal_span(lit, rpath);
                report("rpathInLinkFlag",
                       "a run path must not be spelled in link_flag; declare the directory with mcpp::runtime_search_dir(…) (SPEC-007 R4.4)",
                       i, s, e);
            }
            if (isLinkFlag || isCxxFlag)
                if (const auto token = group1(v, rawPath)) {
                    const auto [s, e] = literal_span(lit, *token);
                    report("rawPathFlag",
                           std::format("spelling \"{}\" in a raw flag bypasses the engine's rendering; use mcpp::include_dir / "
                                       "mcpp::include_dir_after / mcpp::link_search (SPEC-007 R2.1)", *token),
                           i, s, e);
                }
        }
    }

    // 6, the typed form: the strings of `.arg()` / `.command()` in an action block.
    static const std::regex submit(R"(\.submit\s*\()"), argCall(R"(\.(?:arg|command)\s*\()");
    std::set<std::size_t> actionLines;
    for (const auto& b : blocks)
        for (auto i = b.line; i < masked.size(); ++i) {
            actionLines.insert(i);
            if (std::regex_search(masked[i].code, submit)) break;
        }
    for (std::size_t i = 0; i < masked.size(); ++i) {
        if (!actionLines.contains(i) || !std::regex_search(masked[i].code, argCall)) continue;
        for (const auto& lit : masked[i].strings) {
            auto needle = group1(lit.value, shellEnv);
            if (!needle) needle = group1(lit.value, shellCd);
            if (!needle) continue;
            const auto [s, e] = literal_span(lit, *needle);
            report("shellSyntaxInAction",
                   std::format("the command spells shell syntax (\"{}\"); declare the environment and working directory with env(…) and "
                               "cwd(…) instead (SPEC-007 R3.8)", *needle),
                   i, s, e);
        }
    }

    std::set<std::string> seen;
    std::vector<Diagnostic> unique;
    for (auto& d : found)
        if (seen.insert(std::format("{}:{}:{}:{}", d.code, d.line, d.start, d.end)).second) unique.push_back(std::move(d));
    std::ranges::stable_sort(unique, [](const Diagnostic& a, const Diagnostic& b) {
        if (a.line != b.line) return a.line < b.line;
        if (a.start != b.start) return a.start < b.start;
        return a.code < b.code;
    });
    return unique;
}

std::vector<Snippet> snippets() {
    const auto choice = roles_list(",");
    std::vector<Snippet> out{
        {"action", "action ... (typed action block)", "mcpp::action with id, role, description, arg/input/output and submit",
         std::format("The five roles (SPEC-007 R3.6): {}.", roles_list()),
         std::format("mcpp::action ${{1:handle}};\n${{1:handle}}.id = \"${{2:id}}\";\n${{1:handle}}.role = mcpp::roles::${{3|{}|}};\n"
                     "${{1:handle}}.description = \"${{4:description}}\";\n${{1:handle}}.arg(\"${{5:argument}}\")\n    .input(\"${{6:input}}\")\n"
                     "    .output(\"${{7:output}}\")\n    .submit();",
                     choice)},
        {"action", "action ... (prepare block)", "mcpp::action for a prepare role; output_dir is required (SPEC-007 R3.3)",
         "A prepare action must declare output_dir; the engine refuses one without it.",
         "mcpp::action ${1:handle};\n${1:handle}.id = \"${2:id}\";\n${1:handle}.role = mcpp::roles::prepare;\n"
         "${1:handle}.output_dir(\"${3:dir}\");\n${1:handle}.command(\"${4:command}\").submit();"},
    };
    // `mcpp::<typed>("…")` for every directive but `action`: one placeholder, since no arity is known.
    std::set<std::string> seen;
    for (const auto& d : api()["directives"]) {
        const auto name = underscored(str(d, "wire"));
        if (name == "action" || !seen.insert(name).second) continue;
        std::vector<std::string> documentation;
        std::vector<std::string> rules;
        if (d.contains("rules") && d["rules"].is_array())
            for (const auto& r : d["rules"]) rules.push_back(r.get<std::string>());
        if (!rules.empty()) documentation.push_back("SPEC-007 " + joined(rules, ", "));
        if (d.contains("docsUrl") && d["docsUrl"].is_string())
            documentation.push_back(std::format("[docs/30 — build.mcpp]({})", d["docsUrl"].get<std::string>()));
        Snippet s{name, name + "(...)", std::format("mcpp:{}= · slot {}", str(d, "wire"), str(d, "slot")), std::nullopt,
                  name + "(\"${1:value}\")"};
        if (!documentation.empty()) s.documentation = joined(documentation, " · ");
        out.push_back(std::move(s));
    }
    return out;
}

std::optional<std::vector<Item>> completion(std::string_view line, int character, const Switches& sw) {
    const std::string before(line.substr(0, text::byte_offset(line, character)));
    static const std::regex afterImport(R"(\b(?:export\s+)?import\s+([A-Za-z0-9_.]*)$)"), afterScope(R"(mcpp::([A-Za-z_][A-Za-z0-9_]*)?$)");
    if (const auto m = search(before, afterImport)) {
        // Only the list is optional; recognition (hover, the no-diagnostic rule) is not. C++ Modules
        // completes module names from what the build sees, so it answers instead.
        if (!sw.knownModules || sw.served) return std::nullopt;
        std::vector<Item> items;
        const auto typed = (*m)[1].str();
        for (const auto& k : known_modules())
            if (!k.name.ends_with(".*") && k.name.starts_with(typed))
                items.push_back({k.name, "module", "mcpp build-script module", k.description, std::nullopt, std::nullopt});
        return items;
    }
    if (const auto m = search(before, afterScope)) {
        const auto typed = (*m)[1].matched ? (*m)[1].str() : std::string();
        std::vector<Item> items;
        std::set<std::string> seen;
        for (const auto& d : api()["directives"]) {
            const auto name = underscored(str(d, "wire"));
            if (!name.starts_with(typed) || !seen.insert(name).second) continue;
            items.push_back({name, "function", "mcpp build-script API", std::nullopt, std::nullopt, std::nullopt});
        }
        if (sw.snippets)
            for (auto& s : snippets())
                if (s.filterText.starts_with(typed)) items.push_back({s.label, "snippet", s.detail, s.documentation, s.filterText, s.insertText});
        return items;
    }
    return std::nullopt;
}

std::optional<std::string> hover(const std::vector<std::string>& lines, int line, int character, bool served) {
    // An import's hover is C++ Modules' while it serves the file.
    if (!served)
        for (const auto& site : scan_imports(lines))
            if (site.line == line && character >= site.start && character <= site.end)
                if (const auto info = known_module(site.module)) return std::format("**{}**\n\n{}", info->name, info->description);
    if (line < 0 || line >= static_cast<int>(lines.size())) return std::nullopt;
    const auto& text = lines[line];
    static const std::regex symbol(R"(mcpp::([A-Za-z_][A-Za-z0-9_]*))");
    std::optional<std::string> name;
    for (auto it = std::sregex_iterator(text.begin(), text.end(), symbol); it != std::sregex_iterator(); ++it) {
        const auto start = u16(text, static_cast<std::size_t>(it->position(0)));
        const auto end = u16(text, static_cast<std::size_t>(it->position(0) + it->length(0)));
        if (character >= start && character <= end) {
            name = (*it)[1].str();
            break;
        }
    }
    if (!name) return std::nullopt;
    auto wire = *name;
    std::ranges::replace(wire, '_', '-');
    const auto* entry = directive(wire);
    if (!entry) return std::nullopt;
    std::vector<std::string> details{
        std::format("**mcpp::{}**", *name),
        std::format("wire `mcpp:{}=` · slot `{}` · scope `{}` · since protocol {}", str(*entry, "wire"), str(*entry, "slot"),
                    str(*entry, "scope"), entry->contains("since") ? (*entry)["since"].dump() : std::string("undefined"))};
    std::vector<std::string> rules;
    if (entry->contains("rules") && (*entry)["rules"].is_array())
        for (const auto& r : (*entry)["rules"]) rules.push_back(r.get<std::string>());
    if (!rules.empty()) details.push_back("SPEC-007: " + joined(rules, ", "));
    if (entry->contains("docsUrl") && (*entry)["docsUrl"].is_string())
        details.push_back(std::format("[docs/30 — build.mcpp]({})", (*entry)["docsUrl"].get<std::string>()));
    return joined(details, "\n\n");
}

Json api_source() {
    return Json{{"version", str(api(), "sourceVersion")}, {"commit", str(api(), "sourceCommit")}};
}

}  // namespace mcppide::buildscript
