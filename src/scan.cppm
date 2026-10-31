// What a C++ source says about where it starts and what it tests, read as text (mcpp design
// 2026-10-30 part 2 §6.3, §7.3): the line of `main`, and the calls of a test framework's macros.
//
// NOTHING IS BUILT AND NOTHING IS RUN. A file is scanned when it is opened, before any build and
// without mcppls, so the editors can place ▶ at once. Comments, string and character literals (raw
// strings included) and preprocessor lines are skipped, so a `main(` in a comment or a `TEST(` in a
// string is not one. A position found this way is approximate where a language service has a
// better one: the editors take the language service's position of `main` when it gives one.
//
// A MACRO CALL COUNTS AT THE START OF A DECLARATION: after `;`, `{`, `}`, a preprocessor line or
// the start of the file. That is where a framework's test macros are written (at namespace scope,
// or in a test body for a section), and an expression that merely contains the name is not one.
export module mcppide.scan;

import std;

export namespace mcppide::scan {

// One call of a macro: its name, where it is (0-based line and column of the name), and its
// arguments as written, split at the top-level commas, each trimmed.
struct Call {
    std::string name;
    int line = 0;
    int column = 0;
    std::vector<std::string> arguments;
};

struct Result {
    std::optional<int> mainLine;   // 0-based
    std::vector<Call> calls;
};

// Scans `text` for `main` and for calls of the macros named in `macros`.
Result scan(std::string_view text, const std::vector<std::string>& macros);

// A string literal's contents, as the program sees them for the plain escapes (\" \\ \n \t); the
// argument itself when it is not one literal. Adjacent literals are joined.
std::string literal_value(std::string_view argument);

}  // namespace mcppide::scan

namespace mcppide::scan {

namespace {

bool ident_start(char c) { return std::isalpha(static_cast<unsigned char>(c)) || c == '_'; }
bool ident_char(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; }

std::string trim(std::string_view s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.remove_prefix(1);
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.remove_suffix(1);
    return std::string(s);
}

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

// The arguments of a call whose `(` the cursor is on, split at top-level commas; the cursor ends
// after the `)`.
std::vector<std::string> arguments(Cursor& c) {
    std::vector<std::string> out;
    std::string current;
    int depth = 0;
    c.step();   // (
    while (!c.done()) {
        const char ch = c.peek();
        if (ch == '/' && c.peek(1) == '/') { c.skip_line_comment(); continue; }
        if (ch == '/' && c.peek(1) == '*') { c.skip_block_comment(); continue; }
        if (ch == '"' || ch == '\'') {
            const auto from = c.at;
            c.skip_quoted(ch);
            current.append(c.text.substr(from, c.at - from));
            continue;
        }
        if (ch == '(' || ch == '[' || ch == '{' || ch == '<') { if (ch != '<') ++depth; }
        if (ch == ')' || ch == ']' || ch == '}') {
            if (depth == 0) {
                c.step();
                out.push_back(trim(current));
                return out;
            }
            --depth;
        }
        if (ch == ',' && depth == 0) {
            out.push_back(trim(current));
            current.clear();
            c.step();
            continue;
        }
        current.push_back(ch);
        c.step();
    }
    out.push_back(trim(current));
    return out;
}

}  // namespace

Result scan(std::string_view text, const std::vector<std::string>& macros) {
    Result r;
    Cursor c{text};
    int depth = 0;                  // braces
    bool atDeclaration = true;      // the next identifier starts a declaration
    bool lineHasCode = false;       // a `#` is a directive only first on its line
    std::string previous;           // the previous identifier, for `int main(`
    while (!c.done()) {
        const char ch = c.peek();
        if (ch == '\n') { lineHasCode = false; c.step(); continue; }
        if (std::isspace(static_cast<unsigned char>(ch))) { c.step(); continue; }
        if (ch == '/' && c.peek(1) == '/') { c.skip_line_comment(); continue; }
        if (ch == '/' && c.peek(1) == '*') { c.skip_block_comment(); continue; }
        if (ch == '#' && !lineHasCode) { c.skip_directive(); atDeclaration = true; previous.clear(); continue; }
        lineHasCode = true;
        if (c.on_raw()) { c.skip_raw(); atDeclaration = false; previous.clear(); continue; }
        if (ch == '"' || ch == '\'') { c.skip_quoted(ch); atDeclaration = false; previous.clear(); continue; }
        if (ident_start(ch)) {
            const auto from = c.at;
            const int line = c.line, column = c.column();
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
            if (call && atDeclaration && std::ranges::find(macros, word) != macros.end()) {
                c = look;
                r.calls.push_back(Call{word, line, column, arguments(c)});
                atDeclaration = false;
                previous.clear();
                continue;
            }
            if (call && word == "main" && depth == 0 && !r.mainLine && (previous == "int" || previous == "auto"))
                r.mainLine = line;
            previous = word;
            atDeclaration = false;
            continue;
        }
        if (ch == '{') { ++depth; atDeclaration = true; }
        else if (ch == '}') { if (depth > 0) --depth; atDeclaration = true; }
        else if (ch == ';') atDeclaration = true;
        else atDeclaration = false;
        // `std::main`, `x.main`: not the program's.
        if (ch != ':' ) previous.clear();
        c.step();
    }
    return r;
}

std::string literal_value(std::string_view argument) {
    std::string out;
    std::size_t i = 0;
    bool any = false;
    while (i < argument.size()) {
        while (i < argument.size() && std::isspace(static_cast<unsigned char>(argument[i]))) ++i;
        if (i >= argument.size()) break;
        if (argument[i] != '"') return std::string(argument);
        ++i;
        while (i < argument.size() && argument[i] != '"') {
            if (argument[i] == '\\' && i + 1 < argument.size()) {
                const char e = argument[i + 1];
                out.push_back(e == 'n' ? '\n' : e == 't' ? '\t' : e);
                i += 2;
                continue;
            }
            out.push_back(argument[i++]);
        }
        if (i >= argument.size()) return std::string(argument);
        ++i;
        any = true;
    }
    return any ? out : std::string(argument);
}

}  // namespace mcppide::scan
