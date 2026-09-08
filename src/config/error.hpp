#ifndef INCLUDE_TKW_CONFIG_ERROR_HPP
#define INCLUDE_TKW_CONFIG_ERROR_HPP

#include <cstdint>
#include <string>

#include "util/types.hpp"

namespace tkw
{
    namespace config
    {
        /**
         * @brief 配置层错误（文件/JSON 语义无关平台 I/O 细节）。
         * @note IoError 在 resource 边界收敛为 FileNotFound / IoFailed；
         *       字段类错误（Missing/Type/Value）的 detail 一律是字段路径。
         */
        enum class ConfigErrorKind : std::uint8_t
        {
            FileNotFound, /**< 资源文件不存在 */
            IoFailed,     /**< 文件 I/O 失败（非常规文件/无权限/其他） */
            ParseError,   /**< JSON 解析失败（detail 带字节 offset） */
            MissingField, /**< 必填字段缺失 */
            TypeMismatch, /**< 字段类型不符 */
            InvalidValue, /**< 字段值非法（未知枚举/越界；通常由域模块判定） */
        };

        /**
         * @brief 配置错误值：类别 + 上下文（文件路径或字段路径，如 "cards[3].damage"）。
         */
        struct ConfigError
        {
            ConfigErrorKind kind = ConfigErrorKind::ParseError;
            std::string detail;

            bool operator==(const ConfigError &) const = default;
        };

        /**
         * @brief 模块级 Result 别名：错误槽固定为 ConfigError。
         */
        template <typename T>
        using ConfigResult = Result<T, ConfigError>;
    }
}

#endif  // INCLUDE_TKW_CONFIG_ERROR_HPP
