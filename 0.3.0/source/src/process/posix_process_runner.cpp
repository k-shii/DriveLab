#include "process/posix_process_runner.h"

#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <thread>
#include <utility>
#include <vector>

namespace drivelab {
namespace {

void closeFd(int& descriptor) {
    if (descriptor >= 0) {
        close(descriptor);
        descriptor = -1;
    }
}

bool makePipe(bool enabled, int descriptors[2]) {
    descriptors[0] = -1;
    descriptors[1] = -1;
    return !enabled || pipe(descriptors) == 0;
}

void makeNonBlocking(int descriptor) {
    if (descriptor < 0) return;
    int flags = fcntl(descriptor, F_GETFL, 0);
    if (flags >= 0) fcntl(descriptor, F_SETFL, flags | O_NONBLOCK);
}

void drain(int& descriptor, std::string& output) {
    if (descriptor < 0) return;
    char buffer[4096];
    while (true) {
        ssize_t count = read(descriptor, buffer, sizeof(buffer));
        if (count > 0) {
            output.append(buffer, static_cast<std::size_t>(count));
            continue;
        }
        if (count == 0) {
            closeFd(descriptor);
            return;
        }
        if (errno == EINTR) continue;
        if (errno == EAGAIN || errno == EWOULDBLOCK) return;
        closeFd(descriptor);
        return;
    }
}

void waitForOutput(int stdout_fd, int stderr_fd, int timeout_ms) {
    std::vector<pollfd> descriptors;
    if (stdout_fd >= 0) descriptors.push_back({stdout_fd, POLLIN | POLLHUP, 0});
    if (stderr_fd >= 0) descriptors.push_back({stderr_fd, POLLIN | POLLHUP, 0});
    if (descriptors.empty()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(timeout_ms));
        return;
    }
    poll(descriptors.data(), descriptors.size(), timeout_ms);
}

void terminateProcessGroup(pid_t pid, int signal_number) {
    if (kill(-pid, signal_number) != 0) kill(pid, signal_number);
}

Error processError(std::string message) {
    return {ErrorCode::ProcessFailure, "PosixProcessRunner", std::move(message)};
}

}  // namespace

Result<ProcessResult> PosixProcessRunner::run(const ProcessSpec& spec) {
    if (spec.executable.empty()) {
        return Result<ProcessResult>::failure({
            ErrorCode::InvalidArgument,
            "PosixProcessRunner",
            "Process executable must not be empty"
        });
    }

    int stdout_pipe[2];
    int stderr_pipe[2];
    if (!makePipe(spec.capture_stdout, stdout_pipe)) {
        return Result<ProcessResult>::failure(processError(
            "Unable to create stdout pipe: " + std::string(std::strerror(errno))));
    }
    if (!makePipe(spec.capture_stderr, stderr_pipe)) {
        closeFd(stdout_pipe[0]);
        closeFd(stdout_pipe[1]);
        return Result<ProcessResult>::failure(processError(
            "Unable to create stderr pipe: " + std::string(std::strerror(errno))));
    }

    pid_t pid = fork();
    if (pid < 0) {
        closeFd(stdout_pipe[0]);
        closeFd(stdout_pipe[1]);
        closeFd(stderr_pipe[0]);
        closeFd(stderr_pipe[1]);
        return Result<ProcessResult>::failure(processError(
            "Unable to fork process: " + std::string(std::strerror(errno))));
    }

    if (pid == 0) {
        setpgid(0, 0);
        if (spec.capture_stdout) dup2(stdout_pipe[1], STDOUT_FILENO);
        if (spec.capture_stderr) dup2(stderr_pipe[1], STDERR_FILENO);
        closeFd(stdout_pipe[0]);
        closeFd(stdout_pipe[1]);
        closeFd(stderr_pipe[0]);
        closeFd(stderr_pipe[1]);

        for (const auto& [name, value] : spec.environment) {
            setenv(name.c_str(), value.c_str(), 1);
        }

        std::vector<char*> arguments;
        arguments.reserve(spec.arguments.size() + 2);
        arguments.push_back(const_cast<char*>(spec.executable.c_str()));
        for (const std::string& argument : spec.arguments) {
            arguments.push_back(const_cast<char*>(argument.c_str()));
        }
        arguments.push_back(nullptr);
        execvp(spec.executable.c_str(), arguments.data());
        _exit(127);
    }

    setpgid(pid, pid);
    closeFd(stdout_pipe[1]);
    closeFd(stderr_pipe[1]);
    makeNonBlocking(stdout_pipe[0]);
    makeNonBlocking(stderr_pipe[0]);

    ProcessResult result;
    int wait_status = 0;
    bool child_finished = false;
    const auto started = std::chrono::steady_clock::now();
    while (!child_finished) {
        drain(stdout_pipe[0], result.stdout_text);
        drain(stderr_pipe[0], result.stderr_text);

        pid_t waited = waitpid(pid, &wait_status, WNOHANG);
        if (waited == pid) {
            child_finished = true;
            break;
        }
        if (waited < 0 && errno != EINTR) {
            closeFd(stdout_pipe[0]);
            closeFd(stderr_pipe[0]);
            return Result<ProcessResult>::failure(processError(
                "Unable to wait for child process: " + std::string(std::strerror(errno))));
        }

        if (spec.timeout.count() > 0 &&
            std::chrono::steady_clock::now() - started >= spec.timeout) {
            result.timed_out = true;
            terminateProcessGroup(pid, SIGTERM);
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            waited = waitpid(pid, &wait_status, WNOHANG);
            if (waited != pid) {
                terminateProcessGroup(pid, SIGKILL);
                while (waitpid(pid, &wait_status, 0) < 0 && errno == EINTR) {}
            }
            child_finished = true;
            break;
        }

        waitForOutput(stdout_pipe[0], stderr_pipe[0], 20);
    }

    for (int attempt = 0;
         attempt < 20 && (stdout_pipe[0] >= 0 || stderr_pipe[0] >= 0);
         ++attempt) {
        drain(stdout_pipe[0], result.stdout_text);
        drain(stderr_pipe[0], result.stderr_text);
        if (stdout_pipe[0] >= 0 || stderr_pipe[0] >= 0) {
            waitForOutput(stdout_pipe[0], stderr_pipe[0], 5);
        }
    }
    closeFd(stdout_pipe[0]);
    closeFd(stderr_pipe[0]);

    if (WIFEXITED(wait_status)) {
        result.exit_code = WEXITSTATUS(wait_status);
    } else if (WIFSIGNALED(wait_status)) {
        result.terminating_signal = WTERMSIG(wait_status);
        result.exit_code = 128 + result.terminating_signal;
    }
    return Result<ProcessResult>::success(std::move(result));
}

}  // namespace drivelab
