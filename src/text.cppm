// Text as the editors hold it. A document is lines (split at `\n`, a `\r` before it dropped), and a
// position on a line is a count of UTF-16 code units, as LSP and every editor count it; the rules
// layer reads the UTF-8 bytes and turns a byte offset into that count where it answers. The
// whitespace JavaScript trims, JSON as JSON.stringify writes it, and the order localeCompare gives
// are here too: the rules layer answers what mcpp-vscode 0.9's TypeScript answered (design
// 2026-10-30 part 2 §8).
export module mcppide.text;

import std;
import nlohmann.json;

export namespace mcppide::text {

// `text.split(/\r?\n/)`.
std::vector<std::string> lines_of(std::string_view text);

// The UTF-16 code units of `line`'s first `byte` bytes.
int utf16_column(std::string_view line, std::size_t byte);
// The UTF-16 length of `s`.
int utf16_length(std::string_view s);
// The byte offset of UTF-16 column `column`, clamped to the line; a column inside a character is
// the character's start.
std::size_t byte_offset(std::string_view line, int column);

// The length of the JavaScript white space or line terminator at `s[i]`, or 0.
std::size_t space_at(std::string_view s, std::size_t i);
// String.prototype.trim, trimStart, trimEnd.
std::string_view trim(std::string_view s);
std::string_view trim_start(std::string_view s);
std::string_view trim_end(std::string_view s);

// JSON.stringify of a value read from JSON (no spaces; a string's characters as they are).
std::string stringify(const nlohmann::json& value);

// String.prototype.localeCompare as the root collation orders ASCII words: letters before their
// case (a < B < c), a lower-case letter before its capital, digits before letters; a negative,
// zero or positive result.
int locale_compare(std::string_view a, std::string_view b);

}  // namespace mcppide::text

namespace mcppide::text {

std::vector<std::string> lines_of(std::string_view text) {
    std::vector<std::string> out;
    std::size_t at = 0;
    for (;;) {
        const auto nl = text.find('\n', at);
        auto line = text.substr(at, nl == std::string_view::npos ? std::string_view::npos : nl - at);
        if (nl != std::string_view::npos && !line.empty() && line.back() == '\r') line.remove_suffix(1);
        out.emplace_back(line);
        if (nl == std::string_view::npos) break;
        at = nl + 1;
    }
    return out;
}

namespace {

// The bytes of the UTF-8 sequence led by `c`, and the UTF-16 units it is.
std::pair<std::size_t, int> sequence(unsigned char c) {
    if (c < 0x80) return {1, 1};
    if ((c & 0xE0) == 0xC0) return {2, 1};
    if ((c & 0xF0) == 0xE0) return {3, 1};
    if ((c & 0xF8) == 0xF0) return {4, 2};
    return {1, 1};
}

}  // namespace

int utf16_column(std::string_view line, std::size_t byte) {
    int units = 0;
    for (std::size_t i = 0; i < line.size() && i < byte;) {
        const auto [bytes, n] = sequence(static_cast<unsigned char>(line[i]));
        i += bytes;
        units += n;
    }
    return units;
}

int utf16_length(std::string_view s) { return utf16_column(s, s.size()); }

std::size_t byte_offset(std::string_view line, int column) {
    int units = 0;
    std::size_t i = 0;
    while (i < line.size()) {
        const auto [bytes, n] = sequence(static_cast<unsigned char>(line[i]));
        if (units + n > column) break;
        units += n;
        i += bytes;
    }
    return std::min(i, line.size());
}

std::size_t space_at(std::string_view s, std::size_t i) {
    if (i >= s.size()) return 0;
    const auto c = static_cast<unsigned char>(s[i]);
    if (c == ' ' || (c >= 0x09 && c <= 0x0D)) return 1;
    auto is = [&](std::string_view seq) { return s.substr(i, seq.size()) == seq; };
    if (c == 0xC2 && is("\xC2\xA0")) return 2;                                  // U+00A0
    if (c == 0xE1 && is("\xE1\x9A\x80")) return 3;                              // U+1680
    if (c == 0xE2 && i + 2 < s.size() && static_cast<unsigned char>(s[i + 1]) == 0x80) {
        const auto t = static_cast<unsigned char>(s[i + 2]);
        if ((t >= 0x80 && t <= 0x8A) || t == 0xA8 || t == 0xA9 || t == 0xAF) return 3;   // U+2000-200A, 2028, 2029, 202F
    }
    if (c == 0xE2 && is("\xE2\x81\x9F")) return 3;                              // U+205F
    if (c == 0xE3 && is("\xE3\x80\x80")) return 3;                              // U+3000
    if (c == 0xEF && is("\xEF\xBB\xBF")) return 3;                              // U+FEFF
    return 0;
}

std::string_view trim_start(std::string_view s) {
    std::size_t i = 0;
    while (auto n = space_at(s, i)) i += n;
    return s.substr(i);
}

std::string_view trim_end(std::string_view s) {
    // A space is at most three bytes; read backwards by trying each length.
    for (;;) {
        bool cut = false;
        for (std::size_t n = 1; n <= 3 && n <= s.size(); ++n)
            if (space_at(s, s.size() - n) == n) {
                s.remove_suffix(n);
                cut = true;
                break;
            }
        if (!cut) return s;
    }
}

std::string_view trim(std::string_view s) { return trim_end(trim_start(s)); }

std::string stringify(const nlohmann::json& value) {
    return value.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
}

int locale_compare(std::string_view a, std::string_view b) {
    auto rank = [](unsigned char c) -> int {
        if (c >= '0' && c <= '9') return 1000 + c;
        if (c >= 'a' && c <= 'z') return 2000 + (c - 'a') * 2;
        if (c >= 'A' && c <= 'Z') return 2000 + (c - 'A') * 2;
        return c;   // punctuation and spaces before digits and letters, in code order
    };
    for (std::size_t i = 0; i < a.size() && i < b.size(); ++i) {
        const int x = rank(static_cast<unsigned char>(a[i])), y = rank(static_cast<unsigned char>(b[i]));
        if (x != y) return x < y ? -1 : 1;
    }
    if (a.size() != b.size()) return a.size() < b.size() ? -1 : 1;
    // Equal but for case: the lower-case one first, at its first difference.
    for (std::size_t i = 0; i < a.size(); ++i)
        if (a[i] != b[i]) return std::islower(static_cast<unsigned char>(a[i])) ? -1 : 1;
    return 0;
}

}  // namespace mcppide::text
