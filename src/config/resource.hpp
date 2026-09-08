/**
 * @file resource.hpp
 * @brief 命名资源寻址与加载：name → <root>/<name>.json → 解析后的 Document。
 * @note 只负责「文件变 Document」：不做内容语义校验（schema 归域模块），
 *       不缓存（调用方持有 Document 并自行解析成值类型后可安全丢弃）。
 */

#ifndef INCLUDE_TKW_CONFIG_RESOURCE_HPP
#define INCLUDE_TKW_CONFIG_RESOURCE_HPP

#include <filesystem>
#include <pjh_json/document.hpp>
#include <pjh_json/error.hpp>
#include <string>
#include <string_view>

#include "config/error.hpp"
#include "io/file.hpp"

namespace tkw
{
    namespace config
    {
        namespace json = pjh::json;

        /**
         * @class ResourceStore
         * @brief 资源目录句柄。root 在构造时固定（测试/调平衡注入不同目录）。
         */
        class ResourceStore
        {
        public:
            explicit ResourceStore(std::filesystem::path root) : m_root(std::move(root))
            {
            }

            const std::filesystem::path &root() const noexcept { return m_root; }

            /**
             * @brief 加载资源 `<root>/<name>.json`。
             * @return FileNotFound（文件不存在，detail 为完整路径）/
             *         IoFailed（目录、无权限等，detail 为完整路径）/
             *         ParseError（非法 JSON，detail 为 "路径 @ 字节offset"）。
             */
            ConfigResult<json::Document> load(std::string_view name) const
            {
                const auto file = m_root / (std::string(name) + ".json");

                auto text = io::read_text(file);
                if (text.is_err())
                    return ConfigResult<json::Document>::Err(
                        to_config_error(text.unwrap_err(), file));

                try
                {
                    return ConfigResult<json::Document>::Ok(
                        json::parse_copy(text.unwrap()));
                }
                catch (const json::ParseError &e)
                {
                    return ConfigResult<json::Document>::Err(
                        ConfigError{
                            ConfigErrorKind::ParseError,
                            file.string() + " @ " + std::to_string(e.offset())});
                }
            }

        private:
            static ConfigError to_config_error(
                io::IoError e, const std::filesystem::path &file)
            {
                if (e == io::IoError::NotExist)
                    return {ConfigErrorKind::FileNotFound, file.string()};
                return {ConfigErrorKind::IoFailed, file.string()};
            }

            std::filesystem::path m_root;
        };
    }
}

#endif  // INCLUDE_TKW_CONFIG_RESOURCE_HPP
