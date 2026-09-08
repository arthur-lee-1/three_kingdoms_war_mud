/**
 * @file manager.hpp
 * @brief 对局作用域的卡牌容器：摸牌堆/弃牌堆 + 按 entity id 键控的
 *        手牌区/装备区/判定区。
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
                hand_zone[entity_id].push_back(std::move(card));
            }

            /** @brief 从手牌区移除指定牌并返回；不存在时 None。 */
            Option<Card> remove_from_hand(
                const std::string &entity_id, const std::string &instance_id)
            {
                return remove_zone(hand_zone, entity_id, instance_id);
            }

            std::size_t hand_size(const std::string &entity_id) const
            {
                return zone_size(hand_zone, entity_id);
            }

            /** @brief 手牌列表（不存在实体时为空列表）。 */
            const std::vector<Card> &hand(const std::string &entity_id) const
            {
                return zone_ref(hand_zone, entity_id);
            }

            // ── 装备区 ──────────────────────────────────────────────────

            void add_to_equip(const std::string &entity_id, Card card)
            {
                equip_zone[entity_id].push_back(std::move(card));
            }

            Option<Card> remove_from_equip(
                const std::string &entity_id, const std::string &instance_id)
            {
                return remove_zone(equip_zone, entity_id, instance_id);
            }

            std::size_t equip_size(const std::string &entity_id) const
            {
                return zone_size(equip_zone, entity_id);
            }

            const std::vector<Card> &equip(const std::string &entity_id) const
            {
                return zone_ref(equip_zone, entity_id);
            }

            // ── 判定区 ──────────────────────────────────────────────────

            void add_to_judge(const std::string &entity_id, Card card)
            {
                judge_zone[entity_id].push_back(std::move(card));
            }

            Option<Card> remove_from_judge(
                const std::string &entity_id, const std::string &instance_id)
            {
                return remove_zone(judge_zone, entity_id, instance_id);
            }

            std::size_t judge_size(const std::string &entity_id) const
            {
                return zone_size(judge_zone, entity_id);
            }

            const std::vector<Card> &judge(const std::string &entity_id) const
            {
                return zone_ref(judge_zone, entity_id);
            }

            /**
             * @brief 死亡清场：手牌/装备/判定区全部置入弃牌堆。
             * @return 被弃置的牌（供调用方发布弃置事件）。
             */
            std::vector<Card> discard_all(const std::string &entity_id)
            {
                std::vector<Card> out;
                auto drain = [this, &entity_id, &out](auto &zone)
                {
                    auto it = zone.find(entity_id);
                    if (it == zone.end())
                        return;
                    for (auto &c : it->second)
                    {
                        out.push_back(c);
                        discard_pile.push(std::move(c));
                    }
                    zone.erase(it);
                };
                drain(hand_zone);
                drain(equip_zone);
                drain(judge_zone);
                return out;
            }

        private:
            std::uint64_t instance_seq = 0;
            CardStack draw_pile;
            CardStack discard_pile;
            std::unordered_map<std::string, std::vector<Card>> hand_zone;
            std::unordered_map<std::string, std::vector<Card>> equip_zone;
            std::unordered_map<std::string, std::vector<Card>> judge_zone;

            Card make_card(const std::string &def_id, const CardCopy &copy)
            {
                return Card{
                    def_id + "#" + std::to_string(instance_seq++), def_id, copy.suit,
                    copy.number};
            }

            static Option<Card> remove_zone(
                std::unordered_map<std::string, std::vector<Card>> &zone,
                const std::string &entity_id,
                const std::string &instance_id)
            {
                auto it = zone.find(entity_id);
                if (it == zone.end())
                    return Option<Card>::None();
                auto &vec = it->second;
                const auto e = std::find_if(
                    vec.begin(), vec.end(),
                    [&](const Card &c) { return c.instance_id == instance_id; });
                if (e == vec.end())
                    return Option<Card>::None();
                Card c = std::move(*e);
                vec.erase(e);
                if (vec.empty())
                    zone.erase(it);
                return Option<Card>::Some(std::move(c));
            }

            static std::size_t zone_size(
                const std::unordered_map<std::string, std::vector<Card>> &zone,
                const std::string &entity_id)
            {
                auto it = zone.find(entity_id);
                return it == zone.end() ? 0 : it->second.size();
            }

            static const std::vector<Card> &zone_ref(
                const std::unordered_map<std::string, std::vector<Card>> &zone,
                const std::string &entity_id)
            {
                static const std::vector<Card> empty;
                auto it = zone.find(entity_id);
                return it == zone.end() ? empty : it->second;
            }
        };
    }
}

#endif  // INCLUDE_TKW_CARD_MANAGER_HPP