#pragma once

#include <functional>
#include <iosfwd>
#include <string>
#include <vector>

namespace drivelab {

using DemoUiRunner = std::function<int()>;

int runDryRunBootstrap(std::ostream& output, std::ostream& error_output);
int runApplicationCli(const std::vector<std::string>& arguments,
                      std::string executable_name,
                      std::ostream& output,
                      std::ostream& error_output,
                      DemoUiRunner run_demo_ui);

}  // namespace drivelab
