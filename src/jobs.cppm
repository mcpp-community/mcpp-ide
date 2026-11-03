// Jobs: a run of mcpp that takes time and reports while it runs -- a build, a
// test, a pack, a clean, a toolchain install, `mcpp add`.
//
// ONE JOB AT A TIME PER PROJECT. A project's jobs run in the order they were
// asked for. A new job of a kind that supersedes (a build asked for again on
// save) replaces the waiting one of that kind, and the running one is
// cancelled when the request says `supersede`. Cancelling sends the polite
// signal: mcpp ends the steps it started and leaves its result record
// `cancelled` (mcpp SPEC-003 §3.7).
//
// What a job reports goes to `Sink`: the narration as it is written (mcpp's
// standard error, coloured when the adapter renders a terminal), each NDJSON
// record of a `--message-format json` stream, and the end. The adapter turns
// them into a terminal, Problems, a test tree.
export module mcppide.jobs;

import std;
import nlohmann.json;
import mcpplibs.os;
import mcppide.mcpp;

export namespace mcppide::jobs {

using Json = nlohmann::json;

enum class Kind { build, test, pack, clean, toolchain, add, command };

// What standard output carries: narration for a person, NDJSON records (a
// `--message-format json` stream), or one envelope printed at the end.
enum class Mode { narration, ndjson, envelope };

std::string_view kind_name(Kind k);
std::optional<Kind> kind_from(std::string_view s);

struct Request {
    Kind kind = Kind::command;
    std::filesystem::path root;            // the project's directory
    std::vector<std::string> args;         // mcpp's arguments, without the program
    Mode mode = Mode::narration;
    bool color = true;                     // narration for a terminal
    bool supersede = false;                // cancel a running job of this kind
    std::vector<std::string> deny;
    std::string client = "mcpp-ide";
    // Variables set (or, with no value, removed) for this run, over the backend's environment.
    std::vector<std::pair<std::string, std::optional<std::string>>> env;
    // Called once when the job has ended, however it ended, before the editors are told: what the
    // run wrote is read here (a test framework's report).
    std::function<void(std::string_view jobId, const Json& summary)> after;
};

struct Sink {
    std::function<void(std::string_view jobId, std::string_view text)> narration;
    std::function<void(std::string_view jobId, const Json& record)> record;
    std::function<void(std::string_view jobId, const Json& summary)> finished;
    std::function<void(std::string_view jobId, const Json& started)> started;
};

class Jobs {
public:
    Jobs(mcpp::Client client, Sink sink) : client_(std::move(client)), sink_(std::move(sink)) {}
    ~Jobs();

    void set_client(mcpp::Client client);
    // Queues the job; returns its id.
    std::string submit(Request request);
    // Cancels a job: a waiting one is dropped, a running one asked to end.
    bool cancel(std::string_view jobId);
    // The jobs of every project: waiting and running.
    Json list() const;
    // Waits for every job (the tests; shutdown).
    void drain();

private:
    struct Job {
        std::string id;
        Request request;
        std::string state = "waiting";   // waiting | running | done
        std::shared_ptr<mcpplibs::os::process::process> process;
        bool cancelled = false;
    };
    struct Lane {
        std::deque<std::shared_ptr<Job>> queue;
        std::shared_ptr<Job> running;
        std::jthread worker;
    };
    void run_lane(std::string key);
    void execute(const std::shared_ptr<Job>& job);
    // The request's own hook, then the editors.
    void finish(const Job& job, const Json& summary);

    mutable std::mutex mutex_;
    std::condition_variable_any wake_;
    mcpp::Client client_;
    Sink sink_;
    std::map<std::string, Lane> lanes_;
    std::uint64_t next_ = 1;
    bool stopping_ = false;
};

}  // namespace mcppide::jobs

namespace mcppide::jobs {

std::string_view kind_name(Kind k) {
    switch (k) {
        case Kind::build: return "build";
        case Kind::test: return "test";
        case Kind::pack: return "pack";
        case Kind::clean: return "clean";
        case Kind::toolchain: return "toolchain";
        case Kind::add: return "add";
        case Kind::command: return "command";
    }
    return "command";
}

std::optional<Kind> kind_from(std::string_view s) {
    for (auto k : {Kind::build, Kind::test, Kind::pack, Kind::clean, Kind::toolchain, Kind::add, Kind::command})
        if (kind_name(k) == s) return k;
    return std::nullopt;
}

Jobs::~Jobs() {
    {
        std::lock_guard lock(mutex_);
        stopping_ = true;
        for (auto& [key, lane] : lanes_) {
            lane.queue.clear();
            if (lane.running && lane.running->process) lane.running->process->terminate();
        }
    }
    wake_.notify_all();
    std::map<std::string, Lane> lanes;
    {
        std::lock_guard lock(mutex_);
        lanes.swap(lanes_);
    }
    for (auto& [key, lane] : lanes) if (lane.worker.joinable()) lane.worker.join();
}

void Jobs::set_client(mcpp::Client client) {
    std::lock_guard lock(mutex_);
    client_ = std::move(client);
}

std::string Jobs::submit(Request request) {
    auto job = std::make_shared<Job>();
    const auto key = request.root.lexically_normal().generic_string();
    {
        std::lock_guard lock(mutex_);
        job->id = std::format("job-{}", next_++);
        job->request = std::move(request);
        auto& lane = lanes_[key];
        // The same kind asked for again replaces the one still waiting: a save
        // followed by another save builds once.
        if (job->request.kind == Kind::build || job->request.kind == Kind::test) {
            std::erase_if(lane.queue, [&](const std::shared_ptr<Job>& waiting) {
                if (waiting->request.kind != job->request.kind) return false;
                waiting->cancelled = true;
                finish(*waiting, Json{{"status", "superseded"}, {"exitCode", nullptr}});
                return true;
            });
            if (job->request.supersede && lane.running && lane.running->request.kind == job->request.kind &&
                lane.running->process) {
                lane.running->cancelled = true;
                lane.running->process->terminate();
            }
        }
        lane.queue.push_back(job);
        if (!lane.worker.joinable())
            lane.worker = std::jthread([this, key] { run_lane(key); });
    }
    wake_.notify_all();
    return job->id;
}

bool Jobs::cancel(std::string_view jobId) {
    std::lock_guard lock(mutex_);
    for (auto& [key, lane] : lanes_) {
        if (lane.running && lane.running->id == jobId) {
            lane.running->cancelled = true;
            if (lane.running->process) lane.running->process->terminate();
            return true;
        }
        for (auto it = lane.queue.begin(); it != lane.queue.end(); ++it)
            if ((*it)->id == jobId) {
                finish(**it, Json{{"status", "cancelled"}, {"exitCode", nullptr}});
                lane.queue.erase(it);
                return true;
            }
    }
    return false;
}

Json Jobs::list() const {
    std::lock_guard lock(mutex_);
    Json out = Json::array();
    for (const auto& [key, lane] : lanes_) {
        auto describe = [&](const std::shared_ptr<Job>& j, std::string_view state) {
            out.push_back(Json{{"id", j->id}, {"kind", kind_name(j->request.kind)}, {"root", key},
                               {"state", state}, {"args", j->request.args}});
        };
        if (lane.running) describe(lane.running, "running");
        for (const auto& j : lane.queue) describe(j, "waiting");
    }
    return out;
}

void Jobs::drain() {
    for (;;) {
        {
            std::lock_guard lock(mutex_);
            bool busy = false;
            for (const auto& [key, lane] : lanes_) busy = busy || lane.running || !lane.queue.empty();
            if (!busy) return;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
}

void Jobs::run_lane(std::string key) {
    for (;;) {
        std::shared_ptr<Job> job;
        {
            std::unique_lock lock(mutex_);
            auto& lane = lanes_[key];
            wake_.wait(lock, [&] { return stopping_ || !lane.queue.empty(); });
            if (stopping_) return;
            job = lane.queue.front();
            lane.queue.pop_front();
            lane.running = job;
            job->state = "running";
        }
        execute(job);
        {
            std::lock_guard lock(mutex_);
            lanes_[key].running.reset();
        }
    }
}

void Jobs::execute(const std::shared_ptr<Job>& job) {
    mcpp::RunOptions o;
    o.cwd = job->request.root;
    o.args = job->request.args;
    o.deny = job->request.deny;
    o.client = job->request.client;
    o.color = job->request.color;
    o.extraEnv = job->request.env;
    mcpp::Client client;
    {
        std::lock_guard lock(mutex_);
        client = client_;
    }
    const auto started = std::chrono::steady_clock::now();
    if (sink_.started)
        sink_.started(job->id, Json{{"kind", kind_name(job->request.kind)},
                                    {"root", job->request.root.generic_string()},
                                    {"argv", [&] {
                                         Json a = Json::array({client.program().string()});
                                         for (const auto& x : job->request.args) a.push_back(x);
                                         return a;
                                     }()}});
    auto spawned = mcpplibs::os::process::process::spawn(client.spawn_options(o));
    if (!spawned) {
        const auto why = spawned.error().message();
        if (sink_.narration) sink_.narration(job->id, why + "\n");
        finish(*job, Json{{"status", "failed"}, {"exitCode", nullptr}, {"error", why}});
        return;
    }
    auto process = std::make_shared<mcpplibs::os::process::process>(std::move(*spawned));
    {
        std::lock_guard lock(mutex_);
        job->process = process;
        if (job->cancelled) process->terminate();
    }
    // Standard error: the narration, as it comes.
    std::jthread errors([&] {
        for (;;) {
            auto chunk = process->read_error().value_or(std::string{});
            if (chunk.empty()) break;
            if (sink_.narration) sink_.narration(job->id, chunk);
        }
    });
    // Standard output: narration for a person (`mcpp toolchain install`), NDJSON
    // records for a stream, or one envelope at the end (`mcpp pack`).
    Json envelope = nullptr;
    std::string pending;
    for (;;) {
        auto chunk = process->read_output().value_or(std::string{});
        if (chunk.empty()) break;
        if (job->request.mode == Mode::narration) {
            if (sink_.narration) sink_.narration(job->id, chunk);
            continue;
        }
        pending += chunk;
        if (job->request.mode == Mode::envelope) continue;
        for (auto nl = pending.find('\n'); nl != std::string::npos; nl = pending.find('\n')) {
            std::string line = pending.substr(0, nl);
            pending.erase(0, nl + 1);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty()) continue;
            auto record = Json::parse(line, nullptr, false);
            if (record.is_discarded()) {
                if (sink_.narration) sink_.narration(job->id, line + "\n");
                continue;
            }
            // The envelope closes a build stream; everything else is a record.
            if (record.is_object() && record.contains("schemaVersion") && record.value("kind", "") != "mcpp.event")
                envelope = record;
            if (sink_.record) sink_.record(job->id, record);
        }
    }
    if (job->request.mode == Mode::envelope) {
        auto doc = Json::parse(pending, nullptr, false);
        if (!doc.is_discarded()) envelope = std::move(doc);
        else if (sink_.narration) sink_.narration(job->id, pending);
        pending.clear();
    }
    if (!pending.empty() && sink_.narration) sink_.narration(job->id, pending);
    errors.join();
    // The status as a shell reports it (128 + N for a program its system ended).
    const auto ended = process->wait();
    const int code = ended ? ended->shell_code() : -1;
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started);
    std::string status = code == 0 ? "ok" : (code == 5 ? "denied" : "failed");
    {
        std::lock_guard lock(mutex_);
        if (job->cancelled) status = "cancelled";
        job->state = "done";
    }
    Json summary{{"status", status}, {"exitCode", code}, {"durationMs", ms.count()}};
    if (!envelope.is_null()) summary["envelope"] = envelope;
    finish(*job, summary);
}

void Jobs::finish(const Job& job, const Json& summary) {
    if (job.request.after) job.request.after(job.id, summary);
    if (sink_.finished) sink_.finished(job.id, summary);
}

}  // namespace mcppide::jobs
