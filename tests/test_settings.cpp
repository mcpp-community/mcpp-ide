// The registry: defaults, rejections, renames, presets, and the runtime strings.
#include "support.hpp"
import std;
import nlohmann.json;
import mcppide.settings;
import mcppide.i18n;

using namespace mcppide;
using Json = nlohmann::json;

int main() {
    auto r = settings::resolve(Json{{"mcpp.build.profile", "fast"}, {"mcpp.cache.topN", 3}, {"mcpp.task.buildArgs", Json::array({1})}});
    CHECK(r["values"]["mcpp.build.profile"] == "dev");
    CHECK(r["values"]["mcpp.cache.topN"] == 3 || r["values"].contains("mcpp.views.cache.topN"));
    bool enumRejected = false, itemsRejected = false;
    for (const auto& x : r["rejected"]) {
        enumRejected = enumRejected || (x["key"] == "mcpp.build.profile" && x["reason"] == "enum");
        itemsRejected = itemsRejected || (x["key"] == "mcpp.task.buildArgs" && x["reason"] == "items");
    }
    CHECK(enumRejected && itemsRejected);
    auto reg = settings::registry();
    CHECK(reg["settings"].size() > 60 && reg["presets"].size() >= 3);
    CHECK(settings::preset("quiet").has_value() && !settings::preset("nope"));
    CHECK(i18n::normalize_locale("zh_CN") == "zh-cn" && i18n::normalize_locale("de") == "en");
    CHECK(i18n::translate("{0} and {1}", "en", {"a", "b"}) == "a and b");
    CHECK(i18n::translate("Actions", "zh-cn") == "操作");
    std::println("test_settings: ok");
}
