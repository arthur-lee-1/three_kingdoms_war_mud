#ifndef INCLUDE_TKW_ENTITY_ERROR_HPP
#define INCLUDE_TKW_ENTITY_ERROR_HPP

#include <cstdint>

#include "util/types.hpp"

namespace tkw
{
    namespace entity
    {
        /** @brief 实体域错误。 */
        enum class EntityError : std::uint8_t
        {
            DuplicateId, /**< 层内已存在相同 id 的实体 */
        };

        template <typename T>
        using EntityResult = Result<T, EntityError>;
    }
}

#endif  // INCLUDE_TKW_ENTITY_ERROR_HPP
