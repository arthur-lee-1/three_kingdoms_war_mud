#ifndef INCLUDE_TKW_UTIL_TYPES_HPP
#define INCLUDE_TKW_UTIL_TYPES_HPP

#include <pjh_result.hpp>

namespace tkw
{
    template <typename T, typename E>
    using Result = pjh::result::Result<T, E>;

    template <typename T>
    using Option = pjh::result::Option<T>;
}

#endif  // INCLUDE_TKW_UTIL_TYPES_HPP
