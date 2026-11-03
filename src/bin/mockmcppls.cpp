// A stand-in for mcppls in the tests: its version and `mcppls status` (mcppls S3 section 8).
//
// It is placed in a payload the test makes (`<payload>/bin/mcppls`, named by payload.json), and reads
// beside it:
//
//   <payload>/mock-version   the version `--version` prints (default 0.0.20)
//
// and in the root `status` is asked about:
//
//   <root>/.mock-mcppls-state    the workspace's state (default `none`)
//
// `status` states three actions: `restart` for every editor, `restartEngine` for VS Code alone, and
// `openLogDirectory` with VS Code arguments.
import std;
import mcpplibs.os;

namespace {

std::string slurp(const std::filesystem::path& p) {
    std::ifstream in(p, std::ios::binary);
    std::string text(std::istreambuf_iterator<char>(in), {});
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r' || text.back() == ' ')) text.pop_back();
    return text;
}

void out(std::string_view text) {
    (void)mcpplibs::os::stdio::write_output(text);
    (void)mcpplibs::os::stdio::flush_output();
}

std::string json_string(std::string_view s) {
    std::string q = "\"";
    for (char c : s) {
        if (c == '"' || c == '\\') q.push_back('\\');
        q.push_back(c);
    }
    return q + "\"";
}

}  // namespace

int main(int argc, char** argv) {
    const std::vector<std::string> args(argv + 1, argv + argc);
    const auto payload = std::filesystem::absolute(argv[0]).parent_path().parent_path();
    std::string version = slurp(payload / "mock-version");
    if (version.empty()) version = "0.0.20";
    if (!args.empty() && args[0] == "--version") {
        out(std::format("mcppls {}\n", version));
        return 0;
    }
    if (!args.empty() && args[0] == "status") {
        std::string root;
        for (std::size_t i = 1; i + 1 < args.size(); ++i)
            if (args[i] == "--root") root = args[i + 1];
        std::string state = slurp(std::filesystem::path(root) / ".mock-mcppls-state");
        if (state.empty()) state = "none";
        out(std::format(R"({{"schema": 1, "server": {{"version": {}, "platform": "test", "payload": {}}},
"capabilities": ["status"],
"workspace": {{"root": {}, "state": {}}},
"instances": [],
"actions": [
  {{"id": "restart", "title": "Restart the language server", "vscode": "mcppls.restartServer", "jetbrains": "community.mcpp.mcppls.RestartServer", "nvim": "McpplsRestart"}},
  {{"id": "restartEngine", "title": "Restart the engine", "vscode": "mcppls.restartClangd"}},
  {{"id": "openLogDirectory", "title": "Open the log directory", "vscode": "mcppls.revealCacheDirectory", "vscodeArguments": ["logs"]}}
]}}
)",
                        json_string(version), json_string(payload.generic_string()), json_string(root), json_string(state)));
        return 0;
    }
    out("usage: mcppls [--version | status --root <dir> --format json]\n");
    return 2;
}
