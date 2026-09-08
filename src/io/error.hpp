#ifndef INCLUDE_TKW_IO_ERROR_HPP
#define INCLUDE_TKW_IO_ERROR_HPP

#include <cstdint>

#include "util/types.hpp"

namespace tkw
{
    namespace io
    {
        /**
         * @brief 文件 I/O 错误（平台无关语义）。
         * @note pjh_platform 的 ErrorCode 在本模块边界统一收敛为本枚举，
         *       上层（config / persistence）只见 IoError，不感知平台错误码。
         */
        enum class IoError : std::uint8_t
        {
            NotExist,   /**< 路径不存在（读：文件不存在；写：父目录不存在） */
            NotAFile,   /**< 路径存在但不是常规文件（如目录） */
            Permission, /**< 无访问权限 */
            IoFailed,   /**< 其他 I/O 失败 */
        };

        /**
         * @brief 模块级 Result 别名：错误槽固定为 IoError。
         * @note 模块内接口统一写 IOResult<T>，如 IOResult<std::string> / IOResult<void>。
         */
        template <typename T>
        using IOResult = Result<T, IoError>;
    }
}

#endif  // INCLUDE_TKW_IO_ERROR_HPP
