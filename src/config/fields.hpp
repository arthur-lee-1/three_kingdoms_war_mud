/**
 * @file fields.hpp
 * @brief JSON 节点上的类型化字段提取（域无关，不含任何游戏规则）。
 * @note 语义约定：require_* = 缺失/类型不符都硬失败；opt_* = 仅「缺失」允许
 *       回落默认值，类型不符仍失败（默认值不得吞掉写错类型）。
 *       path 参数是**容器自身**的路径（"" = 顶层），用于拼装错误里的
 *       字段路径：require_int(doc.root(), "damage", "cards[3]") 报错时
 *       detail = "cards[3].damage"。
 */

#ifndef INCLUDE_TKW_CONFIG_FIELDS_HPP
#define INCLUDE_TKW_CONFIG_FIELDS_HPP

#include <cstdint>
#include <pjh_json/json.hpp>
#include <string>
#include <string_view>

#include "config/error.hpp"

namespace tkw
{
    namespace config
    {
        namespace json = pjh::json;

        namespace
        {
            /** 错误消息里的字段路径：path 为空即顶层，detail 就是 key 本身。 */
            std::string field_path(std::string_view path, std::string_view key)
            {
                if (path.empty())
                    return std::string(key);
                return std::string(path) + "." + std::string(key);
            }

            /** 容器自身路径（空 = 顶层，消息里统一记 "root"）。 */
            std::string container_path(std::string_view path)
            {
                return path.empty() ? std::string("root") : std::string(path);
            }

            /** 构造携带字段路径错误的 Result。 */
            template <typename T>
            ConfigResult<T> fail(ConfigErrorKind kind, std::string detail)
            {
                return ConfigResult<T>::Err(ConfigError{kind, std::move(detail)});
            }
        }

        /** @brief 必填整型字段。缺失 → MissingField；类型不符 → TypeMismatch。 */
        inline ConfigResult<std::int64_t> require_int(
            const json::Json &obj, std::string_view key, std::string_view path = {})
        {
            const auto *o = obj.try_as_object();
            if (!o)
                return fail<std::int64_t>(
                    ConfigErrorKind::TypeMismatch, container_path(path));
            if (!o->contains(key))
                return fail<std::int64_t>(
                    ConfigErrorKind::MissingField, field_path(path, key));
            const json::Json &v = (*o)[key];
            auto r = v.try_as_int();
            if (!r)
                return fail<std::int64_t>(
                    ConfigErrorKind::TypeMismatch, field_path(path, key));
            return ConfigResult<std::int64_t>::Ok(*r);
        }

        /** @brief 必填字符串字段（拷贝出来，不依赖 Document 生命周期）。 */
        inline ConfigResult<std::string> require_string(
            const json::Json &obj, std::string_view key, std::string_view path = {})
        {
            const auto *o = obj.try_as_object();
            if (!o)
                return fail<std::string>(
                    ConfigErrorKind::TypeMismatch, container_path(path));
            if (!o->contains(key))
                return fail<std::string>(
                    ConfigErrorKind::MissingField, field_path(path, key));
            const json::Json &v = (*o)[key];
            auto r = v.try_as_string();
            if (!r)
                return fail<std::string>(
                    ConfigErrorKind::TypeMismatch, field_path(path, key));
            return ConfigResult<std::string>::Ok(std::string(*r));
        }

        /** @brief 必填布尔字段。 */
        inline ConfigResult<bool> require_bool(
            const json::Json &obj, std::string_view key, std::string_view path = {})
        {
            const auto *o = obj.try_as_object();
            if (!o)
                return fail<bool>(ConfigErrorKind::TypeMismatch, container_path(path));
            if (!o->contains(key))
                return fail<bool>(ConfigErrorKind::MissingField, field_path(path, key));
            const json::Json &v = (*o)[key];
            auto r = v.try_as_boolean();
            if (!r)
                return fail<bool>(ConfigErrorKind::TypeMismatch, field_path(path, key));
            return ConfigResult<bool>::Ok(*r);
        }

        /** @brief 必填对象字段。 */
        inline ConfigResult<const json::Json *> require_object(
            const json::Json &obj, std::string_view key, std::string_view path = {})
        {
            const auto *o = obj.try_as_object();
            if (!o)
                return fail<const json::Json *>(
                    ConfigErrorKind::TypeMismatch, container_path(path));
            if (!o->contains(key))
                return fail<const json::Json *>(
                    ConfigErrorKind::MissingField, field_path(path, key));
            const json::Json &v = (*o)[key];
            if (!v.try_as_object())
                return fail<const json::Json *>(
                    ConfigErrorKind::TypeMismatch, field_path(path, key));
            return ConfigResult<const json::Json *>::Ok(&v);
        }

        /** @brief 必填数组字段。 */
        inline ConfigResult<const json::Array *> require_array(
            const json::Json &obj, std::string_view key, std::string_view path = {})
        {
            const auto *o = obj.try_as_object();
            if (!o)
                return fail<const json::Array *>(
                    ConfigErrorKind::TypeMismatch, container_path(path));
            if (!o->contains(key))
                return fail<const json::Array *>(
                    ConfigErrorKind::MissingField, field_path(path, key));
            const json::Json &v = (*o)[key];
            const auto *arr = v.try_as_array();
            if (!arr)
                return fail<const json::Array *>(
                    ConfigErrorKind::TypeMismatch, field_path(path, key));
            return ConfigResult<const json::Array *>::Ok(arr);
        }

        /** @brief 可选整型字段：缺失回落默认值；**类型不符仍失败**。 */
        inline ConfigResult<std::int64_t> opt_int(
            const json::Json &obj,
            std::string_view key,
            std::int64_t def,
            std::string_view path = {})
        {
            const auto *o = obj.try_as_object();
            if (!o)
                return fail<std::int64_t>(
                    ConfigErrorKind::TypeMismatch, container_path(path));
            if (!o->contains(key))
                return ConfigResult<std::int64_t>::Ok(def);
            const json::Json &v = (*o)[key];
            auto r = v.try_as_int();
            if (!r)
                return fail<std::int64_t>(
                    ConfigErrorKind::TypeMismatch, field_path(path, key));
            return ConfigResult<std::int64_t>::Ok(*r);
        }

        /** @brief 可选字符串字段：缺失回落默认值；类型不符仍失败。 */
        inline ConfigResult<std::string> opt_string(
            const json::Json &obj,
            std::string_view key,
            std::string_view def,
            std::string_view path = {})
        {
            const auto *o = obj.try_as_object();
            if (!o)
                return fail<std::string>(
                    ConfigErrorKind::TypeMismatch, container_path(path));
            if (!o->contains(key))
                return ConfigResult<std::string>::Ok(std::string(def));
            const json::Json &v = (*o)[key];
            auto r = v.try_as_string();
            if (!r)
                return fail<std::string>(
                    ConfigErrorKind::TypeMismatch, field_path(path, key));
            return ConfigResult<std::string>::Ok(std::string(*r));
        }

        /** @brief 可选布尔字段：缺失回落默认值；类型不符仍失败。 */
        inline ConfigResult<bool> opt_bool(
            const json::Json &obj,
            std::string_view key,
            bool def,
            std::string_view path = {})
        {
            const auto *o = obj.try_as_object();
            if (!o)
                return fail<bool>(ConfigErrorKind::TypeMismatch, container_path(path));
            if (!o->contains(key))
                return ConfigResult<bool>::Ok(def);
            const json::Json &v = (*o)[key];
            auto r = v.try_as_boolean();
            if (!r)
                return fail<bool>(ConfigErrorKind::TypeMismatch, field_path(path, key));
            return ConfigResult<bool>::Ok(*r);
        }

        /**
         * @brief 迭代数组字段：对每个元素调用 f(element, item_path)，
         *        item_path 形如 "cards[3]"，可直接作为 require 系列函数的
         *        path 参数，拼出 "cards[3].damage" 这类完整错误路径。
         * @tparam F 返回 ConfigResult<void> 的回调（元素级错误经其返回传播）。
         * @note 字段缺失/非数组 → 按字段路径报 Missing/TypeMismatch；
         *       首个元素错误立即返回，后续元素不再处理。
         */
        template <typename F>
        ConfigResult<void> each(
            const json::Json &obj, std::string_view key, std::string_view path, F &&f)
        {
            const auto *o = obj.try_as_object();
            if (!o)
                return fail<void>(ConfigErrorKind::TypeMismatch, container_path(path));
            if (!o->contains(key))
                return fail<void>(ConfigErrorKind::MissingField, field_path(path, key));
            const json::Json &v = (*o)[key];
            const auto *arr = v.try_as_array();
            if (!arr)
                return fail<void>(ConfigErrorKind::TypeMismatch, field_path(path, key));

            const auto prefix = field_path(path, key);
            for (std::size_t i = 0; i < arr->size(); ++i)
            {
                const std::string item_path = prefix + "[" + std::to_string(i) + "]";
                auto r = f((*arr)[i], item_path);
                if (r.is_err())
                    return fail<void>(r.unwrap_err().kind, r.unwrap_err().detail);
            }
            return ConfigResult<void>::Ok();
        }
    }
}

#endif  // INCLUDE_TKW_CONFIG_FIELDS_HPP
