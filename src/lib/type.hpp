#pragma once

namespace lib {

template <typename... T>
struct Count {
    Count(T...) {}
    static constexpr size_t value = sizeof...(T);
};

}

template <typename... T>
constexpr size_t pack_size(T...) {
    return lib::Count<T...>::value;
}