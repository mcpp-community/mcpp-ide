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
    // A platform's tables, under [platform] and under the selector that overrides it.
    CHECK(manifest::section_of("platform") == "platform");
    CHECK(manifest::section_of("platform.link") == "platform.link");
    CHECK(manifest::section_of("platform.link.inputs") == "platform.link.inputs");
    CHECK(manifest::section_of("platform.build.'cfg(arch = \"aarch64\")'") == "platform.build");
    CHECK(manifest::section_of("platform.hosted.layers") == "platform.layers");
    CHECK(manifest::section_of("target.'cfg(platform = \"openkal\")'.layers") == "platform.layers");
    CHECK(manifest::section_of("target.x86_64-openkal.link") == "platform.link");
    CHECK(manifest::section_of("target.x86_64-openkal.versions") == "versions");
    CHECK(manifest::section_of("tools.overrides") == "tools.overrides");
    // A sub-table that is a section of its own, a mirrored table, the ABI switches.
    CHECK(manifest::section_of("pack.bundle-project") == "pack.bundle-project");
    CHECK(manifest::section_of("runtime.requirements") == "runtime.requirements");
    CHECK(manifest::section_of("hooks.during_build") == "hooks.during_build");
    CHECK(manifest::section_of("resources.version-info") == "resources.version-info");
    CHECK(manifest::section_of("xlings.workspace") == "xlings.workspace");
    CHECK(manifest::section_of("profile.release") == "profile");
    CHECK(manifest::section_of("features.gpu") == "features");
    CHECK(manifest::section_of("workspace.package") == "package");
    CHECK(manifest::section_of("workspace.target.'cfg(os = \"linux\")'.build") == "build");
    CHECK(manifest::section_of("target.'cfg(os = \"linux\")'.abi") == "abi");
    CHECK(manifest::section_of("target.x86_64-linux-gnu.feature-requires-abi") == "feature-requires-abi");
    CHECK(manifest::section_of("package.requires_abi") == "requires_abi");
    CHECK(manifest::section_of("features.mt.requires_abi") == "requires_abi");

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
    // The typed API's names, not the wire's (`mcpp:link-driver=` is `mcpp::link_driver`).
    auto link = labels(manifest::buildscript_complete(Json{{"text", "int main(){ mcpp::link"}, {"line", 0}, {"character", 22}}));
    CHECK(has(link, "link_driver") && has(link, "link_input_from") && has(link, "link_lib") && !has(link, "link-driver"));
    auto def = labels(manifest::buildscript_complete(Json{{"text", "int main(){ mcpp::de"}, {"line", 0}, {"character", 20}}));
    CHECK(has(def, "define") && has(def, "deploy"));

    // The platform keys of mcpp 2026.10.27.1.
    const std::string plat = "[platform]\ntargets = []\n\n[platform.link]\n\n[target.x86_64-openkal]\n\n[host]\n\n[pack]\n";
    auto pk = labels(manifest::complete(Json{{"text", plat}, {"line", 2}, {"character", 0}}));
    CHECK(has(pk, "universal") && has(pk, "plugin") && !has(pk, "targets"));
    auto lk = labels(manifest::complete(Json{{"text", plat}, {"line", 4}, {"character", 0}}));
    CHECK(has(lk, "driver") && has(lk, "seal") && has(lk, "exe"));
    auto drv = labels(manifest::complete(Json{{"text", "[platform.link]\ndriver = \"\"\n"}, {"line", 1}, {"character", 10}}));
    CHECK(has(drv, "lld") && has(drv, "compiler"));
    auto tk = labels(manifest::complete(Json{{"text", plat}, {"line", 6}, {"character", 0}}));
    CHECK(has(tk, "platform") && has(tk, "carrier") && has(tk, "run_under"));
    auto hk = labels(manifest::complete(Json{{"text", plat}, {"line", 8}, {"character", 0}}));
    CHECK(has(hk, "platform"));
    auto pack = labels(manifest::complete(Json{{"text", plat}, {"line", 10}, {"character", 0}}));
    CHECK(has(pack, "format") && has(pack, "compress") && has(pack, "universal") && has(pack, "sign"));
    auto ph = labels(manifest::complete(Json{{"text", "[plat"}, {"line", 0}, {"character", 5}}));
    CHECK(has(ph, "[platform]") && has(ph, "[platform.link]") && has(ph, "[[platform.link.inputs]]") && has(ph, "[host]"));
    // Every table mcpp 2026.10.27.1 reads has its keys.
    const std::string more = "[c-abi]\n\n[profile.release]\n\n[hooks.during_build]\n\n[target.'cfg(os = \"linux\")'.abi]\n\n"
                             "[[runtime.requirements]]\n\n[features.mt]\n\n[xlings]\n\n[test]\n";
    auto keysAt = [&](int line) { return labels(manifest::complete(Json{{"text", more}, {"line", line}, {"character", 0}})); };
    auto cabi = keysAt(1);
    CHECK(has(cabi, "presents") && has(cabi, "data-model") && has(cabi, "wchar") && has(cabi, "builtins"));
    CHECK(has(keysAt(3), "ndebug") && has(keysAt(3), "trim_paths"));
    CHECK(has(keysAt(5), "loop") && !has(keysAt(5), "timeout_seconds"));
    CHECK(has(keysAt(7), "threads") && has(keysAt(7), "exceptions"));
    CHECK(has(keysAt(9), "phase") && has(keysAt(9), "discovery"));
    CHECK(has(keysAt(11), "implies") && has(keysAt(11), "requires_abi") && has(keysAt(11), "rule_module"));
    CHECK(has(keysAt(13), "subos") && has(keysAt(13), "deps"));
    CHECK(has(keysAt(15), "windows_code_page") && has(keysAt(15), "windows_long_paths"));
    auto th = labels(manifest::complete(Json{{"text", "[tar"}, {"line", 0}, {"character", 4}}));
    CHECK(has(th, "[target.'cfg(...)'.abi]") && has(th, "[target.'cfg(...)'.requires_abi]") && !has(th, "[target.<selector>.abi]"));
    std::println("test_manifest: ok");
}
