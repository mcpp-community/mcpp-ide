// A test session (the mcpp test spec's O9, mcpp docs/specs/test.md): the command a project's test
// framework states (`test-session`, B6), started once per project and spoken to in JSON-RPC 2.0 on
// its standard streams, one message per line. The backend runs a project's cases through it when
// one is stated: a run is answered at once, a case added joins it, and a cancel ends the processes
// of what it names. Nothing here knows which framework states it; the methods and notifications are
// the spec's.
export module mcppide.testsession;

import std;
import nlohmann.json;
import mcpplibs.os;

export namespace mcppide::testsession {

using Json = nlohmann::json;

class Client {
public:
    // A notification of the session: its method (`test/build`, `test/case`, `test/program`,
    // `test/run`) and its parameters. Called from the session's reading thread.
    using notify_fn = std::function<void(std::string_view method, const Json& params)>;

    // The session started from `argv` in `cwd`, with `env` over this program's environment; an
    // error names why not.
    static std::expected<std::unique_ptr<Client>, std::string>
    start(const std::vector<std::string>& argv, const std::filesystem::path& cwd,
          const std::vector<std::pair<std::string, std::string>>& env, notify_fn notify);

    ~Client();
    Client(const Client&) = delete;
    Client& operator=(const Client&) = delete;

    // Called with a request's `result` on the reading thread, before any message the session sent
    // after its answer: what an answer names (a run) is known before its notifications arrive.
    using answered_fn = std::function<void(const Json& result)>;

    // A request and its answer (`result`), or why there is none: the session's `error`, its end,
    // or `limit` passing.
    std::expected<Json, std::string> request(std::string_view method, const Json& params,
                                             std::chrono::milliseconds limit = std::chrono::seconds{60},
                                             answered_fn answered = {});
    // Whether the session still runs.
    bool alive() const;

private:
    Client() = default;
    void read_loop();

    mcpplibs::os::process::process proc_;
    notify_fn notify_;
    mutable std::mutex mutex_;
    std::condition_variable answered_;
    std::map<std::int64_t, Json> answers_;
    std::map<std::int64_t, answered_fn> hooks_;
    std::int64_t next_ = 1;
    bool ended_ = false;
    std::jthread reader_;
};

}  // namespace mcppide::testsession

namespace mcppide::testsession {

namespace os = mcpplibs::os;

std::expected<std::unique_ptr<Client>, std::string>
Client::start(const std::vector<std::string>& argv, const std::filesystem::path& cwd,
              const std::vector<std::pair<std::string, std::string>>& env, notify_fn notify) {
    if (argv.empty()) return std::unexpected(std::string("the session names no command"));
    os::process::spawn_options o;
    o.program = argv.front();
    o.args.assign(argv.begin() + 1, argv.end());
    o.cwd = cwd;
    o.in = os::process::stdio::pipe;
    o.out = os::process::stdio::pipe;
    o.err = os::process::stdio::null;
    // Ending it ends the programs it started. It ends with the backend by its input closing (a
    // session ends at the end of its input); a lifetime bound by the system is not asked, as on
    // Linux it follows the thread that started it, and a request's thread ends with the request.
    o.own_unit = true;
    for (const auto& [k, v] : env) o.env.emplace_back(k, v);
    auto p = os::process::process::spawn(o);
    if (!p) return std::unexpected(std::format("{} did not start: {}", argv.front(), p.error().message()));
    std::unique_ptr<Client> c(new Client());
    c->proc_ = std::move(*p);
    c->notify_ = std::move(notify);
    c->reader_ = std::jthread([raw = c.get()] { raw->read_loop(); });
    return c;
}

Client::~Client() {
    if (proc_.valid()) {
        (void)proc_.write(Json{{"jsonrpc", "2.0"}, {"method", "shutdown"}, {"id", 0}}.dump() + "\n");
        proc_.close_input();
        if (auto w = proc_.wait_for(std::chrono::seconds{3}); !w || !*w) proc_.kill();
    }
    if (reader_.joinable()) reader_.join();
}

bool Client::alive() const {
    std::lock_guard g(mutex_);
    return !ended_;
}

void Client::read_loop() {
    std::string pending;
    for (;;) {
        auto chunk = proc_.read_output();
        if (!chunk || chunk->empty()) break;
        pending += *chunk;
        for (auto nl = pending.find('\n'); nl != std::string::npos; nl = pending.find('\n')) {
            const auto line = pending.substr(0, nl);
            pending.erase(0, nl + 1);
            auto m = Json::parse(line, nullptr, false);
            if (!m.is_object()) continue;
            if (m.contains("id") && m["id"].is_number_integer() && (m.contains("result") || m.contains("error"))) {
                const auto id = m["id"].get<std::int64_t>();
                answered_fn hook;
                {
                    std::lock_guard g(mutex_);
                    if (auto h = hooks_.find(id); h != hooks_.end()) {
                        hook = std::move(h->second);
                        hooks_.erase(h);
                    }
                }
                if (hook && m.contains("result")) hook(m["result"]);
                std::lock_guard g(mutex_);
                answers_[id] = std::move(m);
                answered_.notify_all();
            } else if (m.contains("method") && m["method"].is_string() && notify_) {
                notify_(m["method"].get<std::string>(), m.value("params", Json::object()));
            }
        }
    }
    std::lock_guard g(mutex_);
    ended_ = true;
    answered_.notify_all();
}

std::expected<Json, std::string> Client::request(std::string_view method, const Json& params,
                                                 std::chrono::milliseconds limit, answered_fn answered) {
    std::int64_t id;
    {
        std::lock_guard g(mutex_);
        if (ended_) return std::unexpected(std::string("the test session has ended"));
        id = next_++;
        if (answered) hooks_[id] = std::move(answered);
    }
    const Json m{{"jsonrpc", "2.0"}, {"id", id}, {"method", std::string(method)}, {"params", params}};
    if (auto w = proc_.write(m.dump() + "\n"); !w) return std::unexpected(std::string("the test session does not read"));
    std::unique_lock l(mutex_);
    if (!answered_.wait_for(l, limit, [&] { return ended_ || answers_.contains(id); })) {
        hooks_.erase(id);
        return std::unexpected(std::format("the test session did not answer {} in time", method));
    }
    if (!answers_.contains(id)) return std::unexpected(std::string("the test session has ended"));
    auto a = std::move(answers_[id]);
    answers_.erase(id);
    if (a.contains("error")) return std::unexpected(a["error"].value("message", std::string("the session refused it")));
    return a.value("result", Json::object());
}

}  // namespace mcppide::testsession
