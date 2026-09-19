#include "test_support.h"

#include "process/posix_process_runner.h"

#include <chrono>

using namespace drivelab;

int main() {
    return test::run([] {
        PosixProcessRunner runner;

        ProcessSpec echo;
        echo.executable = "/bin/echo";
        echo.arguments = {"hello;this-is-not-a-shell-command"};
        Result<ProcessResult> echo_result = runner.run(echo);
        DL_CHECK(echo_result);
        DL_CHECK(echo_result.value().exit_code == 0);
        DL_CHECK(echo_result.value().stdout_text == "hello;this-is-not-a-shell-command\n");

        ProcessSpec failure;
        failure.executable = "/bin/false";
        Result<ProcessResult> failure_result = runner.run(failure);
        DL_CHECK(failure_result);
        DL_CHECK(failure_result.value().exit_code != 0);

        ProcessSpec timeout;
        timeout.executable = "/bin/sleep";
        timeout.arguments = {"1"};
        timeout.timeout = std::chrono::milliseconds(20);
        Result<ProcessResult> timeout_result = runner.run(timeout);
        DL_CHECK(timeout_result);
        DL_CHECK(timeout_result.value().timed_out);
    });
}
