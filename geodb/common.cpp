#include "geodb/common.hpp"

#include <cstring>
#include <string>
#include <stdexcept>
#include <iostream>

namespace geodb {

void unreachable(const char* msg) {
    std::cerr << "Unreachable code executed";
    if (msg) {
        std::cerr << ": " << msg;
    }
    std::cerr << "." << std::endl;
    std::abort();
}

void assertion_failed_impl(const char* file, int line,
                           const char* condition, const char* message)
{
    std::cerr << "Assertion `" << condition << "` failed";
    if (message && std::strlen(message) > 0) {
        std::cerr << ": " << message;
    }
    std::cerr << ".\n";
    std::cerr << "    (in " << file << ":" << line << ")"
              << std::endl;
    std::abort();
}

} // namespace geodb
