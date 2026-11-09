// The registry: defaults, rejections, renames, presets, and the runtime strings.
import std;
import mcppide.testsupport;
import mcpplibs.mtest;
import nlohmann.json;
import mcppide.settings;
import mcppide.i18n;

using testsupport::check;
using namespace mcpplibs::mtest;

using namespace mcppide;
using Json = nlohmann::json;

UTest settings{"settings", [] {
    auto r = settings::resolve(Json{{"mcpp.build.profile", "fast"}, {"mcpp.cache.topN", 3}, {"mcpp.task.buildArgs", Json::array({1})}});
    check(r["values"]["mcpp.build.profile"] == "dev");
    check(r["values"]["mcpp.cache.topN"] == 3 || r["values"].contains("mcpp.views.cache.topN"));
    bool enumRejected = false, itemsRejected = false;
    for (const auto& x : r["rejected"]) {
        enumRejected = enumRejected || (x["key"] == "mcpp.build.profile" && x["reason"] == "enum");
        itemsRejected = itemsRejected || (x["key"] == "mcpp.task.buildArgs" && x["reason"] == "items");
    }
    check(enumRejected && itemsRejected);
    auto reg = settings::registry();
    check(reg["settings"].size() > 60 && reg["presets"].size() >= 3);
    check(settings::preset("quiet").has_value() && !settings::preset("nope"));
    check(i18n::normalize_locale("zh_CN") == "zh-cn" && i18n::normalize_locale("de") == "en");
    check(i18n::translate("{0} and {1}", "en", {"a", "b"}) == "a and b");
    check(i18n::translate("Actions", "zh-cn") == "操作");
}};
