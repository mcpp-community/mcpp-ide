// A child process with its standard output and standard error on pipes: mcpp,
// run for a job or a query. The backend's own standard streams carry the
// protocol, so a child never inherits them; its input is the null device.
//
// A CHILD IS A GROUP. `terminate` asks the child and everything it started to
// end (SIGTERM to the process group on POSIX, where mcpp writes its result
// record `cancelled`); `kill` ends the group without asking. On Windows the
// child is put in a job object, and both end the job: a console control event
// cannot reach a program that shares no console with this one, which is the
// usual case for a backend an editor started.
module;
#if defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#else
#  if defined(__APPLE__)
#    include <mach-o/dyld.h>
#  endif
#  include <fcntl.h>
#  include <poll.h>
#  include <signal.h>
#  include <spawn.h>
#  include <sys/wait.h>
#  include <unistd.h>
#  include <errno.h>
extern char** environ;
#endif

export module mcppide.platform.process;

import std;

export namespace mcppide::platform {

struct SpawnOptions {
    std::filesystem::path program;                 // absolute, or a name looked up on PATH
    std::vector<std::string> arguments;            // without argv[0]
    std::filesystem::path workDirectory;           // empty: this process's
    // Variables set (or, with an empty optional, removed) on top of this
    // process's environment.
    std::vector<std::pair<std::string, std::optional<std::string>>> environment;
};

class Process {
public:
    Process() = default;
    Process(Process&& other) noexcept;
    Process& operator=(Process&& other) noexcept;
    Process(const Process&) = delete;
    Process& operator=(const Process&) = delete;
    ~Process();

    static std::expected<Process, std::string> spawn(const SpawnOptions& options);

    bool valid() const;
    long pid() const { return pid_; }
    // Blocks until bytes arrive; an empty result means the stream ended.
    std::string read_output();
    std::string read_error();
    // The exit status; 128 + N for a POSIX child ended by signal N.
    int wait();
    // nullopt while it runs.
    std::optional<int> try_wait();
    void terminate();
    void kill();

private:
#if defined(_WIN32)
    void* process_ = nullptr;
    void* job_ = nullptr;
    void* out_ = nullptr;
    void* err_ = nullptr;
#else
    int out_ = -1;
    int err_ = -1;
#endif
    long pid_ = 0;
    std::optional<int> status_;
    std::mutex waitMutex_;
};

// Runs a program to its end and returns what it wrote. Both streams are read
// at once, so a child that fills one pipe cannot stall on it.
struct RunResult {
    int exitCode = -1;
    std::string output;
    std::string error;
};
std::expected<RunResult, std::string> run(const SpawnOptions& options);

// The program a name resolves to on PATH (with PATHEXT on Windows), or nullopt.
std::optional<std::filesystem::path> find_on_path(std::string_view name);
std::optional<std::string> environment_variable(std::string_view name);
// The directory of this program's own executable.
std::filesystem::path self_directory();

}  // namespace mcppide::platform

namespace mcppide::platform {

Process::Process(Process&& other) noexcept { *this = std::move(other); }

Process& Process::operator=(Process&& other) noexcept {
    if (this == &other) return *this;
#if defined(_WIN32)
    std::swap(process_, other.process_);
    std::swap(job_, other.job_);
    std::swap(out_, other.out_);
    std::swap(err_, other.err_);
#else
    std::swap(out_, other.out_);
    std::swap(err_, other.err_);
#endif
    std::swap(pid_, other.pid_);
    std::swap(status_, other.status_);
    return *this;
}

std::optional<std::string> environment_variable(std::string_view name) {
#if defined(_WIN32)
    std::wstring wname(name.begin(), name.end());
    const DWORD need = ::GetEnvironmentVariableW(wname.c_str(), nullptr, 0);
    if (need == 0) return std::nullopt;
    std::wstring value(need, L'\0');
    const DWORD got = ::GetEnvironmentVariableW(wname.c_str(), value.data(), need);
    value.resize(got);
    return std::filesystem::path(value).string();
#else
    if (const char* v = std::getenv(std::string(name).c_str())) return std::string(v);
    return std::nullopt;
#endif
}

std::optional<std::filesystem::path> find_on_path(std::string_view name) {
    std::filesystem::path given(name);
    std::error_code ec;
    if (given.has_parent_path()) {
        if (std::filesystem::is_regular_file(given, ec)) return given;
        return std::nullopt;
    }
    const auto path = environment_variable("PATH").value_or("");
#if defined(_WIN32)
    constexpr char separator = ';';
    std::vector<std::string> extensions{""};
    auto pathext = environment_variable("PATHEXT").value_or(".COM;.EXE;.BAT;.CMD");
    for (std::size_t start = 0; start <= pathext.size();) {
        auto end = pathext.find(';', start);
        if (end == std::string::npos) end = pathext.size();
        if (end > start) extensions.push_back(pathext.substr(start, end - start));
        start = end + 1;
    }
#else
    constexpr char separator = ':';
    std::vector<std::string> extensions{""};
#endif
    for (std::size_t start = 0; start <= path.size();) {
        auto end = path.find(separator, start);
        if (end == std::string::npos) end = path.size();
        if (end > start) {
            const std::filesystem::path dir = path.substr(start, end - start);
            for (const auto& ext : extensions) {
                auto candidate = dir / (std::string(name) + ext);
                if (std::filesystem::is_regular_file(candidate, ec)) return candidate;
            }
        }
        start = end + 1;
    }
    return std::nullopt;
}

std::filesystem::path self_directory() {
#if defined(_WIN32)
    std::wstring buffer(32768, L'\0');
    const DWORD n = ::GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    buffer.resize(n);
    return std::filesystem::path(buffer).parent_path();
#elif defined(__APPLE__)
    std::uint32_t size = 0;
    ::_NSGetExecutablePath(nullptr, &size);
    std::string buffer(size, '\0');
    if (::_NSGetExecutablePath(buffer.data(), &size) != 0) return {};
    std::error_code ec;
    return std::filesystem::weakly_canonical(std::filesystem::path(buffer.c_str()), ec).parent_path();
#else
    std::error_code ec;
    return std::filesystem::read_symlink("/proc/self/exe", ec).parent_path();
#endif
}

#if defined(_WIN32)

namespace {
std::wstring widen(std::string_view s) {
    if (s.empty()) return {};
    const int n = ::MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring out(static_cast<std::size_t>(n), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), n);
    return out;
}
// CommandLineToArgvW's rules, inverted.
std::wstring quote(const std::wstring& arg) {
    if (!arg.empty() && arg.find_first_of(L" \t\n\v\"") == std::wstring::npos) return arg;
    std::wstring out = L"\"";
    for (std::size_t i = 0;; ++i) {
        std::size_t backslashes = 0;
        while (i < arg.size() && arg[i] == L'\\') { ++i; ++backslashes; }
        if (i == arg.size()) { out.append(backslashes * 2, L'\\'); break; }
        if (arg[i] == L'"') { out.append(backslashes * 2 + 1, L'\\'); out.push_back(L'"'); }
        else { out.append(backslashes, L'\\'); out.push_back(arg[i]); }
    }
    out.push_back(L'"');
    return out;
}
std::wstring environment_block(const SpawnOptions& options) {
    std::map<std::wstring, std::wstring, std::less<>> vars;
    std::map<std::wstring, std::wstring, std::less<>> upperToName;
    if (wchar_t* block = ::GetEnvironmentStringsW()) {
        for (wchar_t* p = block; *p; p += std::wcslen(p) + 1) {
            std::wstring entry(p);
            const auto eq = entry.find(L'=', 1);
            if (eq == std::wstring::npos) continue;
            auto name = entry.substr(0, eq);
            auto upper = name;
            for (auto& c : upper) c = static_cast<wchar_t>(std::towupper(c));
            upperToName[upper] = name;
            vars[name] = entry.substr(eq + 1);
        }
        ::FreeEnvironmentStringsW(block);
    }
    for (const auto& [name, value] : options.environment) {
        auto wname = widen(name);
        auto upper = wname;
        for (auto& c : upper) c = static_cast<wchar_t>(std::towupper(c));
        if (auto it = upperToName.find(upper); it != upperToName.end()) vars.erase(it->second);
        if (value) { vars[wname] = widen(*value); upperToName[upper] = wname; }
    }
    std::wstring out;
    for (const auto& [name, value] : vars) { out += name; out += L'='; out += value; out.push_back(L'\0'); }
    out.push_back(L'\0');
    return out;
}
std::string read_handle(void* h) {
    if (!h) return {};
    std::string buffer(65536, '\0');
    DWORD got = 0;
    if (!::ReadFile(static_cast<HANDLE>(h), buffer.data(), static_cast<DWORD>(buffer.size()), &got, nullptr) || got == 0)
        return {};
    buffer.resize(got);
    return buffer;
}
}  // namespace

Process::~Process() {
    if (process_ && !status_) { kill(); (void)wait(); }
    for (void* h : {process_, job_, out_, err_}) if (h) ::CloseHandle(static_cast<HANDLE>(h));
}

bool Process::valid() const { return process_ != nullptr; }

std::expected<Process, std::string> Process::spawn(const SpawnOptions& options) {
    auto program = options.program;
    if (!program.has_parent_path())
        if (auto found = find_on_path(program.string())) program = *found;
    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
    HANDLE outRead = nullptr, outWrite = nullptr, errRead = nullptr, errWrite = nullptr;
    if (!::CreatePipe(&outRead, &outWrite, &sa, 0) || !::CreatePipe(&errRead, &errWrite, &sa, 0))
        return std::unexpected(std::string("cannot create a pipe"));
    ::SetHandleInformation(outRead, HANDLE_FLAG_INHERIT, 0);
    ::SetHandleInformation(errRead, HANDLE_FLAG_INHERIT, 0);
    HANDLE nul = ::CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, nullptr);

    STARTUPINFOEXW si{};
    si.StartupInfo.cb = sizeof(si);
    si.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    si.StartupInfo.hStdInput = nul;
    si.StartupInfo.hStdOutput = outWrite;
    si.StartupInfo.hStdError = errWrite;
    // Only these three handles reach the child: the backend's own protocol
    // pipes must not be held open by a build.
    HANDLE inherited[3] = {nul, outWrite, errWrite};
    SIZE_T attrSize = 0;
    ::InitializeProcThreadAttributeList(nullptr, 1, 0, &attrSize);
    std::vector<char> attrBuffer(attrSize);
    si.lpAttributeList = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attrBuffer.data());
    ::InitializeProcThreadAttributeList(si.lpAttributeList, 1, 0, &attrSize);
    ::UpdateProcThreadAttribute(si.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherited,
                                sizeof(inherited), nullptr, nullptr);

    std::wstring command = quote(program.wstring());
    for (const auto& a : options.arguments) { command += L' '; command += quote(widen(a)); }
    auto env = environment_block(options);
    const std::wstring cwd = options.workDirectory.empty() ? std::wstring{} : options.workDirectory.wstring();
    PROCESS_INFORMATION pi{};
    const BOOL ok = ::CreateProcessW(program.wstring().c_str(), command.data(), nullptr, nullptr, TRUE,
                                     CREATE_UNICODE_ENVIRONMENT | CREATE_NO_WINDOW | EXTENDED_STARTUPINFO_PRESENT |
                                         CREATE_SUSPENDED | CREATE_NEW_PROCESS_GROUP,
                                     env.data(), cwd.empty() ? nullptr : cwd.c_str(), &si.StartupInfo, &pi);
    const DWORD error = ::GetLastError();
    ::DeleteProcThreadAttributeList(si.lpAttributeList);
    ::CloseHandle(outWrite);
    ::CloseHandle(errWrite);
    if (nul != INVALID_HANDLE_VALUE) ::CloseHandle(nul);
    if (!ok) {
        ::CloseHandle(outRead);
        ::CloseHandle(errRead);
        return std::unexpected(std::format("cannot start {} (error {})", program.string(), error));
    }
    HANDLE job = ::CreateJobObjectW(nullptr, nullptr);
    if (job) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION info{};
        info.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        ::SetInformationJobObject(job, JobObjectExtendedLimitInformation, &info, sizeof(info));
        ::AssignProcessToJobObject(job, pi.hProcess);
    }
    ::ResumeThread(pi.hThread);
    ::CloseHandle(pi.hThread);
    Process p;
    p.process_ = pi.hProcess;
    p.job_ = job;
    p.out_ = outRead;
    p.err_ = errRead;
    p.pid_ = static_cast<long>(pi.dwProcessId);
    return p;
}

std::string Process::read_output() { return read_handle(out_); }
std::string Process::read_error() { return read_handle(err_); }

int Process::wait() {
    std::lock_guard lock(waitMutex_);
    if (status_) return *status_;
    ::WaitForSingleObject(static_cast<HANDLE>(process_), INFINITE);
    DWORD code = 0;
    ::GetExitCodeProcess(static_cast<HANDLE>(process_), &code);
    status_ = static_cast<int>(code);
    return *status_;
}

std::optional<int> Process::try_wait() {
    std::lock_guard lock(waitMutex_);
    if (status_) return status_;
    if (::WaitForSingleObject(static_cast<HANDLE>(process_), 0) != WAIT_OBJECT_0) return std::nullopt;
    DWORD code = 0;
    ::GetExitCodeProcess(static_cast<HANDLE>(process_), &code);
    status_ = static_cast<int>(code);
    return status_;
}

void Process::terminate() { kill(); }

void Process::kill() {
    if (job_) ::TerminateJobObject(static_cast<HANDLE>(job_), 130);
    else if (process_) ::TerminateProcess(static_cast<HANDLE>(process_), 130);
}

#else  // POSIX

namespace {
std::string read_fd(int fd) {
    if (fd < 0) return {};
    std::string buffer(65536, '\0');
    for (;;) {
        const auto n = ::read(fd, buffer.data(), buffer.size());
        if (n > 0) { buffer.resize(static_cast<std::size_t>(n)); return buffer; }
        if (n < 0 && errno == EINTR) continue;
        return {};
    }
}
}  // namespace

Process::~Process() {
    if (pid_ > 0 && !status_) { kill(); (void)wait(); }
    if (out_ >= 0) ::close(out_);
    if (err_ >= 0) ::close(err_);
}

bool Process::valid() const { return pid_ > 0; }

std::expected<Process, std::string> Process::spawn(const SpawnOptions& options) {
    auto program = options.program;
    if (!program.has_parent_path()) {
        auto found = find_on_path(program.string());
        if (!found) return std::unexpected(std::format("{} is not on PATH", program.string()));
        program = *found;
    }
    int outPipe[2], errPipe[2];
    if (::pipe(outPipe) != 0) return std::unexpected(std::string("cannot create a pipe"));
    if (::pipe(errPipe) != 0) { ::close(outPipe[0]); ::close(outPipe[1]); return std::unexpected(std::string("cannot create a pipe")); }
    for (int fd : {outPipe[0], errPipe[0]}) ::fcntl(fd, F_SETFD, FD_CLOEXEC);

    posix_spawn_file_actions_t actions;
    ::posix_spawn_file_actions_init(&actions);
    ::posix_spawn_file_actions_addopen(&actions, 0, "/dev/null", O_RDONLY, 0);
    ::posix_spawn_file_actions_adddup2(&actions, outPipe[1], 1);
    ::posix_spawn_file_actions_adddup2(&actions, errPipe[1], 2);
    ::posix_spawn_file_actions_addclose(&actions, outPipe[1]);
    ::posix_spawn_file_actions_addclose(&actions, errPipe[1]);
    std::string cwd = options.workDirectory.string();
#if defined(__GLIBC__) || defined(__APPLE__)
    if (!cwd.empty()) ::posix_spawn_file_actions_addchdir_np(&actions, cwd.c_str());
#endif
    posix_spawnattr_t attr;
    ::posix_spawnattr_init(&attr);
    ::posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETPGROUP | POSIX_SPAWN_SETSIGDEF | POSIX_SPAWN_SETSIGMASK);
    ::posix_spawnattr_setpgroup(&attr, 0);
    sigset_t defaults, empty;
    ::sigemptyset(&empty);
    ::sigemptyset(&defaults);
    for (int s : {SIGINT, SIGTERM, SIGHUP, SIGPIPE, SIGCHLD}) ::sigaddset(&defaults, s);
    ::posix_spawnattr_setsigdefault(&attr, &defaults);
    ::posix_spawnattr_setsigmask(&attr, &empty);

    // The environment: this process's, with the overrides applied.
    std::vector<std::string> env;
    for (char** e = environ; e && *e; ++e) {
        std::string_view entry(*e);
        const auto eq = entry.find('=');
        const auto name = entry.substr(0, eq);
        if (std::ranges::any_of(options.environment, [&](const auto& kv) { return kv.first == name; })) continue;
        env.emplace_back(entry);
    }
    for (const auto& [name, value] : options.environment)
        if (value) env.push_back(name + "=" + *value);
    std::vector<char*> envp;
    for (auto& e : env) envp.push_back(e.data());
    envp.push_back(nullptr);

    std::vector<std::string> argvStorage{program.string()};
    for (const auto& a : options.arguments) argvStorage.push_back(a);
    std::vector<char*> argv;
    for (auto& a : argvStorage) argv.push_back(a.data());
    argv.push_back(nullptr);

    pid_t pid = 0;
    int rc = 0;
#if !(defined(__GLIBC__) || defined(__APPLE__))
    // No posix_spawn_file_actions_addchdir_np (musl before 1.2.4): change
    // directory around the spawn. The backend spawns from one thread at a time
    // (Jobs serialise starts), so the directory is ours for that moment.
    std::string previous;
    if (!cwd.empty()) {
        std::error_code ec;
        previous = std::filesystem::current_path(ec).string();
        std::filesystem::current_path(cwd, ec);
    }
#endif
    rc = ::posix_spawn(&pid, argv[0], &actions, &attr, argv.data(), envp.data());
#if !(defined(__GLIBC__) || defined(__APPLE__))
    if (!cwd.empty() && !previous.empty()) { std::error_code ec; std::filesystem::current_path(previous, ec); }
#endif
    ::posix_spawn_file_actions_destroy(&actions);
    ::posix_spawnattr_destroy(&attr);
    ::close(outPipe[1]);
    ::close(errPipe[1]);
    if (rc != 0) {
        ::close(outPipe[0]);
        ::close(errPipe[0]);
        return std::unexpected(std::format("cannot start {}: {}", program.string(), std::strerror(rc)));
    }
    Process p;
    p.pid_ = static_cast<long>(pid);
    p.out_ = outPipe[0];
    p.err_ = errPipe[0];
    return p;
}

std::string Process::read_output() { return read_fd(out_); }
std::string Process::read_error() { return read_fd(err_); }

namespace {
int decode(int status) {
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    if (WIFSIGNALED(status)) return 128 + WTERMSIG(status);
    return -1;
}
}  // namespace

int Process::wait() {
    std::lock_guard lock(waitMutex_);
    if (status_) return *status_;
    int status = 0;
    while (::waitpid(static_cast<pid_t>(pid_), &status, 0) < 0) {
        if (errno != EINTR) { status_ = -1; return -1; }
    }
    status_ = decode(status);
    return *status_;
}

std::optional<int> Process::try_wait() {
    std::lock_guard lock(waitMutex_);
    if (status_) return status_;
    int status = 0;
    const auto r = ::waitpid(static_cast<pid_t>(pid_), &status, WNOHANG);
    if (r == 0) return std::nullopt;
    status_ = r < 0 ? -1 : decode(status);
    return status_;
}

void Process::terminate() {
    if (pid_ > 0 && !status_) ::kill(-static_cast<pid_t>(pid_), SIGTERM);
}

void Process::kill() {
    if (pid_ > 0 && !status_) ::kill(-static_cast<pid_t>(pid_), SIGKILL);
}

#endif

std::expected<RunResult, std::string> run(const SpawnOptions& options) {
    auto p = Process::spawn(options);
    if (!p) return std::unexpected(p.error());
    RunResult r;
    std::jthread errorReader([&] {
        for (;;) { auto chunk = p->read_error(); if (chunk.empty()) break; r.error += chunk; }
    });
    for (;;) { auto chunk = p->read_output(); if (chunk.empty()) break; r.output += chunk; }
    errorReader.join();
    r.exitCode = p->wait();
    return r;
}

}  // namespace mcppide::platform
