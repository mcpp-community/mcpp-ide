// The backend's own standard streams: the protocol arrives on standard input
// and leaves on standard output, as bytes (Windows would otherwise translate
// line endings and corrupt every Content-Length). Writes are whole messages
// under one lock, so two threads never interleave.
module;
#if defined(_WIN32)
#  include <fcntl.h>
#  include <io.h>
#  include <stdio.h>
#else
#  include <unistd.h>
#  include <errno.h>
#endif

export module mcppide.platform.stdio;

import std;

export namespace mcppide::platform {

void binary_stdio();
// The next bytes of standard input; empty at its end.
std::string read_stdin();
// The whole of `bytes`, or false when standard output is gone.
bool write_stdout(std::string_view bytes);
void write_stderr(std::string_view bytes);

}  // namespace mcppide::platform

namespace mcppide::platform {

namespace { std::mutex g_out; std::mutex g_err; }

void binary_stdio() {
#if defined(_WIN32)
    ::_setmode(_fileno(stdin), _O_BINARY);
    ::_setmode(_fileno(stdout), _O_BINARY);
#endif
}

std::string read_stdin() {
    std::string buffer(65536, '\0');
#if defined(_WIN32)
    const int n = ::_read(0, buffer.data(), static_cast<unsigned>(buffer.size()));
    if (n <= 0) return {};
    buffer.resize(static_cast<std::size_t>(n));
    return buffer;
#else
    for (;;) {
        const auto n = ::read(0, buffer.data(), buffer.size());
        if (n > 0) { buffer.resize(static_cast<std::size_t>(n)); return buffer; }
        if (n < 0 && errno == EINTR) continue;
        return {};
    }
#endif
}

namespace {
bool write_fd(int fd, std::string_view bytes) {
    std::size_t done = 0;
    while (done < bytes.size()) {
#if defined(_WIN32)
        const int n = ::_write(fd, bytes.data() + done, static_cast<unsigned>(bytes.size() - done));
        if (n <= 0) return false;
#else
        const auto n = ::write(fd, bytes.data() + done, bytes.size() - done);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return false;
#endif
        done += static_cast<std::size_t>(n);
    }
    return true;
}
}  // namespace

bool write_stdout(std::string_view bytes) {
    std::lock_guard lock(g_out);
    return write_fd(1, bytes);
}

void write_stderr(std::string_view bytes) {
    std::lock_guard lock(g_err);
    (void)write_fd(2, bytes);
}

}  // namespace mcppide::platform
