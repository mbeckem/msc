#ifndef GEODB_COMMON_HPP
#define GEODB_COMMON_HPP

#include <climits>
#include <cstdint>

/// \file
/// Common typedefs and functoins used by this project.

namespace geodb {

static_assert(CHAR_BIT == 8, "Byte width sanity check.");

using byte = unsigned char;

using std::size_t;

using u8 = uint8_t;
using u16 = uint16_t;
using u32 = uint32_t;
using u64 = uint64_t;

using i8 = int8_t;
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

#define GEODB_DEBUG

#else

#define geodb_assert(condition, message)

#endif

// Do not call directly.
void assertion_failed_impl(const char* file, int line,
                           const char* condition, const char* message);

} // namespace geodb

#endif // GEODB_COMMON_HPP
