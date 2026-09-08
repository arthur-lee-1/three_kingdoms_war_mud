/**
 * @file catalog.hpp
 * @brief 卡牌目录：deck.json + cards/<id>.json 的加载与语义校验。
 * @note 语义校验归本域（config 只管「文件 → Document」，见 resource.hpp 注记）：
 *       - deck.json 引用一张卡 → 按 <root>/cards/<id>.json 加载单卡文件；
 *       - 未知 effect.kind / scope / suit / equip 等在加载时立即 InvalidValue
 *         失败（detail 为字段路径，如 "cards/sha.json.effect.kind"）；
 *       - 文件内 id 必须等于文件名（deck 引用方），不一致即数据事故。
 * @note 加载完成后不持有 Document：全部解析成 CardDef 值类型，Document 即弃。
 */

#ifndef INCLUDE_TKW_CARD_CATALOG_HPP
#define INCLUDE_TKW_CARD_CATALOG_HPP

#include <initializer_list>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "card/def.hpp"
#include "config/error.hpp"
#include "config/fields.hpp"
#include "config/resource.hpp"
#include "util/types.hpp"

namespace tkw
{
    namespace card
    {
        namespace json = pjh::json;
        namespace cfg = tkw::config;

        namespace
        {
            /** 错误消息里的字段路径：path 为空即顶层。 */
            std::string key_path(std::string_view path, std::string_view key)
            {
                if (path.empty())
                    return std::string(key);
                return std::string(path) + "." + std::string(key);
            }

            /** 构造携带字段路径错误的 Result。 */
            template <typename T>
            cfg::ConfigResult<T> fail(cfg::ConfigErrorKind kind, std::string detail)
            {
                return cfg::ConfigResult<T>::Err(cfg::ConfigError{kind, std::move(detail)});
            }

            /** 字符串 → 封闭枚举：未知值报 InvalidValue（detail = 字段路径）。 */
            template <typename E>
            cfg::ConfigResult<E> enum_value(
                std::string_view s, std::string_view path,
                std::initializer_list<std::pair<std::string_view, E>> table)
            {
                for (const auto &[key, val] : table)
                    if (key == s)
                        return cfg::ConfigResult<E>::Ok(val);
                return fail<E>(cfg::ConfigErrorKind::InvalidValue, std::string(path));
            }

            /** 必填字符串字段 → 枚举。缺失/类型不符 → Missing/TypeMismatch。 */
            template <typename E>
            cfg::ConfigResult<E> require_enum(
                const json::Json &obj, std::string_view key, std::string_view path,
                std::initializer_list<std::pair<std::string_view, E>> table)
            {
                auto s = cfg::require_string(obj, key, path);
                if (s.is_err())
                    return cfg::ConfigResult<E>::Err(s.unwrap_err());
                return enum_value<E>(s.unwrap(), key_path(path, key), table);
            }

            /** 可选字符串字段 → 枚举：缺失回落 None；类型不符仍失败。 */
            template <typename E>
            cfg::ConfigResult<Option<E>> opt_enum(
                const json::Json &obj, std::string_view key, std::string_view path,
                std::initializer_list<std::pair<std::string_view, E>> table)
            {
                const auto *o = obj.try_as_object();
                if (!o)
                    return fail<Option<E>>(
                        cfg::ConfigErrorKind::TypeMismatch,
                        std::string(path.empty() ? "root" : path));
                if (!o->contains(key))
                    return cfg::ConfigResult<Option<E>>::Ok(Option<E>::None());
                const json::Json &v = (*o)[key];
                auto s = v.try_as_string();
                if (!s)
                    return fail<Option<E>>(
                        cfg::ConfigErrorKind::TypeMismatch, key_path(path, key));
                auto r = enum_value<E>(*s, key_path(path, key), table);
                if (r.is_err())
                    return cfg::ConfigResult<Option<E>>::Err(r.unwrap_err());
                return cfg::ConfigResult<Option<E>>::Ok(Option<E>::Some(r.unwrap()));
            }

            /** 可选对象字段：缺失回落 None；类型不符仍失败。 */
            cfg::ConfigResult<Option<const json::Json *>> opt_object(
                const json::Json &obj, std::string_view key, std::string_view path)
            {
                const auto *o = obj.try_as_object();
                if (!o)
                    return fail<Option<const json::Json *>>(
                        cfg::ConfigErrorKind::TypeMismatch,
                        std::string(path.empty() ? "root" : path));
                if (!o->contains(key))
                    return cfg::ConfigResult<Option<const json::Json *>>::Ok(
                        Option<const json::Json *>::None());
                const json::Json &v = (*o)[key];
                if (!v.try_as_object())
                    return fail<Option<const json::Json *>>(
                        cfg::ConfigErrorKind::TypeMismatch, key_path(path, key));
                return cfg::ConfigResult<Option<const json::Json *>>::Ok(
                    Option<const json::Json *>::Some(&v));
            }

            /** 解析单个副本：suit + number（点数须在 1..13）。 */
            cfg::ConfigResult<CardCopy> parse_card_copy(
                const json::Json &item, std::string_view ip)
            {
                auto suit = require_enum<Suit>(
                    item, "suit", ip,
                    {{"spade", Suit::Spade}, {"club", Suit::Club},
                     {"heart", Suit::Heart}, {"diamond", Suit::Diamond}});
                if (suit.is_err())
                    return cfg::ConfigResult<CardCopy>::Err(suit.unwrap_err());

                auto num = cfg::require_int(item, "number", ip);
                if (num.is_err())
                    return cfg::ConfigResult<CardCopy>::Err(num.unwrap_err());
                const auto n = num.unwrap();
                if (n < 1 || n > 13)
                    return fail<CardCopy>(
                        cfg::ConfigErrorKind::InvalidValue, key_path(ip, "number"));

                return cfg::ConfigResult<CardCopy>::Ok(
                    CardCopy{suit.unwrap(), static_cast<int>(n)});
            }

            /** 解析 effect 对象。 */
            cfg::ConfigResult<CardEffect> parse_card_effect(
                const json::Json &obj, std::string_view path)
            {
                CardEffect eff;
                auto kind = require_enum<CardEffectKind>(
                    obj, "kind", path,
                    {{"damage", CardEffectKind::Damage},
                     {"jink", CardEffectKind::Jink},
                     {"heal", CardEffectKind::Heal},
                     {"draw", CardEffectKind::Draw},
                     {"discard_target", CardEffectKind::DiscardTarget},
                     {"steal", CardEffectKind::Steal},
                     {"aoe_damage", CardEffectKind::AoeDamage},
                     {"duel", CardEffectKind::Duel},
                     {"counter_trick", CardEffectKind::CounterTrick},
                     {"reveal_pick", CardEffectKind::RevealPick},
                     {"borrowed_sword", CardEffectKind::BorrowedSword},
                     {"delayed_play_skip", CardEffectKind::DelayedPlaySkip},
                     {"lightning", CardEffectKind::Lightning},
                     {"no_sha_limit", CardEffectKind::NoShaLimit},
                     {"ignore_armor", CardEffectKind::IgnoreArmor},
                     {"cixiong", CardEffectKind::Cixiong},
                     {"extra_sha_after_jink", CardEffectKind::ExtraShaAfterJink},
                     {"two_cards_as_sha", CardEffectKind::TwoCardsAsSha},
                     {"discard_two_force_damage", CardEffectKind::DiscardTwoForceDamage},
                     {"multi_target_sha", CardEffectKind::MultiTargetSha},
                     {"discard_horse_on_damage", CardEffectKind::DiscardHorseOnDamage},
                     {"damage_as_discard", CardEffectKind::DamageAsDiscard},
                     {"judgement_jink", CardEffectKind::JudgementJink},
                     {"black_sha_immune", CardEffectKind::BlackShaImmune}});
                if (kind.is_err())
                    return cfg::ConfigResult<CardEffect>::Err(kind.unwrap_err());
                eff.kind = kind.unwrap();

                auto amount = cfg::opt_int(obj, "amount", 0, path);
                if (amount.is_err())
                    return cfg::ConfigResult<CardEffect>::Err(amount.unwrap_err());
                eff.amount = static_cast<int>(amount.unwrap());

                auto count = cfg::opt_int(obj, "count", 0, path);
                if (count.is_err())
                    return cfg::ConfigResult<CardEffect>::Err(count.unwrap_err());
                eff.count = static_cast<int>(count.unwrap());

                auto scope = opt_enum<Scope>(
                    obj, "scope", path,
                    {{"self", Scope::Self}, {"one_other", Scope::OneOther},
                     {"all_others", Scope::AllOthers}, {"all", Scope::All}});
                if (scope.is_err())
                    return cfg::ConfigResult<CardEffect>::Err(scope.unwrap_err());
                eff.scope = scope.unwrap();

                auto resp = opt_enum<ResponseKind>(
                    obj, "response", path,
                    {{"sha", ResponseKind::Sha}, {"jink", ResponseKind::Jink}});
                if (resp.is_err())
                    return cfg::ConfigResult<CardEffect>::Err(resp.unwrap_err());
                eff.response = resp.unwrap();

                auto range = cfg::opt_int(obj, "range", 0, path);
                if (range.is_err())
                    return cfg::ConfigResult<CardEffect>::Err(range.unwrap_err());
                eff.range = static_cast<int>(range.unwrap());

                return cfg::ConfigResult<CardEffect>::Ok(std::move(eff));
            }

            /** 解析 equip 对象。 */
            cfg::ConfigResult<CardEquip> parse_card_equip(
                const json::Json &obj, std::string_view path)
            {
                CardEquip eq;
                auto slot = require_enum<EquipSlot>(
                    obj, "slot", path,
                    {{"weapon", EquipSlot::Weapon},
                     {"armor", EquipSlot::Armor},
                     {"horse", EquipSlot::Horse}});
                if (slot.is_err())
                    return cfg::ConfigResult<CardEquip>::Err(slot.unwrap_err());
                eq.slot = slot.unwrap();

                auto range = cfg::opt_int(obj, "range", 0, path);
                if (range.is_err())
                    return cfg::ConfigResult<CardEquip>::Err(range.unwrap_err());
                eq.range = static_cast<int>(range.unwrap());

                auto dir = opt_enum<HorseDirection>(
                    obj, "direction", path,
                    {{"offensive", HorseDirection::Offensive},
                     {"defensive", HorseDirection::Defensive}});
                if (dir.is_err())
                    return cfg::ConfigResult<CardEquip>::Err(dir.unwrap_err());
                eq.direction = dir.unwrap();

                return cfg::ConfigResult<CardEquip>::Ok(std::move(eq));
            }

            /**
             * @brief 解析单卡文件（root = 文件顶层对象）。
             * @param path 容器路径（如 "cards/sha.json"），用于拼错误字段路径。
             */
            cfg::ConfigResult<CardDef> parse_card_def(
                const json::Json &root, std::string_view path)
            {
                CardDef def;

                auto id = cfg::require_string(root, "id", path);
                if (id.is_err())
                    return cfg::ConfigResult<CardDef>::Err(id.unwrap_err());
                def.id = id.unwrap();

                auto name = cfg::require_string(root, "name", path);
                if (name.is_err())
                    return cfg::ConfigResult<CardDef>::Err(name.unwrap_err());
                def.name = name.unwrap();

                auto type = require_enum<CardType>(
                    root, "type", path,
                    {{"basic", CardType::Basic}, {"trick", CardType::Trick},
                     {"equipment", CardType::Equipment}});
                if (type.is_err())
                    return cfg::ConfigResult<CardDef>::Err(type.unwrap_err());
                def.type = type.unwrap();

                auto subtype = cfg::opt_string(root, "subtype", "", path);
                if (subtype.is_err())
                    return cfg::ConfigResult<CardDef>::Err(subtype.unwrap_err());
                def.subtype = subtype.unwrap();

                auto set = cfg::opt_string(root, "set", "standard", path);
                if (set.is_err())
                    return cfg::ConfigResult<CardDef>::Err(set.unwrap_err());
                def.set = set.unwrap();

                auto text = cfg::opt_string(root, "text", "", path);
                if (text.is_err())
                    return cfg::ConfigResult<CardDef>::Err(text.unwrap_err());
                def.text = text.unwrap();

                auto er = cfg::each(root, "copies", path,
                                    [&def](const json::Json &item, std::string_view ip)
                                        -> cfg::ConfigResult<void>
                {
                    auto copy = parse_card_copy(item, ip);
                    if (copy.is_err())
                        return cfg::ConfigResult<void>::Err(copy.unwrap_err());
                    def.copies.push_back(copy.unwrap());
                    return cfg::ConfigResult<void>::Ok();
                });
                if (er.is_err())
                    return cfg::ConfigResult<CardDef>::Err(er.unwrap_err());

                auto effect = opt_object(root, "effect", path);
                if (effect.is_err())
                    return cfg::ConfigResult<CardDef>::Err(effect.unwrap_err());
                if (effect.unwrap().is_some())
                {
                    auto eff = parse_card_effect(
                        *effect.unwrap().unwrap(), key_path(path, "effect"));
                    if (eff.is_err())
                        return cfg::ConfigResult<CardDef>::Err(eff.unwrap_err());
                    def.effect = Option<CardEffect>::Some(std::move(eff).unwrap());
                }

                auto equip = opt_object(root, "equip", path);
                if (equip.is_err())
                    return cfg::ConfigResult<CardDef>::Err(equip.unwrap_err());
                if (equip.unwrap().is_some())
                {
                    auto eq = parse_card_equip(
                        *equip.unwrap().unwrap(), key_path(path, "equip"));
                    if (eq.is_err())
                        return cfg::ConfigResult<CardDef>::Err(eq.unwrap_err());
                    def.equip = Option<CardEquip>::Some(std::move(eq).unwrap());
                }

                return cfg::ConfigResult<CardDef>::Ok(std::move(def));
            }
        }

        /**
         * @class CardDefCatalog
         * @brief 对局作用域的卡牌定义容器：deck.json 引用的有序定义 + id 索引。
         * @note 一次加载后不可变：实体牌（Card）引用其 def_id 或拷贝副本，
         *       目录本身不参与对局状态变化。
         * @note 迭代顺序 = deck.json 引用顺序（**不是无序**）：build_deck 等
         *       消费者依赖该顺序，保证同 seed 下牌堆初始序确定；id 查询
         *       走内部索引（O(1)），与迭代序无关。
         */
        class CardDefCatalog
        {
        public:
            /**
             * @brief 从 ResourceStore 加载：先读 <deck_name>.json（牌堆构成），
             *        再逐个加载 cards/<id>.json。
             * @return Ok 为目录；Err 为 config 层错误（文件缺失/非法 JSON/
             *         MissingField/TypeMismatch/InvalidValue，detail 带定位）。
             */
            static cfg::ConfigResult<CardDefCatalog> load(
                const cfg::ResourceStore &store, std::string_view deck_name)
            {
                auto deck_doc = store.load(deck_name);
                if (deck_doc.is_err())
                    return cfg::ConfigResult<CardDefCatalog>::Err(deck_doc.unwrap_err());
                const json::Json &root = deck_doc.unwrap().root();

                CardDefCatalog catalog;
                auto er = cfg::each(root, "cards", {},
                                    [&catalog, &store](const json::Json &item,
                                                       std::string_view ip)
                                        -> cfg::ConfigResult<void>
                {
                    auto id_s = item.try_as_string();
                    if (!id_s)
                        return fail<void>(
                            cfg::ConfigErrorKind::TypeMismatch, std::string(ip));
                    const std::string cid(*id_s);

                    if (catalog.index.find(cid) != catalog.index.end())
                        return fail<void>(
                            cfg::ConfigErrorKind::InvalidValue,
                            std::string(ip) + " 重复引用卡牌 " + cid);

                    const std::string file = "cards/" + cid + ".json";
                    auto card_doc = store.load("cards/" + cid);
                    if (card_doc.is_err())
                        return cfg::ConfigResult<void>::Err(card_doc.unwrap_err());

                    auto def = parse_card_def(card_doc.unwrap().root(), file);
                    if (def.is_err())
                        return cfg::ConfigResult<void>::Err(def.unwrap_err());

                    if (def.unwrap().id != cid)
                        return fail<void>(
                            cfg::ConfigErrorKind::InvalidValue, file + ".id");

                    catalog.index.emplace(cid, catalog.defs.size());
                    catalog.defs.push_back(std::move(def).unwrap());
                    return cfg::ConfigResult<void>::Ok();
                });
                if (er.is_err())
                    return cfg::ConfigResult<CardDefCatalog>::Err(er.unwrap_err());
                return cfg::ConfigResult<CardDefCatalog>::Ok(std::move(catalog));
            }

            /** @brief O(1) 按卡牌 id 查询（经内部索引）；不存在时为 None。 */
            Option<const CardDef *> find(const std::string &id) const
            {
                auto it = index.find(id);
                if (it == index.end())
                    return Option<const CardDef *>::None();
                return Option<const CardDef *>::Some(&defs[it->second]);
            }

            std::size_t size() const noexcept { return defs.size(); }

            /** @brief 牌堆物理张数（所有定义副本数之和）。 */
            std::size_t total_copies() const noexcept
            {
                std::size_t n = 0;
                for (const auto &def : defs)
                    n += def.copies.size();
                return n;
            }

            /** @brief 按 deck.json 引用顺序迭代。 */
            auto begin() const noexcept { return defs.begin(); }
            auto end() const noexcept { return defs.end(); }

        private:
            std::vector<CardDef> defs; /**< deck.json 引用顺序（build_deck 等依赖此序） */
            std::unordered_map<std::string, std::size_t> index; /**< id → defs 下标 */
        };
    }
}

#endif  // INCLUDE_TKW_CARD_CATALOG_HPP