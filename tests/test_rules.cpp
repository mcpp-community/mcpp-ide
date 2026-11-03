// The rules layer answers what mcpp-vscode 0.9's TypeScript answered (design 2026-10-30 part 2 §8):
// over each file of tests/rules/corpus, at every position of every line (and past them), the
// scanner's structure and cursor contexts, the completions, hovers, jumps, version contexts and
// diagnostics equal tests/rules/golden, which that implementation wrote (with the documents of its
// parser tests, golden/snippets.json). A difference is printed with its file and position.
#include "support.hpp"
import std;
import nlohmann.json;
import mcppide.text;
import mcppide.manifest;
import mcppide.buildscript;
import mcppide.toml;

using namespace mcppide;
using Json = nlohmann::json;

namespace {

int failures = 0;

void expect(const Json& got, const Json& want, const std::string& where) {
    if (got == want) return;
    if (++failures > 25) return;
    // Two lists: the first entry that differs.
    if (got.is_array() && want.is_array())
        for (std::size_t i = 0; i < std::max(got.size(), want.size()); ++i)
            if (i >= got.size() || i >= want.size() || got[i] != want[i]) {
                std::println(std::cerr, "MISMATCH {} [{} of {}/{}]\n  got:  {}\n  want: {}", where, i, got.size(), want.size(),
                             i < got.size() ? got[i].dump() : "-", i < want.size() ? want[i].dump() : "-");
                return;
            }
    std::println(std::cerr, "MISMATCH {}\n  got:  {}\n  want: {}", where, got.dump().substr(0, 1500), want.dump().substr(0, 1500));
}

Json opt(const std::optional<std::string>& s) { return s ? Json(*s) : Json(); }

void put(Json& j, std::string_view key, const std::optional<std::string>& v) {
    if (v) j[std::string(key)] = *v;
}

// ── the scanner, in the shapes mcpp-vscode 0.9's parser answered ──

std::vector<std::string> stripped(std::vector<std::string> lines) {
    for (auto& l : lines)
        if (!l.empty() && l.back() == '\r') l.pop_back();
    if (lines.empty()) lines.emplace_back();
    return lines;
}

int col16(const std::vector<std::string>& lines, int line, int byte) {
    if (line < 0 || line >= static_cast<int>(lines.size())) return byte;
    return text::utf16_column(lines[line], static_cast<std::size_t>(byte));
}

Json range_json(const std::vector<std::string>& lines, const toml::Range& r) {
    return Json{{"startLine", r.startLine}, {"startCharacter", col16(lines, r.startLine, r.startCol)},
                {"endLine", r.endLine}, {"endCharacter", col16(lines, r.endLine, r.endCol)}};
}

Json segments_json(const std::vector<std::string>& lines, const std::vector<toml::KeySegment>& segments) {
    Json out = Json::array();
    for (const auto& s : segments) out.push_back(Json{{"name", s.name}, {"quoted", s.quoted}, {"range", range_json(lines, s.range)}});
    return out;
}

Json key_value_json(const std::vector<std::string>& lines, const toml::KeyValue& kv);

Json value_json(const std::vector<std::string>& lines, const toml::Value& v) {
    Json j{{"kind", toml::name_of(v.kind)}, {"range", range_json(lines, v.range)}, {"open", v.open}};
    if (v.text) j["text"] = *v.text;
    if (v.kind == toml::ValueKind::string) {
        j["quote"] = std::string(1, v.quote);
        j["multiline"] = v.multiline;
        if (v.contentRange) j["contentRange"] = range_json(lines, *v.contentRange);
    }
    if (v.kind == toml::ValueKind::array) {
        j["elements"] = Json::array();
        for (const auto& e : v.elements) j["elements"].push_back(value_json(lines, e));
    }
    if (v.kind == toml::ValueKind::inlineTable) {
        j["entries"] = Json::array();
        for (const auto& e : v.entries) j["entries"].push_back(key_value_json(lines, e));
    }
    return j;
}

Json key_value_json(const std::vector<std::string>& lines, const toml::KeyValue& kv) {
    Json j{{"type", "keyValue"}, {"keyPath", segments_json(lines, kv.keyPath)}, {"range", range_json(lines, kv.range)}};
    if (kv.value) j["value"] = value_json(lines, *kv.value);
    return j;
}

Json document_json(const std::vector<std::string>& lines) {
    Json nodes = Json::array();
    for (const auto& node : toml::parse(lines).nodes) {
        if (const auto* s = std::get_if<toml::Section>(&node))
            nodes.push_back(Json{{"type", "section"}, {"segments", segments_json(lines, s->segments)}, {"isArray", s->isArray},
                                 {"open", s->open}, {"range", range_json(lines, s->range)}, {"line", s->line}});
        else
            nodes.push_back(key_value_json(lines, std::get<toml::KeyValue>(node)));
    }
    return Json{{"nodes", nodes}};
}

Json context_json(const std::vector<std::string>& lines, int line, int character) {
    const auto c = toml::context_at(lines, line, character);
    const int cl = std::clamp(line, 0, static_cast<int>(lines.size()) - 1);
    const Json replace{{"startCharacter", col16(lines, cl, c.replace.start)}, {"endCharacter", col16(lines, cl, c.replace.end)}};
    auto section = [&] {
        switch (c.section.kind) {
            case toml::Resolution::Kind::top: return Json{{"kind", "top"}};
            case toml::Resolution::Kind::unknown: return Json{{"kind", "unknown"}, {"segments", c.section.segments}};
            case toml::Resolution::Kind::known: return Json{{"kind", "known"}, {"group", c.section.group}};
        }
        return Json();
    };
    switch (c.kind) {
        case toml::Context::Kind::header:
            return Json{{"kind", "section-header"}, {"segments", c.segments}, {"isArray", c.isArray}, {"replaceRange", replace}};
        case toml::Context::Kind::key:
            return Json{{"kind", "key"}, {"section", section()}, {"containerPath", c.containerPath}, {"keyPrefix", c.keyPrefix},
                        {"replaceRange", replace}};
        case toml::Context::Kind::value: {
            Json j{{"kind", "value"}, {"section", section()}, {"keyPath", c.keyPath}, {"insideString", c.insideString},
                   {"stringOpen", c.stringOpen}, {"replaceRange", replace}};
            if (c.valueKind) j["valueKind"] = toml::name_of(*c.valueKind);
            return j;
        }
    }
    return Json();
}

void check_scanner(const std::vector<std::string>& given, const Json& golden, const std::string& name) {
    const auto lines = stripped(given);
    expect(document_json(given), golden["document"], name + " document");
    const int n = static_cast<int>(given.size());
    for (int l = 0; l <= n; ++l) {
        const int len = l < n ? text::utf16_length(lines[std::min(l, static_cast<int>(lines.size()) - 1)]) : 0;
        for (int c = 0; c <= len + 2; ++c) {
            const auto at = std::format("{}:{}", l, c);
            expect(context_json(lines, l, c), golden["contexts"][golden["context"][at].get<std::size_t>()], std::format("{} {} context", name, at));
        }
    }
}

Json toml_diagnostics(const std::vector<manifest::Diagnostic>& ds) {
    Json out = Json::array();
    for (const auto& d : ds) {
        Json j{{"code", d.code}, {"message", d.message}, {"severity", d.severity}, {"line", d.line}, {"startCharacter", d.start},
               {"endCharacter", d.end}};
        put(j, "relatedKey", d.relatedKey);
        out.push_back(j);
    }
    return out;
}

void check_manifest(const std::filesystem::path& file, const Json& golden) {
    const auto name = file.filename().string();
    const auto lines = text::lines_of(testsupport::read(file));
    check_scanner(lines, golden, name);
    manifest::DiagnosticSettings quiet;
    quiet.syntax = "info";
    quiet.unknownSection = "off";
    quiet.unknownKey = "info";
    quiet.planeSeparation = "off";
    quiet.legacyKeys = "warning";
    quiet.mcppFloor = "warning";
    quiet.arrayTable = "off";
    expect(toml_diagnostics(manifest::analyse(lines, {})), golden["diagnostics"]["default"], name + " diagnostics default");
    expect(toml_diagnostics(manifest::analyse(lines, quiet)), golden["diagnostics"]["quiet"], name + " diagnostics quiet");
    const std::map<std::string, int, std::less<>> manifests{{"../lib", 2}, {"libs/core", 0}};
    auto resolve = [&](std::string_view rel) -> std::optional<int> {
        auto it = manifests.find(rel);
        return it == manifests.end() ? std::nullopt : std::optional(it->second);
    };
    const int n = static_cast<int>(lines.size());
    for (int l = 0; l <= n + 1; ++l) {
        const int len = l < n ? text::utf16_length(lines[l]) : 0;
        for (int c = 0; c <= len + 2; ++c) {
            const auto at = std::format("{}:{}", l, c);
            const auto where = std::format("{} {}", name, at);
            if (l < n) {
                const std::vector<std::string> upTo(lines.begin(), lines.begin() + l + 1);
                const auto items = manifest::completions(upTo, l, c);
                Json list = Json::array();
                for (const auto& s : items) {
                    Json j{{"label", s.label}, {"kind", s.kind}, {"detail", s.detail}};
                    put(j, "documentation", s.documentation);
                    put(j, "insertSnippet", s.insertSnippet);
                    list.push_back(j);
                }
                const auto& want = golden["completion"][at];
                expect(list, golden["lists"][want[0].get<std::size_t>()], where + " completion");
                if (!items.empty()) expect(Json{items[0].start, items[0].end}, Json{want[1], want[2]}, where + " completion range");
                const auto v = manifest::dependency_version_context(upTo, l, c);
                const Json wantV = golden.contains("versionContext") && golden["versionContext"].contains(at) ? golden["versionContext"][at] : Json();
                expect(v ? Json{{"name", v->name}, {"insideString", v->insideString},
                                {"range", {{"startCharacter", v->start}, {"endCharacter", v->end}}}}
                         : Json(),
                       wantV, where + " version context");
            }
            const auto h = manifest::hover_at(lines, l, c);
            Json hj;
            if (h) {
                hj = Json{{"title", h->title}, {"body", h->body}};
                put(hj, "documentation", h->documentation);
            }
            expect(hj, golden["hover"].contains(at) ? golden["hovers"][golden["hover"][at].get<std::size_t>()] : Json(), where + " hover");
            const auto d = manifest::definition_at(lines, l, c, resolve);
            expect(d ? Json{{"line", d->line}, {"startCharacter", d->start}, {"endCharacter", d->end}} : Json(),
                   golden["definition"].contains(at) ? golden["definition"][at] : Json(), where + " definition");
        }
    }
}

Json bs_diagnostics(const std::vector<buildscript::Diagnostic>& ds) {
    Json out = Json::array();
    for (const auto& d : ds)
        out.push_back(Json{{"code", d.code}, {"message", d.message}, {"severity", d.severity}, {"line", d.line}, {"startCharacter", d.start},
                           {"endCharacter", d.end}});
    return out;
}

void check_buildscript(const std::filesystem::path& file, const Json& golden) {
    const auto name = file.filename().string();
    const auto lines = text::lines_of(testsupport::read(file));
    expect(bs_diagnostics(buildscript::analyse(lines, "warning")), golden["diagnostics"]["warning"], name + " diagnostics");
    expect(bs_diagnostics(buildscript::analyse(lines, "info", false)), golden["diagnostics"]["served"], name + " diagnostics served");
    Json imports = Json::array();
    for (const auto& s : buildscript::scan_imports(lines))
        imports.push_back(Json{{"module", s.module}, {"line", s.line}, {"startCharacter", s.start}, {"endCharacter", s.end}});
    expect(imports, golden["imports"], name + " imports");
    for (int l = 0; l < static_cast<int>(lines.size()); ++l)
        for (int c = 0; c <= text::utf16_length(lines[l]); ++c) {
            const auto at = std::format("{}:{}", l, c);
            const auto where = std::format("{} {}", name, at);
            const auto items = buildscript::completion(lines[l], c);
            Json got;
            if (items) {
                got = Json::array();
                for (const auto& i : *items) {
                    Json j{{"label", i.label}, {"kind", i.kind}, {"detail", i.detail}};
                    put(j, "documentation", i.documentation);
                    put(j, "filterText", i.filterText);
                    put(j, "insertText", i.insertText);
                    got.push_back(j);
                }
            }
            expect(got, golden["completion"].contains(at) ? golden["lists"][golden["completion"][at].get<std::size_t>()] : Json(),
                   where + " completion");
            const auto h = buildscript::hover(lines, l, c);
            expect(opt(h), golden["hover"].contains(at) ? golden["hovers"][golden["hover"][at].get<std::size_t>()] : Json(), where + " hover");
        }
}

void check_catalogue(const Json& golden) {
    Json snippets = Json::array();
    for (const auto& s : buildscript::snippets()) {
        Json j{{"filterText", s.filterText}, {"label", s.label}, {"detail", s.detail}, {"insertText", s.insertText}};
        put(j, "documentation", s.documentation);
        snippets.push_back(j);
    }
    expect(snippets, golden["snippets"], "catalogue snippets");
    Json modules = Json::array();
    for (const auto& m : buildscript::known_modules())
        modules.push_back(Json{{"name", m.name}, {"description", m.description}, {"docsUrl", m.docsUrl}});
    expect(modules, golden["modules"], "catalogue modules");
    for (const auto& pair : golden["known"]) {
        const auto name = pair[0].get<std::string>();
        const auto m = buildscript::known_module(name);
        expect(m ? Json(m->name) : Json(), pair[1], "known module " + name);
    }
}

}  // namespace

int main() {
    const auto root = testsupport::repo() / "tests" / "rules";
    std::vector<std::filesystem::path> files;
    for (const auto& e : std::filesystem::directory_iterator(root / "corpus")) files.push_back(e.path());
    std::ranges::sort(files);
    CHECK(!files.empty());
    for (const auto& f : files) {
        const auto golden = Json::parse(testsupport::read(root / "golden" / (f.filename().string() + ".json")));
        if (f.extension() == ".toml") check_manifest(f, golden);
        else check_buildscript(f, golden);
    }
    check_catalogue(Json::parse(testsupport::read(root / "golden" / "catalogue.json")));
    // The documents mcpp-vscode 0.9's parser tests read: their structure and every cursor's context.
    int k = 0;
    for (const auto& snippet : Json::parse(testsupport::read(root / "golden" / "snippets.json")))
        check_scanner(snippet["lines"].get<std::vector<std::string>>(), snippet, std::format("snippet {}", k++));

    // Past the goldens: the catalogue's versions, newest first, a package's own only.
    const Json envelope{{"data", {{"packages", Json::array({
        Json{{"name", "zlib"}, {"namespace", "compat"}, {"description", " A compression library "}, {"versions", {"1.2.13", "1.3.1", "1.3.0"}}},
        Json{{"name", "zlibx"}, {"namespace", "x"}, {"versions", {"9.0.0"}}},
    })}}}};
    const auto versions = manifest::catalogue_versions(envelope, "zlib");
    CHECK(versions.size() == 3 && versions[0].version == "1.3.1" && versions[2].version == "1.2.13");
    CHECK(versions[0].summary && *versions[0].summary == "A compression library");
    const auto lines = text::lines_of("[dependencies]\nzlib = \"1.\n");
    const auto at = manifest::dependency_version_context(lines, 1, 9);
    CHECK(at && at->name == "zlib" && at->insideString);
    const auto suggestions = manifest::version_suggestions(*at, versions);
    CHECK(suggestions.size() == 3 && suggestions[0].insertSnippet == "1.3.1" && suggestions[0].kind == "version");
    // A suggestion's detail in the user's language.
    const auto zh = manifest::completions(text::lines_of("[dependencies]\n"), 1, 0, "zh-cn");
    CHECK(!zh.empty() && zh[0].detail != "SemVer version dependency");

    if (failures) std::println(std::cerr, "{} mismatch(es)", failures);
    CHECK(failures == 0);
    std::println("rules: the goldens hold");
    return 0;
}
