// The part of JSON Schema (2020-12) protocol/*.schema.json uses: `type` (one or a list), `enum`,
// `const`, `properties`, `required`, `additionalProperties`, `items`, `minItems`, `minimum`, `allOf`,
// `anyOf`, `oneOf` and `$ref` into the document's own `$defs`. Enough for the backend's tests to hold
// the golden fixtures to the contract the backend reads, and the backend's answers to the protocol
// the adapters read; a keyword outside this list is ignored, never guessed at.
export module mcppide.schema;

import std;
import nlohmann.json;

export namespace mcppide::schema {

using Json = nlohmann::json;

// The problems of `value` against `schema` (whose `$ref`s resolve in `root`), each one line with the
// JSON pointer of the value it is about; empty when the value conforms.
std::vector<std::string> validate(const Json& value, const Json& schema, const Json& root);

}  // namespace mcppide::schema

namespace mcppide::schema {

namespace {

bool has_type(const Json& v, std::string_view type) {
    if (type == "object") return v.is_object();
    if (type == "array") return v.is_array();
    if (type == "string") return v.is_string();
    if (type == "integer") return v.is_number_integer();
    if (type == "number") return v.is_number();
    if (type == "boolean") return v.is_boolean();
    if (type == "null") return v.is_null();
    return false;
}

const Json* resolve(const Json& root, std::string_view ref) {
    constexpr std::string_view prefix = "#/$defs/";
    if (!ref.starts_with(prefix)) return nullptr;
    const auto name = std::string(ref.substr(prefix.size()));
    if (!root.contains("$defs") || !root["$defs"].contains(name)) return nullptr;
    return &root["$defs"][name];
}

void check(const Json& v, const Json& s, const Json& root, const std::string& at, std::vector<std::string>& out, int depth) {
    if (depth > 64) { out.push_back(std::format("{}: the schema recurses too deep", at)); return; }
    if (s.is_boolean()) {
        if (!s.get<bool>()) out.push_back(std::format("{}: no value is allowed here", at.empty() ? "/" : at));
        return;
    }
    if (!s.is_object()) return;
    const auto where = at.empty() ? std::string("/") : at;
    if (s.contains("$ref")) {
        const auto* target = resolve(root, s["$ref"].get<std::string>());
        if (!target) out.push_back(std::format("{}: unresolved $ref {}", where, s["$ref"].get<std::string>()));
        else check(v, *target, root, at, out, depth + 1);
    }
    if (s.contains("type")) {
        bool ok = false;
        if (s["type"].is_string()) ok = has_type(v, s["type"].get<std::string>());
        else for (const auto& t : s["type"]) ok = ok || has_type(v, t.get<std::string>());
        if (!ok) { out.push_back(std::format("{}: expected {}, found {}", where, s["type"].dump(), v.type_name())); return; }
    }
    if (s.contains("const") && v != s["const"]) out.push_back(std::format("{}: expected {}, found {}", where, s["const"].dump(), v.dump()));
    if (s.contains("enum") && std::ranges::find(s["enum"], v) == s["enum"].end())
        out.push_back(std::format("{}: {} is not one of {}", where, v.dump(), s["enum"].dump()));
    if (s.contains("minimum") && v.is_number() && v.get<double>() < s["minimum"].get<double>())
        out.push_back(std::format("{}: {} is below {}", where, v.dump(), s["minimum"].dump()));
    if (v.is_object()) {
        if (s.contains("required"))
            for (const auto& k : s["required"])
                if (!v.contains(k.get<std::string>())) out.push_back(std::format("{}: missing \"{}\"", where, k.get<std::string>()));
        for (auto it = v.begin(); it != v.end(); ++it) {
            const auto path = at + "/" + it.key();
            if (s.contains("properties") && s["properties"].contains(it.key())) check(it.value(), s["properties"][it.key()], root, path, out, depth + 1);
            else if (s.contains("additionalProperties")) check(it.value(), s["additionalProperties"], root, path, out, depth + 1);
        }
    }
    if (v.is_array()) {
        if (s.contains("minItems") && v.size() < s["minItems"].get<std::size_t>())
            out.push_back(std::format("{}: {} items, at least {} expected", where, v.size(), s["minItems"].get<std::size_t>()));
        if (s.contains("items"))
            for (std::size_t i = 0; i < v.size(); ++i) check(v[i], s["items"], root, std::format("{}/{}", at, i), out, depth + 1);
    }
    if (s.contains("allOf")) for (const auto& sub : s["allOf"]) check(v, sub, root, at, out, depth + 1);
    auto matches = [&](const Json& sub) {
        std::vector<std::string> trial;
        check(v, sub, root, at, trial, depth + 1);
        return trial.empty();
    };
    if (s.contains("anyOf")) {
        bool any = false;
        for (const auto& sub : s["anyOf"]) if (matches(sub)) { any = true; break; }
        if (!any) out.push_back(std::format("{}: matches none of anyOf ({} alternatives)", where, s["anyOf"].size()));
    }
    if (s.contains("oneOf")) {
        int n = 0;
        for (const auto& sub : s["oneOf"]) if (matches(sub)) ++n;
        if (n != 1) out.push_back(std::format("{}: matches {} of oneOf, exactly one expected", where, n));
    }
}

}  // namespace

std::vector<std::string> validate(const Json& value, const Json& schema, const Json& root) {
    std::vector<std::string> out;
    check(value, schema, root, "", out, 0);
    return out;
}

}  // namespace mcppide::schema
