// mcpp.toml editing help: where the cursor is, what is offered there.
#include "support.hpp"
import std;
import nlohmann.json;
import mcppide.manifest;

using namespace mcppide;
using Json = nlohmann::json;

static std::vector<std::string> labels(const Json& r) {
    std::vector<std::string> out;
    for (const auto& i : r["items"]) out.push_back(i["label"].get<std::string>());
    return out;
}
static bool has(const std::vector<std::string>& v, std::string_view s) { return std::ranges::find(v, s) != v.end(); }

int main() {
    CHECK(manifest::section_of("target.'cfg(os = \"linux\")'.build") == "build");
    CHECK(manifest::section_of("dependencies.mcpplibs") == "dependencies");
    CHECK(manifest::section_of("targets.app") == "targets");

    const std::string text = "[package]\nname = \"a\"\n\n[build]\nbmi_schedule = \"\"\n\n";
    auto keys = labels(manifest::complete(Json{{"text", text}, {"line", 5}, {"character", 0}}));
    CHECK(has(keys, "sources") && !has(keys, "bmi_schedule"));   // a key already written is not offered again
    auto values = labels(manifest::complete(Json{{"text", text}, {"line", 4}, {"character", 16}}));
    CHECK(has(values, "auto") && has(values, "on") && has(values, "off"));
    auto headers = labels(manifest::complete(Json{{"text", "[bu"}, {"line", 0}, {"character", 3}}));
    CHECK(has(headers, "[build]") && has(headers, "[targets.<name>]"));
    auto hover = manifest::hover(Json{{"text", text}, {"line", 4}, {"character", 2}});
    CHECK(hover.is_object() && hover["contents"].get<std::string>().find("bmi_schedule") != std::string::npos);
    auto syms = manifest::symbols(Json{{"text", text}});
    CHECK(syms.size() == 2 && syms[1]["name"] == "build" && syms[1]["children"].size() == 1);
    auto bs = labels(manifest::buildscript_complete(Json{{"text", "int main(){ mcpp::cxx"}, {"line", 0}, {"character", 21}}));
    CHECK(has(bs, "cxxflag"));
    std::println("test_manifest: ok");
}
