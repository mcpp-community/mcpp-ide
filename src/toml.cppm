// mcpp.toml read tolerantly, for an editor: the structure an edit in progress already has -- table
// headers, keys, values -- with where each one is, and the place of a cursor in it. Nothing here
// throws for a document being typed (an unclosed `[`, string, array or inline table): what was
// recognised is answered, and an open construct says so. It is not a TOML validator; mcpp is
// (manifest/check).
//
// The subset read: table headers (`[[...]]` arrays of tables, quoted segments), bare, quoted and
// dotted keys, strings (`"""` and `'''` multi-line ones too), integers, booleans, arrays and inline
// tables (nested, across lines), and comments. Positions are 0-based; a column is a byte offset in
// the line (mcppide.text turns it into the editor's UTF-16 column).
//
// A port of mcpp-vscode 0.9's src/toml/parser.ts (design 2026-10-30 part 2 §8); the goldens in
// tests/rules are that implementation's answers.
export module mcppide.toml;

import std;
import mcppide.spec;
import mcppide.text;

export namespace mcppide::toml {

struct Pos {
    int line = 0, col = 0;
};

// 0-based; the end is exclusive.
struct Range {
    int startLine = 0, startCol = 0, endLine = 0, endCol = 0;
};

// One segment of a key or a table name: the name unquoted, and the token's range (quotes included).
struct KeySegment {
    std::string name;
    bool quoted = false;
    Range range;
};

enum class ValueKind { string, integer, boolean, array, inlineTable, unknown };
std::string_view name_of(ValueKind k);

struct KeyValue;

// A value. `open`: not closed (a string without its closing quote, an array without `]`, an inline
// table without `}`), its range running to where the scan stopped.
struct Value {
    ValueKind kind = ValueKind::unknown;
    Range range;
    bool open = false;
    std::optional<std::string> text;     // a string's content (escapes as written), a scalar's token
    char quote = 0;                       // a string's quote
    bool multiline = false;
    std::optional<Range> contentRange;    // a string's content, without the quotes
    std::vector<Value> elements;          // an array's
    std::vector<KeyValue> entries;        // an inline table's
};

// `a.b.c = value`: the key's segments, and the value when one was written.
struct KeyValue {
    std::vector<KeySegment> keyPath;
    Range range;
    std::optional<Value> value;
};

// `[a.b]` or `[[a.b]]`; `open`: no `]` yet.
struct Section {
    std::vector<KeySegment> segments;
    bool isArray = false;
    bool open = false;
    Range range;
    int line = 0;
};

using Node = std::variant<Section, KeyValue>;

struct Document {
    std::vector<Node> nodes;
};

// What a table is: before any header (`top`), a table the rules layer does not know (`unknown`), or
// one it reads as `group` -- a section of spec/toml-schema.json, or a table that has its own reading
// (`workspace.dependencies`, `runtime.capability`).
struct Resolution {
    enum class Kind { top, unknown, known } kind = Kind::top;
    std::string group;
    std::vector<std::string> segments;
};

Resolution resolve_section(const std::vector<std::string>& segments);

// The token a suggestion replaces, on the cursor's line (bytes).
struct ReplaceRange {
    int start = 0, end = 0;
};

// Where a cursor is.
struct Context {
    enum class Kind { header, key, value } kind = Kind::key;
    // header: the segments before the cursor's token; whether it is `[[`.
    std::vector<std::string> segments;
    bool isArray = false;
    // key and value: the table the statement is in.
    Resolution section;
    // key: inside an inline table, the keys leading to it; the dotted key's segments before the
    // cursor's token.
    std::vector<std::string> containerPath, keyPrefix;
    // value: the key's whole path (through inline tables and arrays), the kind of the value under
    // the cursor (none before it starts), whether the cursor is inside a string and the string is open.
    std::vector<std::string> keyPath;
    std::optional<ValueKind> valueKind;
    bool insideString = false, stringOpen = false;
    ReplaceRange replace;
};

// The document's structure. `lines` as text::lines_of gives them.
Document parse(const std::vector<std::string>& lines);
// The context of a cursor at `line` and UTF-16 column `character`, both clamped into the document.
Context context_at(const std::vector<std::string>& lines, int line, int character);

}  // namespace mcppide::toml

namespace mcppide::toml {

std::string_view name_of(ValueKind k) {
    switch (k) {
        case ValueKind::string: return "string";
        case ValueKind::integer: return "integer";
        case ValueKind::boolean: return "boolean";
        case ValueKind::array: return "array";
        case ValueKind::inlineTable: return "inlineTable";
        case ValueKind::unknown: return "unknown";
    }
    return "unknown";
}

namespace {

// Tables the rules layer reads by name, whether or not the schema has them.
const std::set<std::string, std::less<>> kKnownSections = {
    "package", "lib", "build", "generated_files", "dependencies", "dev-dependencies", "build-dependencies",
    "features", "indices", "capabilities", "runtime", "resources", "toolchain", "xlings", "xlings.workspace",
    "xlings.envs", "workspace", "workspace.dependencies", "pack", "pack.bundle-project", "language",
    "tools.overrides",
};

// `<base>.<name>` is `<base>`.
const std::set<std::string, std::less<>> kParameterizedBases = {
    "targets", "profile", "feature-deps", "dependencies", "dev-dependencies",
};

// A platform's tables a target selector overrides (`[target.<sel>.link]` is `[platform.link]`).
const std::set<std::string, std::less<>> kPlatformOverrides = {"carrier", "layers", "link", "runners", "tools"};

// Tables whose sub-tables are named entries.
bool names_entries(std::string_view head) {
    return kParameterizedBases.contains(head) || head == "build-dependencies" || head == "features" || head == "platform";
}

const std::set<std::string, std::less<>>& schema_sections() {
    static const auto names = [] {
        std::set<std::string, std::less<>> out;
        for (const auto& s : spec::toml_schema()["sections"]) out.insert(s.value("name", ""));
        return out;
    }();
    return names;
}

std::string join(std::span<const std::string> parts) {
    std::string out;
    for (const auto& p : parts) {
        if (!out.empty()) out += '.';
        out += p;
    }
    return out;
}

std::optional<std::string> resolve_group(std::span<const std::string> segments) {
    const auto joined = join(segments);
    if (kKnownSections.contains(joined) || schema_sections().contains(joined)) return joined;
    const auto& head = segments[0];
    if (head == "target") {
        if (segments.size() <= 2) return "target";
        auto rest = segments.subspan(2);
        if (kPlatformOverrides.contains(rest[0])) {
            std::vector<std::string> platform{"platform"};
            platform.insert(platform.end(), rest.begin(), rest.end());
            return resolve_group(platform);
        }
        return resolve_group(rest);
    }
    if (head == "workspace" && segments.size() >= 3 && segments[1] == "dependencies") return "workspace.dependencies";
    if (head == "workspace" && segments.size() >= 2) return resolve_group(segments.subspan(1));
    if (segments.size() >= 2 && segments.back() == "requires_abi") return "requires_abi";
    if (head == "platform" && segments.size() >= 3 && segments[1] == "hosted" && segments[2] == "layers")
        return "platform.layers";
    if (head == "runtime" && segments.size() == 2) return "runtime.capability";
    if (names_entries(head) && segments.size() >= 2) {
        for (auto length = segments.size() - 1; length >= 1; --length) {
            const auto name = join(segments.first(length));
            if (schema_sections().contains(name) || kKnownSections.contains(name)) return name;
        }
        return head;
    }
    return std::nullopt;
}

bool bare_key_char(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
}

// The cursor's context was found: the scan stops.
struct Found {};

class Scanner {
public:
    explicit Scanner(std::vector<std::string> lines, std::optional<Pos> cursor = std::nullopt) : lines_(std::move(lines)) {
        for (auto& l : lines_)
            if (!l.empty() && l.back() == '\r') l.pop_back();
        if (lines_.empty()) lines_.emplace_back();
        if (cursor) {
            const int line = std::clamp(cursor->line, 0, static_cast<int>(lines_.size()) - 1);
            cursor_ = Pos{line, cursor->col};
        }
    }

    // A cursor given as a UTF-16 column.
    static Scanner at(const std::vector<std::string>& lines, int line, int character) {
        Scanner s(lines);
        const int l = std::clamp(line, 0, static_cast<int>(s.lines_.size()) - 1);
        const auto byte = text::byte_offset(s.lines_[l], std::max(0, character));
        s.cursor_ = Pos{l, static_cast<int>(byte)};
        return s;
    }

    std::optional<Context> context;

    std::vector<Node> scan() {
        std::vector<Node> nodes;
        while (!eof()) {
            skip_trivia();
            // Between statements: a key starts here.
            if (cursor_reached()) capture_key({}, {});
            if (eof()) break;
            const auto start = pos();
            if (peek() == '[') {
                nodes.emplace_back(scan_header(start));
            } else if (auto kv = scan_key_value({})) {
                nodes.emplace_back(std::move(*kv));
            }
            // Whatever is not recognised is consumed, so the scan ends.
            if (same(pos(), start)) advance();
        }
        return nodes;
    }

    // At the end of the document: a key of the current table.
    Context final_context() const {
        Context c;
        c.kind = Context::Kind::key;
        c.section = section_resolution();
        c.replace = empty_replace();
        return c;
    }

private:
    std::vector<std::string> lines_;
    int line_ = 0, col_ = 0;
    std::optional<Pos> cursor_;
    std::optional<std::vector<std::string>> currentSection_;

    Pos pos() const { return {line_, col_}; }
    bool eof() const { return line_ >= static_cast<int>(lines_.size()); }
    bool at_eol() const { return eof() || col_ >= static_cast<int>(lines_[line_].size()); }
    char peek() const { return at_eol() ? '\0' : lines_[line_][col_]; }
    char peek_at(int offset) const {
        if (eof()) return '\0';
        const auto& t = lines_[line_];
        return col_ + offset < static_cast<int>(t.size()) ? t[col_ + offset] : '\0';
    }
    void advance() {
        if (eof()) return;
        col_ += 1;
        if (col_ > static_cast<int>(lines_[line_].size())) {
            line_ += 1;
            col_ = 0;
        }
    }
    static bool same(Pos a, Pos b) { return a.line == b.line && a.col == b.col; }
    static int cmp(Pos a, Pos b) { return a.line != b.line ? a.line - b.line : a.col - b.col; }

    void skip_inline_ws() {
        while (peek() == ' ' || peek() == '\t') col_ += 1;
    }
    // Spaces, line ends and comments (a `#` inside a string never reaches here).
    void skip_trivia() {
        while (!eof()) {
            const char ch = peek();
            if (ch == ' ' || ch == '\t') { col_ += 1; continue; }
            if (at_eol()) { line_ += 1; col_ = 0; continue; }
            if (ch == '#') {
                while (!at_eol()) col_ += 1;
                continue;
            }
            break;
        }
    }

    Range range_from(Pos start) const { return {start.line, start.col, line_, col_}; }

    std::string slice(Pos a, Pos b) const {
        // A scan that ran to the end of the document (an unclosed multi-line string) ends one line
        // past the last; its text ends where the last line does.
        if (b.line >= static_cast<int>(lines_.size())) {
            const int last = static_cast<int>(lines_.size()) - 1;
            b = {last, static_cast<int>(lines_[last].size())};
            if (a.line > last) return {};
        }
        auto cut = [](const std::string& s, int from, int to) {
            from = std::clamp(from, 0, static_cast<int>(s.size()));
            to = std::clamp(to, from, static_cast<int>(s.size()));
            return s.substr(from, to - from);
        };
        if (a.line == b.line) return cut(lines_[a.line], a.col, b.col);
        std::string out = cut(lines_[a.line], a.col, static_cast<int>(lines_[a.line].size()));
        for (int l = a.line + 1; l < b.line; ++l) out += "\n" + lines_[l];
        out += "\n" + cut(lines_[b.line], 0, b.col);
        return out;
    }

    bool cursor_reached() const { return cursor_ && cmp(pos(), *cursor_) >= 0; }

    // Whether the cursor is in the range, its end included (a cursor often sits right after a token).
    bool cursor_within(const Range& r) const {
        if (!cursor_) return false;
        const auto& c = *cursor_;
        if (c.line < r.startLine || c.line > r.endLine) return false;
        if (c.line == r.startLine && c.col < r.startCol) return false;
        if (c.line == r.endLine && c.col > r.endCol) return false;
        return true;
    }

    // The token around the cursor on its line, up to a stop character either side.
    template <class Stop>
    ReplaceRange expand_on_line(Stop stop) const {
        if (!cursor_) return {};
        const auto& t = lines_[cursor_->line];
        const int at = std::min(cursor_->col, static_cast<int>(t.size()));
        int start = at, end = at;
        while (start > 0 && !stop(t[start - 1])) --start;
        while (end < static_cast<int>(t.size()) && !stop(t[end])) ++end;
        return {start, end};
    }

    ReplaceRange empty_replace() const {
        const int col = cursor_ ? cursor_->col : 0;
        return {col, col};
    }
    static ReplaceRange token_replace(const Range& r) { return {r.startCol, r.endCol}; }
    static std::optional<ReplaceRange> single_line_replace(const Range& r) {
        if (r.startLine != r.endLine) return std::nullopt;
        return ReplaceRange{r.startCol, r.endCol};
    }

    Resolution section_resolution() const {
        if (!currentSection_) return Resolution{};
        return resolve_section(*currentSection_);
    }

    static std::vector<std::string> names(const std::vector<KeySegment>& segments) {
        std::vector<std::string> out;
        for (const auto& s : segments) out.push_back(s.name);
        return out;
    }

    [[noreturn]] void capture(Context c) {
        context = std::move(c);
        throw Found{};
    }
    [[noreturn]] void capture_key(std::vector<std::string> containerPath, std::vector<std::string> keyPrefix,
                                  const Range* token = nullptr) {
        Context c;
        c.kind = Context::Kind::key;
        c.section = section_resolution();
        c.containerPath = std::move(containerPath);
        c.keyPrefix = std::move(keyPrefix);
        c.replace = token ? token_replace(*token) : empty_replace();
        capture(std::move(c));
    }
    [[noreturn]] void capture_value(std::vector<std::string> keyPath, std::optional<ValueKind> kind,
                                    bool insideString = false, bool stringOpen = false,
                                    std::optional<ReplaceRange> replace = std::nullopt) {
        Context c;
        c.kind = Context::Kind::value;
        c.section = section_resolution();
        c.keyPath = std::move(keyPath);
        c.valueKind = kind;
        c.insideString = insideString;
        c.stringOpen = stringOpen;
        c.replace = replace ? *replace : empty_replace();
        capture(std::move(c));
    }
    [[noreturn]] void capture_header(const std::vector<KeySegment>& segments, bool isArray, const Range* token = nullptr) {
        Context c;
        c.kind = Context::Kind::header;
        c.segments = names(segments);
        c.isArray = isArray;
        c.replace = token ? token_replace(*token) : empty_replace();
        capture(std::move(c));
    }

    // `[a.b]`, `[[a.b]]`, quoted segments; without its `]` the header is open and the rest of the
    // line is skipped.
    Section scan_header(Pos start) {
        advance();   // [
        bool isArray = false;
        if (peek() == '[') {
            isArray = true;
            advance();
        }
        std::vector<KeySegment> segments;
        bool open = true;
        for (;;) {
            skip_inline_ws();
            if (cursor_reached()) capture_header(segments, isArray);
            auto segment = scan_key_segment();
            if (!segment) break;
            if (cursor_within(segment->range)) capture_header(segments, isArray, &segment->range);
            segments.push_back(std::move(*segment));
            skip_inline_ws();
            if (peek() == '.') {
                advance();
                continue;
            }
            break;
        }
        skip_inline_ws();
        if (cursor_reached()) capture_header(segments, isArray);
        if (peek() == ']') {
            advance();
            if (isArray && peek() == ']') advance();
            // `[[x]` with one `]` is closed too.
            open = false;
        }
        if (open)
            while (!at_eol()) col_ += 1;
        Section node{segments, isArray, open, range_from(start), start.line};
        currentSection_ = names(segments);
        return node;
    }

    // A bare or quoted key segment (a quoted one that runs past its line is cut there).
    std::optional<KeySegment> scan_key_segment() {
        const auto start = pos();
        const char quote = peek();
        if (quote == '"' || quote == '\'') {
            advance();
            const auto contentStart = pos();
            while (!at_eol() && peek() != quote) {
                if (quote == '"' && peek() == '\\' && col_ + 1 < static_cast<int>(lines_[line_].size())) {
                    col_ += 2;
                    continue;
                }
                col_ += 1;
            }
            const auto contentEnd = pos();
            if (peek() == quote) advance();
            return KeySegment{slice(contentStart, contentEnd), true, range_from(start)};
        }
        if (!bare_key_char(quote)) return std::nullopt;
        while (bare_key_char(peek())) col_ += 1;
        return KeySegment{slice(start, pos()), false, range_from(start)};
    }

    // A dotted key and its value; `containerPath` the keys leading into an inline table.
    std::optional<KeyValue> scan_key_value(const std::vector<std::string>& containerPath) {
        const auto start = pos();
        std::vector<KeySegment> segments;
        for (;;) {
            skip_inline_ws();
            if (cursor_reached()) capture_key(containerPath, names(segments));
            auto segment = scan_key_segment();
            if (!segment) break;
            if (cursor_within(segment->range)) capture_key(containerPath, names(segments), &segment->range);
            segments.push_back(std::move(*segment));
            skip_inline_ws();
            if (peek() == '.') {
                advance();
                continue;
            }
            break;
        }
        skip_inline_ws();
        // Between the key and `=`: still the key.
        if (cursor_reached()) capture_key(containerPath, names(segments));
        const auto keyNames = names(segments);
        std::optional<Value> value;
        if (peek() == '=') {
            advance();
            skip_inline_ws();
            auto valuePath = containerPath;
            valuePath.insert(valuePath.end(), keyNames.begin(), keyNames.end());
            // After `=`, before the value.
            if (cursor_reached()) {
                // A token after the cursor on its line is what a suggestion replaces whole.
                if (!at_eol() && peek() != '#') {
                    auto ahead = scan_value(valuePath);   // a cursor inside the token is captured there
                    const auto r = single_line_replace(ahead.range);
                    capture_value(valuePath, ahead.kind, false, false, r ? *r : empty_replace());
                }
                capture_value(valuePath, std::nullopt);
            }
            // A value starts on the key's line, or there is none.
            if (!at_eol() && peek() != '#') value = scan_value(valuePath);
        }
        if (segments.empty() && !value) return std::nullopt;
        return KeyValue{std::move(segments), range_from(start), std::move(value)};
    }

    Value scan_value(const std::vector<std::string>& keyPath) {
        const char ch = peek();
        if (ch == '"' || ch == '\'') return scan_string(keyPath);
        if (ch == '[') return scan_array(keyPath);
        if (ch == '{') return scan_inline_table(keyPath);
        return scan_bare(keyPath);
    }

    Value scan_string(const std::vector<std::string>& keyPath) {
        const auto start = pos();
        const char quote = peek();
        advance();
        bool multiline = false, open = true;
        auto contentStart = pos(), contentEnd = pos();
        if (peek() == quote) {
            advance();
            if (peek() == quote) {
                multiline = true;
                advance();
                contentStart = pos();
            } else {
                open = false;   // "" or ''
            }
        }
        std::optional<Pos> closeStart;
        if (open) {
            while (!eof()) {
                if (!multiline && at_eol()) break;   // a one-line string runs past its line: open
                const char ch = peek();
                if (ch == '\\' && quote == '"') {
                    advance();
                    if (!eof() && (multiline || !at_eol())) advance();
                    continue;
                }
                if (ch == quote) {
                    if (multiline) {
                        if (peek_at(1) == quote && peek_at(2) == quote) {
                            contentEnd = pos();
                            closeStart = pos();
                            advance();
                            advance();
                            advance();
                            open = false;
                            break;
                        }
                        advance();
                        continue;
                    }
                    contentEnd = pos();
                    closeStart = pos();
                    advance();
                    open = false;
                    break;
                }
                advance();
            }
            if (open) contentEnd = pos();
        }
        Value node;
        node.kind = ValueKind::string;
        node.range = range_from(start);
        node.open = open;
        node.text = slice(contentStart, contentEnd);
        node.quote = quote;
        node.multiline = multiline;
        node.contentRange = Range{contentStart.line, contentStart.col, contentEnd.line, contentEnd.col};
        if (cursor_within(node.range) && cursor_) {
            const auto& cursor = *cursor_;
            const bool afterOpen = cmp(cursor, contentStart) >= 0;
            const bool beforeClose = !closeStart || cmp(cursor, *closeStart) <= 0;
            const bool insideString = afterOpen && beforeClose;
            // Inside the content: the token up to the quotes on this line, within the content.
            auto replace = empty_replace();
            if (insideString) {
                const auto expanded = expand_on_line([quote](char c) { return c == quote; });
                const int minCol = contentStart.line == cursor.line ? contentStart.col : 0;
                const int maxCol = contentEnd.line == cursor.line ? contentEnd.col
                                 : static_cast<int>(lines_[cursor.line].size());
                replace = {std::max(expanded.start, minCol), std::min(std::max(expanded.end, minCol), maxCol)};
            }
            capture_value(keyPath, ValueKind::string, insideString, open && afterOpen, replace);
        }
        return node;
    }

    Value scan_array(const std::vector<std::string>& keyPath) {
        const auto start = pos();
        advance();   // [
        Value node;
        node.kind = ValueKind::array;
        node.open = true;
        for (;;) {
            skip_trivia();
            if (cursor_reached()) capture_value(keyPath, std::nullopt);
            if (eof()) break;
            const char ch = peek();
            if (ch == ']') {
                advance();
                node.open = false;
                break;
            }
            if (ch == ',') {
                advance();
                continue;
            }
            const auto elementStart = pos();
            node.elements.push_back(scan_value(keyPath));
            if (same(pos(), elementStart)) advance();
        }
        node.range = range_from(start);
        return node;
    }

    Value scan_inline_table(const std::vector<std::string>& keyPath) {
        const auto start = pos();
        advance();   // {
        Value node;
        node.kind = ValueKind::inlineTable;
        node.open = true;
        for (;;) {
            skip_trivia();
            if (cursor_reached()) capture_key(keyPath, {});
            if (eof()) break;
            const char ch = peek();
            if (ch == '}') {
                advance();
                node.open = false;
                break;
            }
            if (ch == ',') {
                advance();
                continue;
            }
            const auto entryStart = pos();
            if (auto entry = scan_key_value(keyPath)) node.entries.push_back(std::move(*entry));
            if (same(pos(), entryStart)) advance();
        }
        node.range = range_from(start);
        return node;
    }

    // A bare token: a boolean, an integer, anything else `unknown` (not validated).
    Value scan_bare(const std::vector<std::string>& keyPath) {
        const auto start = pos();
        auto end = pos();
        while (!at_eol()) {
            const char ch = peek();
            if (ch == ',' || ch == ']' || ch == '}' || ch == '[' || ch == '{' || ch == '#') break;
            col_ += 1;
            if (ch != ' ' && ch != '\t') end = pos();
        }
        Value node;
        node.text = slice(start, end);
        const auto& t = *node.text;
        if (t == "true" || t == "false") {
            node.kind = ValueKind::boolean;
        } else {
            // /^[+-]?\d[\d_]*$/
            std::size_t i = (!t.empty() && (t[0] == '+' || t[0] == '-')) ? 1 : 0;
            bool integer = i < t.size() && std::isdigit(static_cast<unsigned char>(t[i]));
            for (std::size_t j = i; integer && j < t.size(); ++j)
                integer = std::isdigit(static_cast<unsigned char>(t[j])) || t[j] == '_';
            node.kind = integer ? ValueKind::integer : ValueKind::unknown;
        }
        node.range = Range{start.line, start.col, end.line, end.col};
        if (cursor_within(node.range)) capture_value(keyPath, node.kind, false, false, token_replace(node.range));
        return node;
    }
};

}  // namespace

Resolution resolve_section(const std::vector<std::string>& segments) {
    if (segments.empty()) return Resolution{};
    if (auto group = resolve_group(segments)) return Resolution{Resolution::Kind::known, *group, {}};
    return Resolution{Resolution::Kind::unknown, {}, segments};
}

Document parse(const std::vector<std::string>& lines) {
    Scanner s(lines);
    return Document{s.scan()};
}

Context context_at(const std::vector<std::string>& lines, int line, int character) {
    auto s = Scanner::at(lines, line, character);
    try {
        s.scan();
    } catch (const Found&) {
        if (s.context) return *s.context;
    }
    // Not reached: the cursor is in the space at the end, a key of the current table.
    return s.final_context();
}

}  // namespace mcppide::toml
