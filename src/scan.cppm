// Where a C++ source starts, read as text (mcpp design 2026-10-30 part 2 §6.3): the line of `main`.
// What a test program's cases are is the program's to say (mcpp test spec B3); nothing of a test
// framework is read here.
//
// NOTHING IS BUILT AND NOTHING IS RUN. A file is scanned when it is opened, before any build and
// without mcppls, so the editors can place ▶ at once. Comments, string and character literals (raw
// strings included) and preprocessor lines are skipped, so a `main(` in a comment or a string is
// not one. A position found this way is approximate where a language service has a better one: the
// editors take the language service's position of `main` when it gives one.
export module mcppide.scan;

import std;

export namespace mcppide::scan {

struct Result {
    std::optional<int> mainLine;   // 0-based
};

// Scans `text` for `main`.
Result scan(std::string_view text);

}  // namespace mcppide::scan

namespace mcppide::scan {

namespace {

bool ident_start(char c) { return std::isalpha(static_cast<unsigned char>(c)) || c == '_'; }
bool ident_char(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; }

// A cursor over the text that knows its line and steps over what is not code.
struct Cursor {
    std::string_view text;
    std::size_t at = 0;
    int line = 0;
    std::size_t lineStart = 0;

    bool done() const { return at >= text.size(); }
    char peek(std::size_t ahead = 0) const { return at + ahead < text.size() ? text[at + ahead] : '\0'; }
    void step() {
        if (text[at] == '\n') { ++line; lineStart = at + 1; }
        ++at;
    }
    int column() const { return static_cast<int>(at - lineStart); }

    void skip_line_comment() { while (!done() && peek() != '\n') step(); }
    void skip_block_comment() {
        step(); step();
        while (!done() && !(peek() == '*' && peek(1) == '/')) step();
        if (!done()) { step(); step(); }
    }
    void skip_quoted(char quote) {
        step();
        while (!done() && peek() != quote && peek() != '\n') {
            if (peek() == '\\' && at + 1 < text.size()) step();
            step();
        }
        if (!done() && peek() == quote) step();
    }
    // R"delim( ... )delim" -- the cursor is on the R.
    void skip_raw() {
        step(); step();   // R"
        std::string delimiter;
        while (!done() && peek() != '(' && delimiter.size() < 16) { delimiter.push_back(peek()); step(); }
        const std::string end = ")" + delimiter + "\"";
        while (!done() && text.substr(at, end.size()) != end) step();
        for (std::size_t i = 0; i < end.size() && !done(); ++i) step();
    }
    // A preprocessor line, with its continuations.
    void skip_directive() {
        while (!done() && peek() != '\n') {
            if (peek() == '\\' && peek(1) == '\n') step();
            else if (peek() == '/' && peek(1) == '*') { skip_block_comment(); continue; }
            step();
        }
    }
    // Whether the cursor is on the start of a raw string literal (R, u8R, uR, UR, LR).
    bool on_raw() const {
        std::size_t i = at;
        if (text.substr(i, 2) == "u8") i += 2;
        else if (peek() == 'u' || peek() == 'U' || peek() == 'L') i += 1;
        return i + 1 < text.size() && text[i] == 'R' && text[i + 1] == '"';
    }
};

}  // namespace

Result scan(std::string_view text) {
    Result r;
    Cursor c{text};
    int depth = 0;                  // braces
    bool lineHasCode = false;       // a `#` is a directive only first on its line
    std::string previous;           // the previous identifier, for `int main(`
    while (!c.done()) {
        const char ch = c.peek();
        if (ch == '\n') { lineHasCode = false; c.step(); continue; }
        if (std::isspace(static_cast<unsigned char>(ch))) { c.step(); continue; }
        if (ch == '/' && c.peek(1) == '/') { c.skip_line_comment(); continue; }
        if (ch == '/' && c.peek(1) == '*') { c.skip_block_comment(); continue; }
        if (ch == '#' && !lineHasCode) { c.skip_directive(); previous.clear(); continue; }
        lineHasCode = true;
        if (c.on_raw()) { c.skip_raw(); previous.clear(); continue; }
        if (ch == '"') {
            c.skip_quoted(ch);
            previous.clear();
            continue;
        }
        if (ch == '\'') { c.skip_quoted(ch); previous.clear(); continue; }
        if (ident_start(ch)) {
            const auto from = c.at;
            const int line = c.line;
            while (!c.done() && ident_char(c.peek())) c.step();
            const std::string word(text.substr(from, c.at - from));
            // What follows the name, past blanks and comments on the way.
            Cursor look = c;
            while (!look.done()) {
                if (std::isspace(static_cast<unsigned char>(look.peek()))) { look.step(); continue; }
                if (look.peek() == '/' && look.peek(1) == '*') { look.skip_block_comment(); continue; }
                if (look.peek() == '/' && look.peek(1) == '/') { look.skip_line_comment(); continue; }
                break;
            }
            const bool call = look.peek() == '(';
            if (call && word == "main" && depth == 0 && !r.mainLine && (previous == "int" || previous == "auto"))
                r.mainLine = line;
            previous = word;
            continue;
        }
        if (ch == '{') ++depth;
        else if (ch == '}' && depth > 0) --depth;
        // `std::main`, `x.main`: not the program's.
        if (ch != ':' ) previous.clear();
        c.step();
    }
    return r;
}

}  // namespace mcppide::scan
