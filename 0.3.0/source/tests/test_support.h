#pragma once

#include <iostream>
#include <stdexcept>
#include <string>

namespace drivelab::test {

class Failure final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

inline void check(bool condition, const char* expression, const char* file, int line) {
    if (condition) return;
    throw Failure(std::string(file) + ':' + std::to_string(line) +
                  " check failed: " + expression);
}

template <typename Function>
int run(Function function) {
    try {
        function();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}

}  // namespace drivelab::test

#define DL_CHECK(expression) \
    ::drivelab::test::check(static_cast<bool>(expression), #expression, __FILE__, __LINE__)
