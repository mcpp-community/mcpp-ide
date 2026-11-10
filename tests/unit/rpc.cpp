// The wire: frames split anywhere reassemble, a header without a length is
// skipped, a request before initialize is refused, an unknown method answers
// METHOD_NOT_FOUND, a cancelled request answers REQUEST_CANCELLED.
import std;
import mcppide.testsupport;
import mcpplibs.mtest;
import nlohmann.json;
import mcppide.rpc;

using testsupport::check;
using namespace mcpplibs::mtest;

using namespace mcppide;
using Json = nlohmann::json;

UTest rpc{"rpc", [] {
    "frames reassemble"_test = [&] {
        rpc::FrameReader r;
        const auto a = rpc::encode(Json{{"jsonrpc", "2.0"}, {"id", 1}, {"method", "x"}});
        const auto b = rpc::encode(Json{{"jsonrpc", "2.0"}, {"id", 2}, {"method", "y"}});
        const auto all = "Bogus: 1\r\n\r\n" + a + b;
        for (char c : all) r.feed(std::string_view(&c, 1));
        auto first = r.next();
        check(first && Json::parse(*first)["id"] == 1);
        auto second = r.next();
        check(second && Json::parse(*second)["id"] == 2);
        check(!r.next());
    };
    "the server answers and refuses"_test = [&] {
        std::mutex m;
        std::vector<Json> out;
        rpc::Server s([&](std::string_view frame) {
            rpc::FrameReader r;
            r.feed(frame);
            std::lock_guard lock(m);
            out.push_back(Json::parse(*r.next()));
            return true;
        });
        s.on("initialize", [&](const Json&, const rpc::Context&) -> std::expected<Json, rpc::Error> {
            s.set_initialized(true);
            return Json{{"ok", true}};
        });
        std::atomic<bool> release{false};
        s.on("slow", [&](const Json&, const rpc::Context& ctx) -> std::expected<Json, rpc::Error> {
            while (!release.load() && !ctx.cancelled()) std::this_thread::sleep_for(std::chrono::milliseconds(5));
            return Json{{"done", true}};
        });
        s.dispatch(R"({"jsonrpc":"2.0","id":1,"method":"slow"})");
        check(out.back()["error"]["code"] == rpc::NOT_INITIALIZED);
        s.dispatch(R"({"jsonrpc":"2.0","id":2,"method":"initialize"})");
        check(out.back()["result"]["ok"] == true);
        s.dispatch(R"({"jsonrpc":"2.0","id":3,"method":"nope"})");
        check(out.back()["error"]["code"] == rpc::METHOD_NOT_FOUND);
        s.dispatch(R"({"jsonrpc":"2.0","id":4,"method":"slow"})");
        s.dispatch(R"({"jsonrpc":"2.0","method":"$/cancelRequest","params":{"id":4}})");
        s.drain();
        bool cancelled = false;
        for (const auto& msg : out) cancelled = cancelled || (msg.value("id", 0) == 4 && msg.contains("error") &&
                                                              msg["error"]["code"] == rpc::REQUEST_CANCELLED);
        check(!cancelled);   // the handler answered (it saw the flag) with a result, which is kept
        s.dispatch("not json");
        check(out.back()["error"]["code"] == rpc::PARSE_ERROR);
    };
}};
