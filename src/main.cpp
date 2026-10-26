// mcpp-ide: the IDE backend for mcpp.
//
//   mcpp-ide serve                  JSON-RPC on standard input and output (the adapters' wire)
//   mcpp-ide self-check [--json]    what the backend finds: mcpp, its contract, xlings
//   mcpp-ide --version
import std;
import nlohmann.json;
import mcppide.rpc;
import mcppide.server;
import mcppide.platform.stdio;

int main(int argc, char** argv) {
    std::vector<std::string_view> args(argv + 1, argv + argc);
    using namespace mcppide;
    if (args.empty() || args[0] == "--help" || args[0] == "-h") {
        std::println("mcpp-ide {} -- the IDE backend for mcpp\n", server::kVersion);
        std::println("  mcpp-ide serve                 JSON-RPC on standard input and output");
        std::println("  mcpp-ide self-check [--json]   what the backend finds: mcpp, its contract, xlings");
        std::println("  mcpp-ide --version");
        return args.empty() ? 2 : 0;
    }
    if (args[0] == "--version") {
        std::println("mcpp-ide {} (protocol {})", server::kVersion, server::kProtocol);
        return 0;
    }
    if (args[0] == "serve") {
        rpc::Server rpc;
        server::Session session(rpc);
        session.install();
        return rpc.serve();
    }
    if (args[0] == "self-check") {
        rpc::Server rpc([](std::string_view) { return true; });
        server::Session session(rpc);
        session.install();
        rpc.dispatch(R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"client":{"name":"cli"}}})");
        rpc.drain();
        auto report = session.self_check();
        if (args.size() > 1 && args[1] == "--json") { std::println("{}", report.dump(2)); return 0; }
        for (const auto& c : report["checks"])
            std::println("{:8} {}", c.value("status", ""), c.value("message", ""));
        bool bad = false;
        for (const auto& c : report["checks"]) bad = bad || c.value("status", "") == "error";
        return bad ? 1 : 0;
    }
    platform::write_stderr(std::format("mcpp-ide: unknown command '{}' (serve, self-check, --version)\n", args[0]));
    return 2;
}
