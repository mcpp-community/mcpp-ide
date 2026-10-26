// The runtime strings, in the user's language: English keys, the Chinese
// bundle generated from spec/i18n/runtime.zh-cn.json. `{0}`, `{1}`... are the
// arguments. Both adapters ask the backend, so a message reads the same in
// either editor.
export module mcppide.i18n;

import std;
import nlohmann.json;
import mcppide.spec;

export namespace mcppide::i18n {

// "auto" resolves to the editor's display language, which the adapter sends.
std::string normalize_locale(std::string_view locale);
std::string translate(std::string_view key, std::string_view locale, const std::vector<std::string>& args = {});
// The whole bundle for a locale, for a webview that renders without asking.
nlohmann::json bundle(std::string_view locale);

}  // namespace mcppide::i18n

namespace mcppide::i18n {

std::string normalize_locale(std::string_view locale) {
    std::string l(locale);
    for (auto& c : l) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    std::ranges::replace(l, '_', '-');
    if (l.starts_with("zh")) return "zh-cn";
    return "en";
}


std::string translate(std::string_view key, std::string_view locale, const std::vector<std::string>& args) {
    std::string text(key);
    if (normalize_locale(locale) == "zh-cn")
        if (auto it = spec::runtime_zh_cn().find(key); it != spec::runtime_zh_cn().end()) text = it->second;
    for (std::size_t i = 0; i < args.size(); ++i) {
        const auto marker = std::format("{{{}}}", i);
        for (auto pos = text.find(marker); pos != std::string::npos; pos = text.find(marker, pos + args[i].size()))
            text.replace(pos, marker.size(), args[i]);
    }
    return text;
}

nlohmann::json bundle(std::string_view locale) {
    nlohmann::json out = nlohmann::json::object();
    if (normalize_locale(locale) == "zh-cn")
        for (const auto& [en, z] : spec::runtime_zh_cn()) out[en] = z;
    return out;
}

}  // namespace mcppide::i18n
