/**
 * @file manager.hpp
 * @brief 对局作用域的卡牌容器：摸牌堆/弃牌堆 + 按 entity id 键控的
 *        手牌区/装备区/判定区（三者共用一个 CardZone 抽象）。
 * @note 哑状态持有者（对齐 entity/manager.hpp 的定位）：
 *       - 不校验规则：装备槽位占用、手牌上限、摸空补牌等归 gameplay；
 *       - 不发布事件：摸/弃/打出由 gameplay 观察返回值并自行发布；
 *       - 不持有 CardDefCatalog：需要 type/effect/equip 时由 gameplay 经
 *         catalog 解析，保持本模块无目录依赖。
 */

#ifndef INCLUDE_TKW_CARD_MANAGER_HPP
#define INCLUDE_TKW_CARD_MANAGER_HPP

#include <algorithm>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "card/card.hpp"
#include "card/catalog.hpp"
#include "card/def.hpp"
#include "util/rng.hpp"
#include "util/types.hpp"

namespace tkw
{
    namespace card
    {
        /** @brief 一个按 instance_id 管理的牌区（手牌/装备/判定共用）。 */
        class CardZone
        {
        public:
            void add(Card card) { cards_.push_back(std::move(card)); }

            /** @brief 按 instance_id 移除并返回；不存在时 None。 */
            Option<Card> remove(const std::string &instance_id)
            {
                const auto it = std::find_if(
                    cards_.begin(), cards_.end(),
                    [&](const Card &c) { return c.instance_id == instance_id; });
                if (it == cards_.end())
                    return Option<Card>::None();
                Card c = std::move(*it);
                cards_.erase(it);
                return Option<Card>::Some(std::move(c));
            }

            std::size_t size() const noexcept { return cards_.size(); }
            bool empty() const noexcept { return cards_.empty(); }
            const std::vector<Card> &view() const noexcept { return cards_; }

            /** @brief 取走全部牌（死亡清场）。 */
            std::vector<Card> drain() { return std::move(cards_); }

        private:
            std::vector<Card> cards_;
        };

        /**
         * @class CardManager
         * @brief 卡牌容器 + 按 entity id 的区（hand/equip/judge）。
         * @note 实体引用一律用 id 字符串（跨域约定），不依赖 entity 模块。
         */
        class CardManager
        {
        public:
            CardManager() = default;
            CardManager(const CardManager &) = delete;
            CardManager &operator=(const CardManager &) = delete;
            CardManager(CardManager &&) noexcept = default;
            CardManager &operator=(CardManager &&) noexcept = default;

            /**
             * @brief 按目录构建摸牌堆：每份副本生成一张实体牌并分配唯一 instance_id。
             * @note 卡牌顺序 = 目录迭代序（deck.json 引用顺序），同 seed 下确定。
             */
            void build_deck(const CardDefCatalog &catalog)
            {
                for (const auto &def : catalog)
                {
                    for (const auto &copy : def.copies)
                        draw_pile.push(make_card(def.id, copy));
                }
            }

            // ── 摸牌堆 / 弃牌堆 ──────────────────────────────────────────

            /** @brief 摸顶牌；摸空时 None（不自动洗回弃牌堆）。 */
            Option<Card> draw() { return draw_pile.pop(); }

            /** @brief 弃牌（置弃牌堆顶）。 */
            void discard(Card card) { discard_pile.push(std::move(card)); }

            /** @brief 从弃牌堆取回一张牌（结算回滚用）；不存在时 None。 */
            Option<Card> remove_from_discard(const std::string &instance_id)
            {
                return discard_pile.remove(instance_id);
            }

            /** @brief 置摸牌堆顶（种牌堆/结算后回置等）。 */
            void add_to_draw(Card card) { draw_pile.push(std::move(card)); }

            /**
             * @brief 弃牌堆整体洗回摸牌堆（判定/摸牌时牌堆空的补牌）。
             * @note 弃牌堆为空时无操作；转移后原地洗牌。
             */
            void refill_draw(Rng &rng)
            {
                while (true)
                {
                    auto c = discard_pile.pop();
                    if (c.is_none())
                        break;
                    draw_pile.push(std::move(c).unwrap());
                }
                if (draw_pile.size() > 1)
                    draw_pile.shuffle(rng);
            }

            std::size_t draw_size() const noexcept { return draw_pile.size(); }
            std::size_t discard_size() const noexcept { return discard_pile.size(); }

            void shuffle_draw(Rng &rng) { draw_pile.shuffle(rng); }
            Option<const Card *> draw_top() const { return draw_pile.top(); }

            // ── 手牌区 ──────────────────────────────────────────────────

            void add_to_hand(const std::string &entity_id, Card card)
            {
                hand_zones[entity_id].add(std::move(card));
            }

            Option<Card> remove_from_hand(
                const std::string &entity_id, const std::string &instance_id)
            {
                auto *z = find_zone(hand_zones, entity_id);
                return z ? z->remove(instance_id) : Option<Card>::None();
            }

            std::size_t hand_size(const std::string &entity_id) const
            {
                const auto *z = find_zone(hand_zones, entity_id);
                return z ? z->size() : 0;
            }

            /** @brief 手牌列表（不存在实体时为空列表）。 */
            const std::vector<Card> &hand(const std::string &entity_id) const
            {
                const auto *z = find_zone(hand_zones, entity_id);
                return z ? z->view() : empty_list();
            }

            // ── 装备区 ──────────────────────────────────────────────────

            void add_to_equip(const std::string &entity_id, Card card)
            {
                equip_zones[entity_id].add(std::move(card));
            }

            Option<Card> remove_from_equip(
                const std::string &entity_id, const std::string &instance_id)
            {
                auto *z = find_zone(equip_zones, entity_id);
                return z ? z->remove(instance_id) : Option<Card>::None();
            }

            std::size_t equip_size(const std::string &entity_id) const
            {
                const auto *z = find_zone(equip_zones, entity_id);
                return z ? z->size() : 0;
            }

            const std::vector<Card> &equip(const std::string &entity_id) const
            {
                const auto *z = find_zone(equip_zones, entity_id);
                return z ? z->view() : empty_list();
            }

            // ── 判定区 ──────────────────────────────────────────────────

            void add_to_judge(const std::string &entity_id, Card card)
            {
                judge_zones[entity_id].add(std::move(card));
            }

            Option<Card> remove_from_judge(
                const std::string &entity_id, const std::string &instance_id)
            {
                auto *z = find_zone(judge_zones, entity_id);
                return z ? z->remove(instance_id) : Option<Card>::None();
            }

            std::size_t judge_size(const std::string &entity_id) const
            {
                const auto *z = find_zone(judge_zones, entity_id);
                return z ? z->size() : 0;
            }

            const std::vector<Card> &judge(const std::string &entity_id) const
            {
                const auto *z = find_zone(judge_zones, entity_id);
                return z ? z->view() : empty_list();
            }

            // ── 跨区操作 ────────────────────────────────────────────────

            /**
             * @brief 从任一实体区域移除（hand → equip → judge 顺序）。
             * @param from 非空时写入来源区域。
             */
            Option<Card> remove_from_any(
                const std::string &entity_id, const std::string &instance_id,
                Zone *from = nullptr)
            {
                if (auto c = remove_from_hand(entity_id, instance_id); c.is_some())
                {
                    if (from)
                        *from = Zone::Hand;
                    return c;
                }
                if (auto c = remove_from_equip(entity_id, instance_id); c.is_some())
                {
                    if (from)
                        *from = Zone::Equip;
                    return c;
                }
                if (auto c = remove_from_judge(entity_id, instance_id); c.is_some())
                {
                    if (from)
                        *from = Zone::Judge;
                    return c;
                }
                return Option<Card>::None();
            }

            /**
             * @brief 死亡清场：手牌/装备/判定区全部置入弃牌堆。
             * @return 被弃置的牌（供调用方发布弃置事件）。
             */
            std::vector<Card> discard_all(const std::string &entity_id)
            {
                std::vector<Card> out;
                for (auto *zones : {&hand_zones, &equip_zones, &judge_zones})
                {
                    auto it = zones->find(entity_id);
                    if (it == zones->end())
                        continue;
                    auto cards = it->second.drain();
                    for (auto &c : cards)
                    {
                        out.push_back(c);
                        discard_pile.push(std::move(c));
                    }
                    zones->erase(it);
                }
                return out;
            }

        private:
            std::uint64_t instance_seq = 0;
            CardStack draw_pile;
            CardStack discard_pile;
            std::unordered_map<std::string, CardZone> hand_zones;
            std::unordered_map<std::string, CardZone> equip_zones;
            std::unordered_map<std::string, CardZone> judge_zones;

            static const std::vector<Card> &empty_list()
            {
                static const std::vector<Card> empty;
                return empty;
            }

            static CardZone *find_zone(
                std::unordered_map<std::string, CardZone> &zones,
                const std::string &entity_id)
            {
                auto it = zones.find(entity_id);
                return it == zones.end() ? nullptr : &it->second;
            }

            static const CardZone *find_zone(
                const std::unordered_map<std::string, CardZone> &zones,
                const std::string &entity_id)
            {
                auto it = zones.find(entity_id);
                return it == zones.end() ? nullptr : &it->second;
            }

            Card make_card(const std::string &def_id, const CardCopy &copy)
            {
                return Card{
                    def_id + "#" + std::to_string(instance_seq++), def_id, copy.suit,
                    copy.number};
            }
        };
    }
}

#endif  // INCLUDE_TKW_CARD_MANAGER_HPP
