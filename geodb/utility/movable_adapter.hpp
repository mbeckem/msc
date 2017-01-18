#ifndef UTILITY_MOVABLE_ADAPTER_HPP
#define UTILITY_MOVABLE_ADAPTER_HPP

#include "geodb/common.hpp"

#include <tpie/memory.h>

#include <type_traits>

namespace geodb {

template<typename T, typename Enable = void>
class movable_adapter;

struct in_place_t {};

/// A template class for making an arbitrary type
/// move-constructible.
/// This is done by wrapping non-movable objects into a unique pointer.
/// If the object is already move-constructible, then we will simply
/// store the object itself.
///
/// The inner object can be accessed using `operator->` or `operator*`.
///
/// It is an error to access the inner object after it has been moved.
template<typename T, typename Enable>
class movable_adapter {
public:
    static constexpr bool wrapped = true;

    template<typename... Args>
    movable_adapter(in_place_t, Args&&... args)
        : m_inner(tpie::make_unique<T>(std::forward<Args>(args)...))
    {}

    movable_adapter(movable_adapter&& other) noexcept = default;
    movable_adapter& operator=(movable_adapter&& other) = delete;

    T& operator*() { check(); return *m_inner; }
    const T& operator*() const { check(); return *m_inner; }
    T* operator->() { check(); return m_inner.get(); }
    const T* operator->() const { check(); return m_inner.get(); }

private:
    void check() const {
        geodb_assert(m_inner, "instance is in moved from state");
    }

private:
    tpie::unique_ptr<T> m_inner;
};

// Specialization for move-constructible types.
template<typename T>
class movable_adapter<T, std::enable_if_t<std::is_move_constructible<T>::value>> {
    static_assert(std::is_nothrow_move_constructible<T>::value,
                  "The type must be noexcept-move-constructible");

public:
    static constexpr bool wrapped = false;

    template<typename... Args>
    movable_adapter(in_place_t, Args&&... args)
        : m_inner(std::forward<Args>(args)...)
    {}

    movable_adapter(movable_adapter&& other) noexcept
        : m_inner(std::move(other.m_inner))
    {}

    movable_adapter& operator=(movable_adapter&& other) = delete;

    T& operator*() { return m_inner; }
    const T& operator*() const { return m_inner; }
    T* operator->() { return std::addressof(m_inner); }
    const T* operator->() const { return std::addressof(m_inner); }

private:
    T m_inner;
};

template<typename T, typename... Args>
movable_adapter<T> make_movable(Args&&... args) {
    return movable_adapter<T>(in_place_t(), std::forward<Args>(args)...);
}

} // namespace geodb

#endif // UTILITY_MOVABLE_ADAPTER_HPP
