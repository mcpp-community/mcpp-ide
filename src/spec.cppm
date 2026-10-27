// spec/ as the backend reads it: the settings registry, the presets, the runtime strings, and
// mcpp's manifest keys and build-program directives, parsed once from the JSON that build.mcpp
// carried into `mcppide.generated.spec`.
export module mcppide.spec;

import std;
import nlohmann.json;
import mcppide.generated.spec;

export namespace mcppide::spec {

using Json = nlohmann::json;

struct Setting {
    std::string key;
    std::string type;          // boolean | string | number | array
    Json        defaultValue;
    Json        allowed;       // the allowed values, an array, or null
    std::optional<double> minimum, maximum;
    std::string group, scope, applies, tier;
    bool        deprecated = false;
    std::vector<std::string> aliases;   // earlier keys of this setting
    std::string title, description;
};

const std::vector<Setting>& settings();
const Json& presets();                  // spec/presets.json's "presets"
const std::map<std::string, std::string, std::less<>>& runtime_zh_cn();
const Json& toml_schema();
const Json& buildscript_api();
// spec/commands.json: the command table both adapters declare (groups, commands).
const Json& commands();
// The editors' strings (command titles, setting titles, view names): spec/i18n/manifest.<locale>.json.
// "zh-cn" falls back to English key by key.
const Json& manifest_strings(std::string_view locale);
// One editor string; the key itself when no bundle has it.
std::string manifest_string(std::string_view key, std::string_view locale);

}  // namespace mcppide::spec

namespace mcppide::spec {

const std::vector<Setting>& settings() {
    static const std::vector<Setting> all = [] {
        std::vector<Setting> out;
        const auto doc = Json::parse(generated::kSettingsJson);
        for (const auto& s : doc.at("settings")) {
            Setting v;
            v.key = s.value("key", "");
            v.type = s.value("type", "");
            v.defaultValue = s.contains("default") ? s["default"] : Json(nullptr);
            v.allowed = s.contains("enum") ? s["enum"] : Json(nullptr);
            if (s.contains("minimum") && s["minimum"].is_number()) v.minimum = s["minimum"].get<double>();
            if (s.contains("maximum") && s["maximum"].is_number()) v.maximum = s["maximum"].get<double>();
            v.group = s.value("group", "");
            v.scope = s.value("scope", "");
            v.applies = s.value("applies", "");
            v.tier = s.value("tier", "");
            v.deprecated = s.value("deprecated", false);
            if (s.contains("aliases") && s["aliases"].is_array())
                for (const auto& a : s["aliases"]) if (a.is_string()) v.aliases.push_back(a.get<std::string>());
            v.title = s.value("title", "");
            v.description = s.value("description", "");
            out.push_back(std::move(v));
        }
        return out;
    }();
    return all;
}

const Json& presets() {
    static const Json p = Json::parse(generated::kPresetsJson).value("presets", Json::array());
    return p;
}

const std::map<std::string, std::string, std::less<>>& runtime_zh_cn() {
    static const auto m = [] {
        std::map<std::string, std::string, std::less<>> out;
        const auto doc = Json::parse(generated::kRuntimeZhCnJson);
        for (auto it = doc.begin(); it != doc.end(); ++it)
            if (it->is_string()) out.emplace(it.key(), it->get<std::string>());
        return out;
    }();
    return m;
}

const Json& toml_schema() {
    static const Json s = Json::parse(generated::kTomlSchemaJson);
    return s;
}

const Json& buildscript_api() {
    static const Json a = Json::parse(generated::kBuildScriptApiJson);
    return a;
}

const Json& commands() {
    static const Json c = Json::parse(generated::kCommandsJson);
    return c;
}

const Json& manifest_strings(std::string_view locale) {
    static const Json en = Json::parse(generated::kManifestEnJson);
    static const Json zh = [] {
        Json z = en;
        const auto zhStrings = Json::parse(generated::kManifestZhCnJson);
        for (auto it = zhStrings.begin(); it != zhStrings.end(); ++it) z[it.key()] = it.value();
        return z;
    }();
    return locale == "zh-cn" ? zh : en;
}

std::string manifest_string(std::string_view key, std::string_view locale) {
    const auto& m = manifest_strings(locale);
    if (auto it = m.find(std::string(key)); it != m.end() && it->is_string()) return it->get<std::string>();
    return std::string(key);
}

}  // namespace mcppide::spec
