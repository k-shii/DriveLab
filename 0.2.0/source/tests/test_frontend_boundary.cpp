#include "test_support.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <regex>
#include <string>
#include <vector>

namespace {

std::string readFile(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    DL_CHECK(input);
    return {std::istreambuf_iterator<char>(input),
            std::istreambuf_iterator<char>()};
}

std::string withoutWhitespace(const std::string& source) {
    std::string compact;
    compact.reserve(source.size());
    std::copy_if(source.begin(), source.end(), std::back_inserter(compact),
                 [](unsigned char character) {
                     return !std::isspace(character);
                 });
    return compact;
}

}  // namespace

int main() {
    return drivelab::test::run([] {
        const std::filesystem::path prototype =
            std::filesystem::path{DRIVELAB_SOURCE_DIR} / "prototype";
        DL_CHECK(std::filesystem::is_directory(prototype));

        const std::vector<std::string> forbidden = {
            "system(",
            "popen(",
            "pclose(",
            "fork(",
            "vfork(",
            "execv(",
            "execve(",
            "execvp(",
            "execl(",
            "execlp(",
            "posix_spawn(",
            "kill(",
            "waitpid(",
            "/sys/class/block/",
            "/root/drivelab-reports",
            "saveReport(",
            "<sys/wait.h>",
            "<sys/stat.h>",
            "<signal.h>"
        };
        const std::regex storage_command{
            R"(["'](lsblk|smartctl|fio|hdparm|nwipe|ioping)([[:space:]"']))"
        };

        for (const std::filesystem::directory_entry& entry :
             std::filesystem::recursive_directory_iterator(prototype)) {
            if (!entry.is_regular_file()) continue;
            const std::filesystem::path extension = entry.path().extension();
            if (extension != ".cpp" && extension != ".h" && extension != ".hpp") {
                continue;
            }

            const std::string source = readFile(entry.path());
            const std::string compact = withoutWhitespace(source);
            for (const std::string& pattern : forbidden) {
                DL_CHECK(compact.find(pattern) == std::string::npos);
            }
            DL_CHECK(!std::regex_search(source, storage_command));
        }

        const std::string launcher = readFile(prototype / "drivelab.cpp");
        DL_CHECK(launcher.find("std::ofstream") == std::string::npos);
        DL_CHECK(launcher.find("runApplicationCli") != std::string::npos);
        DL_CHECK(launcher.find("runDemoUi") != std::string::npos);

        const std::string demo_ui = readFile(prototype / "demo_ui.cpp");
        DL_CHECK(demo_ui.find("/tmp/drivelab-demo-mouse.log") !=
                 std::string::npos);
    });
}
