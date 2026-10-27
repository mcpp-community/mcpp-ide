// mcpp-ide-dev: the repository's own tools, as one program of this package (no script runtime is
// needed beside mcpp).
//
//   mcpp-ide-dev capture [--mcpp PATH] [--out DIR]
//       The golden fixtures under protocol/fixtures, from a real mcpp: the envelopes, streams and
//       records the backend reads. The mock mcpp and both adapters' tests replay them, so a change in
//       mcpp's contract shows up as a diff here first.
//   mcpp-ide-dev gen vscode --out DIR [--stage] [--backends DIR]
//       The VS Code extension's declarations from spec/: package.json (vscode/package.json with the
//       generated commands, keybindings, menus and configuration), package.nls.json and
//       package.nls.zh-cn.json. With --stage, also the files the extension ships (vscode/out, media,
//       syntaxes, the webui, README, CHANGELOG, LICENSE, and bin/<platform>/ from --backends): the
//       directory `vsce package` packs.
//   mcpp-ide-dev gen jetbrains --out DIR
//       The CLion plugin's declarations from spec/: META-INF/mcpp-actions.xml (included by
//       plugin.xml) and the message bundles messages/McppClionBundle[_zh_CN].properties.
//   mcpp-ide-dev check-i18n
//       Every runtime string the backend and the adapters pass to t(...) has a Chinese entry in
//       spec/i18n/runtime.zh-cn.json; every editor string a Chinese one in manifest.zh-cn.json.
//
// Nothing generated is written into the repository: the outputs go where the adapters' builds put
// them (vscode/dist, jetbrains/build).
import std;
import nlohmann.json;
import mcpplibs.os;

namespace {

using Json = nlohmann::json;
namespace fs = std::filesystem;

std::string slurp(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), {});
}

void spit(const fs::path& p, std::string_view text) {
    fs::create_directories(p.parent_path());
    if (slurp(p) == text) return;
    std::ofstream(p, std::ios::binary) << text;
}

Json read_json(const fs::path& p) {
    auto doc = Json::parse(slurp(p), nullptr, false);
    if (doc.is_discarded()) throw std::runtime_error(std::format("{}: not JSON", p.generic_string()));
    return doc;
}

std::string replace_all(std::string s, std::string_view from, std::string_view to) {
    if (from.empty()) return s;
    for (auto pos = s.find(from); pos != std::string::npos; pos = s.find(from, pos + to.size())) s.replace(pos, from.size(), to);
    return s;
}

// The repository: the nearest directory above the working one whose mcpp.toml is mcpp-ide's.
fs::path repo() {
    std::error_code ec;
    for (auto d = fs::current_path(ec); !d.empty(); d = d.parent_path()) {
        if (slurp(d / "mcpp.toml").find("name        = \"mcpp-ide\"") != std::string::npos) return d;
        if (d == d.parent_path()) break;
    }
    throw std::runtime_error("run mcpp-ide-dev inside the mcpp-ide repository");
}

std::optional<std::string> option(std::span<const std::string> args, std::string_view name) {
    for (std::size_t i = 0; i + 1 < args.size(); ++i) if (args[i] == name) return args[i + 1];
    return std::nullopt;
}
bool flag(std::span<const std::string> args, std::string_view name) {
    return std::ranges::find(args, std::string(name)) != args.end();
}

// ── capture ─────────────────────────────────────────────────────────────────

struct Ran {
    int code = -1;
    std::string out, err;
};

Ran run(const fs::path& program, std::vector<std::string> args, const fs::path& cwd) {
    mcpplibs::os::process::spawn_options o;
    o.program = program;
    o.args = std::move(args);
    o.cwd = cwd;
    o.in = mcpplibs::os::process::stdio::null;
    o.env = {{"NO_COLOR", "1"}, {"MCPP_CLIENT", "mcpp-ide-fixtures"}};
    auto r = mcpplibs::os::process::run(o);
    if (!r) return Ran{-1, {}, r.error().message()};
    return Ran{r->status.shell_code(), std::move(r->out), std::move(r->err)};
}

int capture(std::span<const std::string> args) {
    const auto root = repo();
    fs::path mcpp = option(args, "--mcpp").value_or("");
    if (mcpp.empty()) {
        auto found = mcpplibs::os::env::find_executable("mcpp");
        if (!found) throw std::runtime_error("no mcpp on PATH; name one with --mcpp");
        mcpp = *found;
    }
    const fs::path out = option(args, "--out").value_or((root / "protocol" / "fixtures").string());
    fs::create_directories(out);
    const auto tmp = mcpplibs::os::dirs::temp() / std::format("mcppide-capture-{}", mcpplibs::os::process::self()->id);
    fs::remove_all(tmp);
    fs::create_directories(tmp);
    const auto app = tmp / "app";
    const auto home = mcpplibs::os::dirs::home().value_or(fs::path{}).generic_string();
    // Paths in the fixtures are the temporary project's and the home's; the replay rewrites @ROOT@ to
    // the directory a test runs in and @HOME@ to its home.
    auto scrub = [&](std::string text) {
        text = replace_all(std::move(text), app.generic_string(), "@ROOT@");
        if (!home.empty()) text = replace_all(std::move(text), home, "@HOME@");
        return text;
    };
    auto save = [&](std::string_view name, std::string text) { spit(out / name, scrub(std::move(text))); };

    auto version = run(mcpp, {"--version"}, tmp);
    save("version.txt", version.out.substr(0, version.out.find('\n') + 1));
    save("protocol-version.json", run(mcpp, {"--protocol-version"}, tmp).out);
    if (run(mcpp, {"new", "app"}, tmp).code != 0) throw std::runtime_error("mcpp new app failed");

    save("build-ok.ndjson", run(mcpp, {"build", "--message-format", "json"}, app).out);
    {
        const auto last = read_json(app / "target" / ".last-result.json");
        save("result-ok.json", read_json(last["data"]["result"].get<std::string>()).dump(2) + "\n");
    }
    save("launch.json", run(mcpp, {"run", "--print-launch"}, app).out);
    save("test.ndjson", run(mcpp, {"test", "--message-format", "json"}, app).out);
    save("test-list.ndjson", run(mcpp, {"test", "--list", "--message-format", "json"}, app).out);
    save("build-database.json", run(mcpp, {"emit", "build-database", "--format", "json"}, app).out);
    save("index-list.json", run(mcpp, {"index", "list", "--format", "json"}, app).out);
    save("pack-formats.json", run(mcpp, {"pack", "--list-formats"}, app).out);
    save("self-env.json", run(mcpp, {"self", "env", "--format", "json"}, app).out);

    // A developer's home holds hundreds of cache entries and the index hundreds of packages; the
    // fixtures keep enough of each to exercise every field.
    auto trimmed = [&](std::string text, std::string_view key, std::size_t n) {
        auto doc = Json::parse(text, nullptr, false);
        if (doc.is_discarded() || !doc.contains("data") || !doc["data"].contains(key)) return text;
        auto& list = doc["data"][std::string(key)];
        if (list.is_array() && list.size() > n) list.erase(list.begin() + static_cast<std::ptrdiff_t>(n), list.end());
        return doc.dump(2) + "\n";
    };
    save("search.json", trimmed(run(mcpp, {"search", "--format", "json"}, app).out, "packages", 60));
    save("cache-list.json", trimmed(run(mcpp, {"cache", "list", "--format", "json"}, app).out, "entries", 40));
    const auto toolchains = run(mcpp, {"toolchain", "list", "--format", "json"}, app).out;
    save("toolchain-list.json", toolchains);

    // The platform a target's row names (mcpp's platforms, design 2026-10-28 §9): a build for the
    // first target that has one, whose plan event carries the `platform` object.
    {
        auto doc = Json::parse(toolchains, nullptr, false);
        std::string target;
        if (!doc.is_discarded() && doc.contains("data") && doc["data"].contains("targets"))
            for (const auto& t : doc["data"]["targets"])
                if (t.contains("platform") && t["platform"].is_object() && !t["platform"].value("name", "").empty()) {
                    target = t.value("target", "");
                    break;
                }
        if (!target.empty()) {
            save("build-platform.ndjson", run(mcpp, {"build", "--target", target, "--message-format", "json"}, app).out);
            save("toolchain-list-platform.json", toolchains);
        } else {
            std::println(std::cerr, "note: this mcpp reports no platform per target; build-platform.ndjson not captured");
        }
    }

    spit(app / "src" / "main.cpp", "int main() { return undeclared_name; }\n");
    save("build-failed.ndjson", run(mcpp, {"build", "--message-format", "json"}, app).out);
    spit(app / "mcpp.toml", "[package\nname = \"app\"\n");
    save("build-manifest-error.ndjson", run(mcpp, {"build", "--message-format", "json"}, app).out);
    fs::remove_all(tmp);
    std::println("fixtures from {} in {}", version.out.substr(0, version.out.find('\n')), out.generic_string());
    return 0;
}

// ── gen ─────────────────────────────────────────────────────────────────────

struct Spec {
    Json settings, commands, en, zh;
};

Spec load_spec(const fs::path& root) {
    return Spec{read_json(root / "spec" / "settings.json"), read_json(root / "spec" / "commands.json"),
                read_json(root / "spec" / "i18n" / "manifest.en.json"), read_json(root / "spec" / "i18n" / "manifest.zh-cn.json")};
}

bool offered_in(const Json& c, std::string_view editor) {
    if (c.contains("editors") && c["editors"].is_array())
        return std::ranges::any_of(c["editors"], [&](const Json& e) { return e.is_string() && e.get<std::string>() == editor; });
    const auto kind = c.contains("action") ? c["action"].value("kind", "") : std::string{};
    if (kind == "forward" || kind == "editor") return editor == "vscode";
    return true;
}

// The `contributes` that come from spec/: commands, keybindings, menus, configuration.
void vscode_contributes(const Spec& spec, Json& contributes) {
    Json commands = Json::array(), keybindings = Json::array();
    Json menus = contributes.contains("menus") ? contributes["menus"] : Json::object();
    Json palette = menus.contains("commandPalette") ? menus["commandPalette"] : Json::array();
    for (const auto& c : spec.commands["commands"]) {
        if (!offered_in(c, "vscode")) continue;
        const auto id = c.value("id", "");
        Json cmd{{"command", id}, {"title", std::format("%command.{}.title%", id)}, {"category", "%category%"}};
        if (c.contains("icon") && c["icon"].contains("vscode")) cmd["icon"] = c["icon"]["vscode"];
        commands.push_back(cmd);
        if (c.contains("keybinding")) {
            Json k{{"command", id}, {"key", c["keybinding"].value("key", "")}};
            if (c["keybinding"].contains("mac")) k["mac"] = c["keybinding"]["mac"];
            if (c.contains("when")) k["when"] = c["when"];
            keybindings.push_back(k);
        }
        if (c.contains("menus"))
            for (const auto& m : c["menus"]) {
                Json entry{{"command", id}};
                if (m.contains("group")) entry["group"] = m["group"];
                if (m.contains("when")) entry["when"] = m["when"];
                menus[m.value("menu", "")].push_back(entry);
            }
        if (!c.value("palette", true)) palette.push_back(Json{{"command", id}, {"when", "false"}});
        else if (c.contains("when")) palette.push_back(Json{{"command", id}, {"when", c["when"]}});
    }
    menus["commandPalette"] = palette;
    contributes["commands"] = commands;
    contributes["keybindings"] = keybindings;
    contributes["menus"] = menus;

    // One configuration object; a setting's order is its group's order, then its own.
    std::map<std::string, int> groupOrder;
    for (const auto& g : spec.settings["groups"]) groupOrder[g.value("id", "")] = g.value("order", 0);
    Json properties = Json::object();
    for (const auto& s : spec.settings["settings"]) {
        const auto key = s.value("key", "");
        Json p{{"type", s.value("type", "string")}, {"default", s.contains("default") ? s["default"] : Json(nullptr)}};
        if (s.contains("enum")) p["enum"] = s["enum"];
        if (s.value("type", "") == "array") p["items"] = Json{{"type", "string"}};
        if (s.contains("minimum")) p["minimum"] = s["minimum"];
        if (s.contains("maximum")) p["maximum"] = s["maximum"];
        p["scope"] = s.value("scope", "resource");
        p["order"] = groupOrder[s.value("group", "")] * 1000 + s.value("order", 0);
        p["description"] = std::format("%{}.title%", key);
        p["markdownDescription"] = std::format("%{}.description%", key);
        if (s.value("deprecated", false)) p["markdownDeprecationMessage"] = std::format("%{}.deprecationMessage%", key);
        if (s.value("tier", "") == "advanced") p["tags"] = Json::array({"advanced"});
        properties[key] = p;
    }
    contributes["configuration"] = Json{{"title", "%mcpp.configuration.title%"}, {"properties", properties}};
}

void copy_tree(const fs::path& from, const fs::path& to, std::span<const std::string_view> skip = {}) {
    std::error_code ec;
    if (!fs::exists(from, ec)) return;
    for (fs::recursive_directory_iterator it(from, ec), end; !ec && it != end; it.increment(ec)) {
        const auto rel = fs::relative(it->path(), from, ec);
        const auto relText = rel.generic_string();
        if (std::ranges::any_of(skip, [&](std::string_view s) { return relText == s || relText.starts_with(std::string(s) + "/"); })) {
            if (it->is_directory(ec)) it.disable_recursion_pending();
            continue;
        }
        if (it->is_directory(ec)) { fs::create_directories(to / rel, ec); continue; }
        fs::create_directories((to / rel).parent_path(), ec);
        fs::copy_file(it->path(), to / rel, fs::copy_options::overwrite_existing, ec);
        if (ec) throw std::runtime_error(std::format("copying {}: {}", it->path().generic_string(), ec.message()));
    }
}

int gen_vscode(std::span<const std::string> args) {
    const auto root = repo();
    const auto out = fs::path(option(args, "--out").value_or((root / "vscode" / "dist" / "extension").string()));
    const auto spec = load_spec(root);
    auto pkg = read_json(root / "vscode" / "package.json");
    vscode_contributes(spec, pkg["contributes"]);
    // npm's own fields stay in the development manifest.
    pkg.erase("scripts");
    pkg.erase("devDependencies");
    spit(out / "package.json", pkg.dump(2) + "\n");
    spit(out / "package.nls.json", spec.en.dump(2) + "\n");
    Json zh = spec.en;
    for (auto it = spec.zh.begin(); it != spec.zh.end(); ++it) zh[it.key()] = it.value();
    spit(out / "package.nls.zh-cn.json", zh.dump(2) + "\n");
    if (flag(args, "--stage")) {
        copy_tree(root / "vscode" / "out" / "src", out / "out" / "src");
        copy_tree(root / "vscode" / "media", out / "media");
        copy_tree(root / "vscode" / "syntaxes", out / "syntaxes");
        static constexpr std::array<std::string_view, 2> webuiSkip{"tests", "tsconfig.json"};
        copy_tree(root / "webui", out / "webui", webuiSkip);
        for (const auto* f : {"README.md", "CHANGELOG.md", ".vscodeignore"})
            if (fs::exists(root / "vscode" / f)) fs::copy_file(root / "vscode" / f, out / f, fs::copy_options::overwrite_existing);
        fs::copy_file(root / "LICENSE", out / "LICENSE.txt", fs::copy_options::overwrite_existing);
        if (auto backends = option(args, "--backends")) copy_tree(*backends, out / "bin");
    }
    std::println("vscode declarations in {}", out.generic_string());
    return 0;
}

// A .properties value: ASCII, everything else as \uXXXX (UTF-16 units), and the format's own
// characters escaped.
std::string properties_escape(std::string_view s) {
    std::string out;
    std::size_t i = 0;
    while (i < s.size()) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        char32_t cp = c;
        int n = 1;
        if (c >= 0xF0) { cp = c & 0x07; n = 4; }
        else if (c >= 0xE0) { cp = c & 0x0F; n = 3; }
        else if (c >= 0xC0) { cp = c & 0x1F; n = 2; }
        for (int k = 1; k < n && i + k < s.size(); ++k) cp = (cp << 6) | (static_cast<unsigned char>(s[i + k]) & 0x3F);
        i += n;
        if (cp == '\\') out += "\\\\";
        else if (cp == '\n') out += "\\n";
        else if (cp == '=' || cp == ':' || cp == '#' || cp == '!') { out += '\\'; out += static_cast<char>(cp); }
        else if (cp < 0x80) out += static_cast<char>(cp);
        else if (cp < 0x10000) out += std::format("\\u{:04x}", static_cast<unsigned>(cp));
        else {
            const char32_t v = cp - 0x10000;
            out += std::format("\\u{:04x}\\u{:04x}", static_cast<unsigned>(0xD800 + (v >> 10)), static_cast<unsigned>(0xDC00 + (v & 0x3FF)));
        }
    }
    return out;
}

std::string xml_escape(std::string_view s) {
    std::string out;
    for (char c : s) {
        if (c == '&') out += "&amp;";
        else if (c == '<') out += "&lt;";
        else if (c == '>') out += "&gt;";
        else if (c == '"') out += "&quot;";
        else out += c;
    }
    return out;
}

int gen_jetbrains(std::span<const std::string> args) {
    const auto root = repo();
    const auto out = fs::path(option(args, "--out").value_or((root / "jetbrains" / "build" / "generated" / "mcpp-ide").string()));
    const auto spec = load_spec(root);
    // The actions: one group per spec group under Tools | mcpp, every command one action of the same
    // class, which reads what it does from the backend's command table by its id. JetBrains keeps its
    // own keymap (the VS Code chords are taken there: Ctrl+Alt+B is Go to Implementation), so no
    // shortcut is declared.
    std::string xml = "<!-- Generated by mcpp-ide-dev gen jetbrains from spec/commands.json. -->\n<idea-plugin>\n  <actions>\n";
    xml += "    <group id=\"mcpp.Menu\" popup=\"true\" icon=\"io.github.mcppcommunity.mcppclion.McppIcons.Logo\">\n";
    xml += "      <add-to-group group-id=\"ToolsMenu\" anchor=\"last\"/>\n";
    std::map<std::string, std::vector<const Json*>> byGroup;
    for (const auto& c : spec.commands["commands"])
        if (offered_in(c, "jetbrains") && !c.value("deprecated", false)) byGroup[c.value("group", "")].push_back(&c);
    for (const auto& g : spec.commands["groups"]) {
        const auto gid = g.value("id", "");
        if (!byGroup.contains(gid)) continue;
        xml += std::format("      <group id=\"mcpp.Menu.{}\" popup=\"true\">\n", gid);
        for (const auto* c : byGroup[gid]) {
            const auto id = c->value("id", "");
            xml += std::format("        <action id=\"{}\" class=\"io.github.mcppcommunity.mcppclion.actions.McppCommandAction\"", xml_escape(id));
            if (c->contains("icon") && (*c)["icon"].contains("jetbrains")) xml += std::format(" icon=\"{}\"", xml_escape((*c)["icon"]["jetbrains"].get<std::string>()));
            xml += "/>\n";
        }
        xml += "      </group>\n";
    }
    xml += "    </group>\n  </actions>\n</idea-plugin>\n";
    spit(out / "META-INF" / "mcpp-actions.xml", xml);

    // The bundles: the actions' texts, the groups', and every editor string under its own key.
    auto bundle = [&](const Json& strings) {
        std::string text = "# Generated by mcpp-ide-dev gen jetbrains from spec/i18n.\n";
        std::vector<std::pair<std::string, std::string>> rows;
        for (const auto& c : spec.commands["commands"]) {
            if (!offered_in(c, "jetbrains")) continue;
            const auto id = c.value("id", "");
            const auto key = std::format("command.{}.title", id);
            const auto title = strings.contains(key) ? strings[key].get<std::string>() : spec.en.value(key, id);
            rows.push_back({std::format("action.{}.text", id), title});
        }
        for (const auto& g : spec.commands["groups"]) {
            const auto key = g.value("title", "");
            const auto title = strings.contains(key) ? strings[key].get<std::string>() : spec.en.value(key, key);
            rows.push_back({std::format("group.mcpp.Menu.{}.text", g.value("id", "")), title});
        }
        rows.push_back({"group.mcpp.Menu.text", "mcpp"});
        for (auto it = strings.begin(); it != strings.end(); ++it)
            if (it->is_string()) rows.push_back({it.key(), it->get<std::string>()});
        std::ranges::sort(rows);
        rows.erase(std::unique(rows.begin(), rows.end(), [](const auto& a, const auto& b) { return a.first == b.first; }), rows.end());
        for (const auto& [k, v] : rows) text += std::format("{}={}\n", properties_escape(k), properties_escape(v));
        return text;
    };
    Json zh = spec.en;
    for (auto it = spec.zh.begin(); it != spec.zh.end(); ++it) zh[it.key()] = it.value();
    spit(out / "messages" / "McppClionBundle.properties", bundle(spec.en));
    spit(out / "messages" / "McppClionBundle_zh_CN.properties", bundle(zh));
    std::println("jetbrains declarations in {}", out.generic_string());
    return 0;
}

// ── check-i18n ──────────────────────────────────────────────────────────────

// The first argument of every `t("…")` / `t('…')` / `t(`…`)` in a source: a runtime string.
std::vector<std::string> runtime_strings(std::string_view text) {
    std::vector<std::string> out;
    for (std::size_t pos = 0; (pos = text.find("t(", pos)) != std::string_view::npos; pos += 2) {
        if (pos > 0) {
            const char before = text[pos - 1];
            if (std::isalnum(static_cast<unsigned char>(before)) || before == '_' || before == '.') continue;
        }
        std::size_t i = pos + 2;
        while (i < text.size() && (text[i] == ' ' || text[i] == '\n')) ++i;
        if (i >= text.size()) break;
        const char quote = text[i];
        if (quote != '"' && quote != '\'' && quote != '`') continue;
        std::string s;
        bool closed = false;
        for (++i; i < text.size(); ++i) {
            if (text[i] == '\\' && i + 1 < text.size()) {
                const char n = text[++i];
                s += n == 'n' ? '\n' : n;
                continue;
            }
            if (text[i] == quote) { closed = true; break; }
            if (quote == '`' && text[i] == '$' && i + 1 < text.size() && text[i + 1] == '{') { closed = false; break; }
            s += text[i];
        }
        if (closed && !s.empty()) out.push_back(std::move(s));
    }
    return out;
}

int check_i18n(std::span<const std::string>) {
    const auto root = repo();
    const auto zh = read_json(root / "spec" / "i18n" / "runtime.zh-cn.json");
    std::set<std::string> missing;
    std::size_t seen = 0;
    const std::array<std::pair<fs::path, std::vector<std::string>>, 4> trees{{
        {root / "src", {".cppm", ".cpp"}},
        {root / "vscode" / "src", {".ts"}},
        {root / "webui", {".js"}},
        {root / "jetbrains" / "src" / "main" / "kotlin", {".kt"}},
    }};
    std::error_code ec;
    for (const auto& [dir, exts] : trees)
        for (fs::recursive_directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
            if (!it->is_regular_file(ec)) continue;
            const auto ext = it->path().extension().string();
            if (std::ranges::find(exts, ext) == exts.end()) continue;
            if (it->path().generic_string().find("/node_modules/") != std::string::npos) continue;
            // This program's own scanner reads like a call.
            if (it->path().filename() == "dev.cpp") continue;
            auto found = runtime_strings(slurp(it->path()));
            // A cleanup plan's texts are translated where they are shown (policy.cppm's `titleKey`).
            for (std::string_view key : {"titleKey = \"", "detailKey = \""}) {
                const auto text = slurp(it->path());
                for (auto pos = text.find(key); pos != std::string::npos; pos = text.find(key, pos + 1)) {
                    const auto start = pos + key.size();
                    const auto end = text.find('"', start);
                    if (end != std::string::npos) found.push_back(text.substr(start, end - start));
                }
            }
            for (const auto& s : found) {
                ++seen;
                if (!zh.contains(s)) missing.insert(s);
            }
        }
    const auto en = read_json(root / "spec" / "i18n" / "manifest.en.json");
    const auto zhm = read_json(root / "spec" / "i18n" / "manifest.zh-cn.json");
    std::vector<std::string> missingEditor;
    for (auto it = en.begin(); it != en.end(); ++it) if (!zhm.contains(it.key())) missingEditor.push_back(it.key());
    for (const auto& s : missing) std::println("runtime string without zh-cn: {}", Json(s).dump());
    for (const auto& k : missingEditor) std::println("editor string without zh-cn: {}", k);
    std::println("{} runtime strings read; {} without a translation; {} editor strings without one", seen, missing.size(),
                 missingEditor.size());
    return missing.empty() && missingEditor.empty() ? 0 : 1;
}

}  // namespace

int main(int argc, char** argv) {
    std::vector<std::string> args(argv + 1, argv + argc);
    try {
        if (args.empty() || args[0] == "--help") {
            std::println("mcpp-ide-dev capture | gen vscode|jetbrains --out DIR | check-i18n");
            return args.empty() ? 2 : 0;
        }
        const std::span<const std::string> rest(args.begin() + 1, args.end());
        if (args[0] == "capture") return capture(rest);
        if (args[0] == "check-i18n") return check_i18n(rest);
        if (args[0] == "gen" && !rest.empty()) {
            const std::span<const std::string> more(rest.begin() + 1, rest.end());
            if (rest[0] == "vscode") return gen_vscode(more);
            if (rest[0] == "jetbrains") return gen_jetbrains(more);
        }
        std::println(std::cerr, "mcpp-ide-dev: unknown command (capture, gen vscode|jetbrains, check-i18n)");
        return 2;
    } catch (const std::exception& e) {
        std::println(std::cerr, "mcpp-ide-dev: {}", e.what());
        return 1;
    }
}
