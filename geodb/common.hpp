#ifndef COMMON_HPP
#define COMMON_HPP

#include <climits>
#include <cstdint>

#include <gsl/gsl_assert>

namespace geodb {

static_assert(CHAR_BIT == 8, "Byte width sanity check.");

using byte = unsigned char;

using u16 = uint16_t;
using u32 = uint32_t;
using u64 = uint64_t;

using i16 = int16_t;
using i32 = int32_t;
using i64 = int64_t;

/// Marks a section of code as unreachable, suppressing compiler warnings.
/// An error will be thrown should the code section be executed anyway.
[[noreturn]]
void unreachable(const char* msg);

/// Marks a set of arguments as unused, suppressing compiler warnings.
template<typename... Args>
void unused(Args&&...) {}

#ifndef NDEBUG

/// Similar to standard assert, but allows for a custom message.
#define geodb_assert(condition, message)                        \
    do {                                                        \
        if (!(condition)) {                                     \
            ::geodb::assertion_failed_impl(__FILE__, __LINE__,  \
                #condition, message);                           \
        }                                                       \
    } while (0)

#else

#define geodb_assert(condition, message) do { } while(0)

#endif

// Do not call directly.
void assertion_failed_impl(const char* file, int line,
                           const char* condition, const char* message);

} // namespace geodb

#endif // COMMON_HPP
