// JSON-RPC 2.0 over the LSP base protocol (Content-Length framing) on the
// backend's standard streams: the one wire both adapters speak.
//
// A request runs on a thread of its own, so a long one (a library search, a
// cache listing) does not hold the next; the responses go out in the order
// they finish, as JSON-RPC allows. `$/cancelRequest` marks a request
// cancelled; a handler that can stop early asks `cancelled()`.
export module mcppide.rpc;

import std;
import nlohmann.json;
import mcpplibs.os;

export namespace mcppide::rpc {

using Json = nlohmann::json;

// The whole of `bytes` on standard output, which carries the protocol: one message at a time,
// as bytes (mcpplibs.os's streams translate nothing), or false once the output is gone.
inline bool write_stdout(std::string_view bytes) {
    static std::mutex lock;
    std::lock_guard guard(lock);
    if (!mcpplibs::os::stdio::write_output(bytes)) return false;
    (void)mcpplibs::os::stdio::flush_output();
    return true;
}

inline constexpr int PARSE_ERROR = -32700;
inline constexpr int INVALID_REQUEST = -32600;
inline constexpr int METHOD_NOT_FOUND = -32601;
inline constexpr int INVALID_PARAMS = -32602;
inline constexpr int INTERNAL_ERROR = -32603;
inline constexpr int NOT_INITIALIZED = -32002;
inline constexpr int REQUEST_FAILED = -32803;
inline constexpr int REQUEST_CANCELLED = -32800;

// A handler's failure: a code and a message the adapter may show.
struct Error {
    int code = REQUEST_FAILED;
    std::string message;
    Json data = nullptr;
};

class FrameReader {
public:
    void feed(std::string_view bytes) { buffer_.append(bytes); }
    // The next complete message body; nullopt when more bytes are needed. A
    // frame without a Content-Length is skipped.
    std::optional<std::string> next();
private:
    std::string buffer_;
};

std::string encode(const Json& message);

struct Context {
    Json id;
    std::shared_ptr<std::atomic<bool>> cancelFlag;
    bool cancelled() const { return cancelFlag && cancelFlag->load(); }
};

using Handler = std::function<std::expected<Json, Error>(const Json& params, const Context& ctx)>;
using NotificationHandler = std::function<void(const Json& params)>;

class Server {
public:
    // Output goes to `write`, which is the backend's standard output in
    // production and a capture in the tests.
    explicit Server(std::function<bool(std::string_view)> write = write_stdout);

    void on(std::string method, Handler handler);
    void on_notification(std::string method, NotificationHandler handler);
    // Sends a notification to the adapter.
    void notify(std::string_view method, Json params);
    // Handles one message body; requests run on threads of their own.
    void dispatch(std::string_view body);
    // Reads standard input until it ends or `exit` arrives; returns the exit
    // status the protocol asks for (0 after shutdown, 1 otherwise).
    int serve();
    // Waits for the requests in flight.
    void drain();

    bool shutdown_requested() const { return shutdown_.load(); }
    void set_initialized(bool v) { initialized_.store(v); }

private:
    void reply(const Json& id, std::expected<Json, Error> result);
    std::function<bool(std::string_view)> write_;
    std::map<std::string, Handler, std::less<>> handlers_;
    std::map<std::string, NotificationHandler, std::less<>> notifications_;
    std::mutex inflightMutex_;
    std::map<std::string, std::shared_ptr<std::atomic<bool>>> inflight_;
    struct Worker {
        std::shared_ptr<std::atomic<bool>> done;
        std::jthread thread;
    };
    std::list<Worker> workers_;
    std::mutex workersMutex_;
    std::atomic<bool> shutdown_{false};
    std::atomic<bool> exit_{false};
    std::atomic<bool> initialized_{false};
};

}  // namespace mcppide::rpc

namespace mcppide::rpc {

std::optional<std::string> FrameReader::next() {
    for (;;) {
        const auto headerEnd = buffer_.find("\r\n\r\n");
        if (headerEnd == std::string::npos) return std::nullopt;
        std::optional<std::size_t> length;
        std::string_view headers(buffer_.data(), headerEnd);
        std::size_t pos = 0;
        while (pos <= headers.size()) {
            auto end = headers.find("\r\n", pos);
            if (end == std::string_view::npos) end = headers.size();
            auto line = headers.substr(pos, end - pos);
            constexpr std::string_view key = "content-length:";
            if (line.size() > key.size()) {
                std::string lower(line.substr(0, key.size()));
                for (auto& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                if (lower == key) {
                    auto value = line.substr(key.size());
                    while (!value.empty() && value.front() == ' ') value.remove_prefix(1);
                    std::size_t n = 0;
                    if (std::from_chars(value.data(), value.data() + value.size(), n).ec == std::errc{}) length = n;
                }
            }
            pos = end + 2;
        }
        if (!length) { buffer_.erase(0, headerEnd + 4); continue; }
        if (buffer_.size() < headerEnd + 4 + *length) return std::nullopt;
        std::string body = buffer_.substr(headerEnd + 4, *length);
        buffer_.erase(0, headerEnd + 4 + *length);
        return body;
    }
}

std::string encode(const Json& message) {
    const auto body = message.dump(-1, ' ', false, Json::error_handler_t::replace);
    return std::format("Content-Length: {}\r\n\r\n{}", body.size(), body);
}

Server::Server(std::function<bool(std::string_view)> write) : write_(std::move(write)) {}

void Server::on(std::string method, Handler handler) { handlers_[std::move(method)] = std::move(handler); }

void Server::on_notification(std::string method, NotificationHandler handler) {
    notifications_[std::move(method)] = std::move(handler);
}

void Server::notify(std::string_view method, Json params) {
    (void)write_(encode(Json{{"jsonrpc", "2.0"}, {"method", method}, {"params", std::move(params)}}));
}

void Server::reply(const Json& id, std::expected<Json, Error> result) {
    Json message{{"jsonrpc", "2.0"}, {"id", id}};
    if (result) message["result"] = std::move(*result);
    else {
        Json error{{"code", result.error().code}, {"message", result.error().message}};
        if (!result.error().data.is_null()) error["data"] = result.error().data;
        message["error"] = std::move(error);
    }
    (void)write_(encode(message));
}

void Server::dispatch(std::string_view body) {
    auto message = Json::parse(body, nullptr, false);
    if (message.is_discarded() || !message.is_object()) {
        reply(nullptr, std::unexpected(Error{PARSE_ERROR, "the message is not JSON"}));
        return;
    }
    const bool hasId = message.contains("id") && !message["id"].is_null();
    const std::string method = message.value("method", "");
    const Json params = message.contains("params") ? message["params"] : Json::object();
    if (!hasId) {
        if (method == "exit") { exit_.store(true); return; }
        if (method == "$/cancelRequest") {
            const auto key = params.contains("id") ? params["id"].dump() : std::string{};
            std::lock_guard lock(inflightMutex_);
            if (auto it = inflight_.find(key); it != inflight_.end()) it->second->store(true);
            return;
        }
        if (auto it = notifications_.find(method); it != notifications_.end()) it->second(params);
        return;
    }
    const Json id = message["id"];
    if (method.empty()) { reply(id, std::unexpected(Error{INVALID_REQUEST, "a request names no method"})); return; }
    if (method == "shutdown") { drain(); shutdown_.store(true); reply(id, Json(nullptr)); return; }
    if (!initialized_.load() && method != "initialize") {
        reply(id, std::unexpected(Error{NOT_INITIALIZED, "initialize first"}));
        return;
    }
    auto it = handlers_.find(method);
    if (it == handlers_.end()) {
        reply(id, std::unexpected(Error{METHOD_NOT_FOUND, std::format("unknown method {}", method)}));
        return;
    }
    auto flag = std::make_shared<std::atomic<bool>>(false);
    {
        std::lock_guard lock(inflightMutex_);
        inflight_[id.dump()] = flag;
    }
    Handler handler = it->second;
    auto run = [this, handler, params, id, flag] {
        std::expected<Json, Error> result = std::unexpected(Error{INTERNAL_ERROR, "the handler did not answer"});
        try {
            result = handler(params, Context{id, flag});
        } catch (const std::exception& e) {
            result = std::unexpected(Error{INTERNAL_ERROR, e.what()});
        }
        if (flag->load() && !result) result = std::unexpected(Error{REQUEST_CANCELLED, "cancelled"});
        reply(id, std::move(result));
        std::lock_guard lock(inflightMutex_);
        inflight_.erase(id.dump());
    };
    // `initialize` answers before anything else is read: the adapter waits
    // for it, and the capabilities it carries decide what the adapter sends.
    if (method == "initialize") { run(); return; }
    auto done = std::make_shared<std::atomic<bool>>(false);
    std::lock_guard lock(workersMutex_);
    // Finished workers are joined here, so the list holds the requests in
    // flight and not every request the session has seen.
    std::erase_if(workers_, [](Worker& w) { return w.done->load(); });
    workers_.push_back(Worker{done, std::jthread([run = std::move(run), done] { run(); done->store(true); })});
}

void Server::drain() {
    std::list<Worker> pending;
    {
        std::lock_guard lock(workersMutex_);
        pending.swap(workers_);
    }
    for (auto& w : pending) if (w.thread.joinable()) w.thread.join();
}

int Server::serve() {
    FrameReader reader;
    while (!exit_.load()) {
        auto chunk = mcpplibs::os::stdio::read_input().value_or(std::string{});
        if (chunk.empty()) break;
        reader.feed(chunk);
        while (auto body = reader.next()) {
            dispatch(*body);
            if (exit_.load()) break;
        }
    }
    drain();
    return shutdown_.load() ? 0 : 1;
}

}  // namespace mcppide::rpc
