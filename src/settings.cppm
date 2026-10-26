// The settings registry (spec/settings.json), for both editors: what a value
// may be, what it is when absent, which earlier key it replaces, and the
// named presets. An adapter hands the backend the values it read; the backend
// answers the values to use and what it rejected, so an invalid hand-edited
// value is reported the same way in VS Code and in CLion.
export module mcppide.settings;

import std;
import nlohmann.json;
import mcppide.generated.spec;

export namespace mcppide::settings {

using Json = nlohmann::json;

struct Checked {
    Json value;                       // the value to use
    std::optional<std::string> reason;   // why the given value was rejected
    std::string expectation;          // what the setting takes
};

const generated::Setting* find(std::string_view key);
Json default_of(const generated::Setting& s);
Checked check(const generated::Setting& s, const Json& given);
// Every setting: the values to use, the rejections, and the renames an
// earlier key asks for (`aliases`).
Json resolve(const Json& given);
// The registry, for a settings panel: groups, entries, presets.
Json registry();
std::optional<Json> preset(std::string_view id);

}  // namespace mcppide::settings

namespace mcppide::settings {

const generated::Setting* find(std::string_view key) {
    for (const auto& s : generated::kSettings) if (s.key == key) return &s;
    return nullptr;
}

Json default_of(const generated::Setting& s) {
    auto v = Json::parse(s.defaultJson, nullptr, false);
    return v.is_discarded() ? Json(nullptr) : v;
}

namespace {
std::string expectation_of(const generated::Setting& s) {
    if (!s.enumJson.empty()) {
        auto e = Json::parse(s.enumJson, nullptr, false);
        std::string out;
        for (const auto& v : e) { if (!out.empty()) out += " | "; out += v.get<std::string>(); }
        return out;
    }
    if (s.type == "number" && (!s.minimum.empty() || !s.maximum.empty()))
        return std::format("{} … {}", s.minimum.empty() ? "-∞" : std::string(s.minimum),
                           s.maximum.empty() ? "∞" : std::string(s.maximum));
    return s.type == "array" ? "array of strings" : std::string(s.type);
}
}  // namespace

Checked check(const generated::Setting& s, const Json& v) {
    Checked out{default_of(s), std::nullopt, expectation_of(s)};
    auto reject = [&](std::string why) { out.reason = std::move(why); return out; };
    if (v.is_null()) return out;
    if (s.type == "boolean") {
        if (!v.is_boolean()) return reject("type");
    } else if (s.type == "number") {
        if (!v.is_number()) return reject("type");
        const double d = v.get<double>();
        if (!s.minimum.empty() && d < std::stod(std::string(s.minimum))) return reject("minimum");
        if (!s.maximum.empty() && d > std::stod(std::string(s.maximum))) return reject("maximum");
    } else if (s.type == "string") {
        if (!v.is_string()) return reject("type");
        if (!s.enumJson.empty()) {
            auto e = Json::parse(s.enumJson, nullptr, false);
            if (std::ranges::find(e, v) == e.end()) return reject("enum");
        }
    } else if (s.type == "array") {
        if (!v.is_array()) return reject("type");
        for (const auto& item : v) if (!item.is_string()) return reject("items");
    }
    out.value = v;
    return out;
}

Json resolve(const Json& given) {
    Json values = Json::object(), rejected = Json::array(), renames = Json::array(), deprecated = Json::array();
    for (const auto& s : generated::kSettings) {
        Json v = given.is_object() && given.contains(std::string(s.key)) ? given[std::string(s.key)] : Json(nullptr);
        if (v.is_null()) {
            // An earlier spelling of this setting still in the user's file.
            auto aliases = Json::parse(s.aliasesJson, nullptr, false);
            if (!aliases.is_discarded())
                for (const auto& a : aliases)
                    if (a.is_string() && given.is_object() && given.contains(a.get<std::string>())) {
                        v = given[a.get<std::string>()];
                        renames.push_back(Json{{"from", a}, {"to", s.key}, {"value", v}});
                        break;
                    }
        }
        auto c = check(s, v);
        if (c.reason) rejected.push_back(Json{{"key", s.key}, {"reason", *c.reason}, {"expectation", c.expectation},
                                              {"given", v}});
        if (s.deprecated && !v.is_null()) deprecated.push_back(s.key);
        values[std::string(s.key)] = c.value;
    }
    return Json{{"values", values}, {"rejected", rejected}, {"renames", renames}, {"deprecated", deprecated}};
}

Json registry() {
    Json entries = Json::array();
    for (const auto& s : generated::kSettings) {
        Json e{{"key", s.key}, {"type", s.type}, {"default", default_of(s)}, {"group", s.group},
               {"scope", s.scope}, {"applies", s.applies}, {"tier", s.tier}, {"deprecated", s.deprecated},
               {"title", s.title}, {"description", s.description}, {"expectation", expectation_of(s)}};
        if (!s.enumJson.empty()) e["enum"] = Json::parse(s.enumJson);
        if (!s.minimum.empty()) e["minimum"] = std::stod(std::string(s.minimum));
        if (!s.maximum.empty()) e["maximum"] = std::stod(std::string(s.maximum));
        entries.push_back(std::move(e));
    }
    return Json{{"settings", entries}, {"presets", Json::parse(generated::kPresetsJson)["presets"]}};
}

std::optional<Json> preset(std::string_view id) {
    for (const auto& p : Json::parse(generated::kPresetsJson)["presets"])
        if (p.value("id", "") == id) return p;
    return std::nullopt;
}

}  // namespace mcppide::settings
