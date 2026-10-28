// The settings registry (spec/settings.json), for both editors: what a value
// may be, what it is when absent, which earlier key it replaces, and the
// named presets. An adapter hands the backend the values it read; the backend
// answers the values to use and what it rejected, so an invalid hand-edited
// value is reported the same way in VS Code and in CLion.
export module mcppide.settings;

import std;
import nlohmann.json;
import mcppide.spec;

export namespace mcppide::settings {

using Json = nlohmann::json;

struct Checked {
    Json value;                       // the value to use
    std::optional<std::string> reason;   // why the given value was rejected
    std::string expectation;          // what the setting takes
};

const spec::Setting* find(std::string_view key);
Json default_of(const spec::Setting& s);
Checked check(const spec::Setting& s, const Json& given);
// Every setting: the values to use, the rejections, and the renames an
// earlier key asks for (`aliases`).
Json resolve(const Json& given);
// The registry, for a settings panel: groups, entries, presets, with the titles and descriptions
// in `locale` ("en" | "zh-cn").
Json registry(std::string_view locale = "en");
std::optional<Json> preset(std::string_view id);

}  // namespace mcppide::settings

namespace mcppide::settings {

const spec::Setting* find(std::string_view key) {
    for (const auto& s : spec::settings()) if (s.key == key) return &s;
    return nullptr;
}

Json default_of(const spec::Setting& s) { return s.defaultValue; }

namespace {
std::string number_text(double d) {
    auto t = std::format("{}", d);
    return t;
}

std::string expectation_of(const spec::Setting& s) {
    if (s.allowed.is_array()) {
        std::string out;
        for (const auto& v : s.allowed) { if (!out.empty()) out += " | "; out += v.is_string() ? v.get<std::string>() : v.dump(); }
        return out;
    }
    if (s.type == "number" && (s.minimum || s.maximum))
        return std::format("{} … {}", s.minimum ? number_text(*s.minimum) : "-∞", s.maximum ? number_text(*s.maximum) : "∞");
    return s.type == "array" ? "array of strings" : s.type;
}
}  // namespace

Checked check(const spec::Setting& s, const Json& v) {
    Checked out{default_of(s), std::nullopt, expectation_of(s)};
    auto reject = [&](std::string why) { out.reason = std::move(why); return out; };
    if (v.is_null()) return out;
    if (s.type == "boolean") {
        if (!v.is_boolean()) return reject("type");
    } else if (s.type == "number") {
        if (!v.is_number()) return reject("type");
        const double d = v.get<double>();
        if (s.minimum && d < *s.minimum) return reject("minimum");
        if (s.maximum && d > *s.maximum) return reject("maximum");
    } else if (s.type == "string") {
        if (!v.is_string()) return reject("type");
        if (s.allowed.is_array() && std::ranges::find(s.allowed, v) == s.allowed.end()) return reject("enum");
    } else if (s.type == "array") {
        if (!v.is_array()) return reject("type");
        for (const auto& item : v) if (!item.is_string()) return reject("items");
    }
    out.value = v;
    return out;
}

Json resolve(const Json& given) {
    Json values = Json::object(), rejected = Json::array(), renames = Json::array(), deprecated = Json::array();
    for (const auto& s : spec::settings()) {
        Json v = given.is_object() && given.contains(s.key) ? given[s.key] : Json(nullptr);
        if (v.is_null()) {
            // An earlier spelling of this setting still in the user's file.
            for (const auto& a : s.aliases)
                if (given.is_object() && given.contains(a)) {
                    v = given[a];
                    renames.push_back(Json{{"from", a}, {"to", s.key}, {"value", v}});
                    break;
                }
        }
        auto c = check(s, v);
        if (c.reason) rejected.push_back(Json{{"key", s.key}, {"reason", *c.reason}, {"expectation", c.expectation},
                                              {"given", v}});
        if (s.deprecated && !v.is_null()) deprecated.push_back(s.key);
        values[s.key] = c.value;
    }
    return Json{{"values", values}, {"rejected", rejected}, {"renames", renames}, {"deprecated", deprecated}};
}

Json registry(std::string_view locale) {
    auto text = [&](const std::string& key, const std::string& fallback) {
        auto v = spec::manifest_string(key, locale);
        return v == key ? fallback : v;
    };
    Json entries = Json::array();
    for (const auto& s : spec::settings()) {
        Json e{{"key", s.key}, {"type", s.type}, {"default", default_of(s)}, {"group", s.group},
               {"scope", s.scope}, {"applies", s.applies}, {"tier", s.tier}, {"deprecated", s.deprecated},
               {"title", text(s.key + ".title", s.title)}, {"description", text(s.key + ".description", s.description)},
               {"expectation", expectation_of(s)}};
        if (s.allowed.is_array()) e["enum"] = s.allowed;
        if (s.minimum) e["minimum"] = *s.minimum;
        if (s.maximum) e["maximum"] = *s.maximum;
        entries.push_back(std::move(e));
    }
    Json groups = spec::setting_groups();
    for (auto& g : groups) g["title"] = text("settings.group." + g.value("id", ""), g.value("title", ""));
    Json presets = spec::presets();
    for (auto& p : presets) {
        p["title"] = text("settings.preset." + p.value("id", "") + ".title", p.value("title", ""));
        p["description"] = text("settings.preset." + p.value("id", "") + ".description", p.value("description", ""));
    }
    return Json{{"groups", groups}, {"settings", entries}, {"presets", presets}};
}

std::optional<Json> preset(std::string_view id) {
    for (const auto& p : spec::presets())
        if (p.value("id", "") == id) return p;
    return std::nullopt;
}

}  // namespace mcppide::settings
