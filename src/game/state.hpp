/**
 * @file state.hpp
 * @brief 对局状态的基础操作：扣血/回血/摸牌（不含濒死死亡，那些归 combat.hpp）。
 * @note 这些是「状态层」原语：只改实体状态与牌堆，不发布流程事件（濒死/死亡），
 *       但会发布卡牌域事件（如摸牌）供日志/回放消费。
 */

#ifndef INCLUDE_TKW_GAME_STATE_HPP
#define INCLUDE_TKW_GAME_STATE_HPP

#include <string>
#include <utility>

#include "card/def.hpp"
#include "card/manager.hpp"
#include "entity/manager.hpp"
#include "game/card_event.hpp"
#include "game/context.hpp"
#include "util/types.hpp"

namespace tkw
{
    namespace game
    {
        /** @brief 花色是否为黑（♠/♣）。 */
        inline bool is_black_suit(card::Suit s)
        {
            return s == card::Suit::Spade || s == card::Suit::Club;
        }

        /** @brief 花色是否为红（♥/♦）。 */
        inline bool is_red_suit(card::Suit s)
        {
            return s == card::Suit::Heart || s == card::Suit::Diamond;
        }

        /** @brief 扣血（纯状态：可扣到非正=濒死值状态，不触发死亡流程）。 */
        inline void apply_damage(
            GameContext &ctx, const std::string &source,
            const std::string &target, int amount)
        {
            const auto e = ctx.entities->find(target);
            if (e.is_some())
                e.unwrap()->take_damage(source, amount, false);
        }

        /** @brief 回血（按上限钳制）。 */
        inline void apply_heal(GameContext &ctx, const std::string &target, int amount)
        {
            const auto e = ctx.entities->find(target);
            if (e.is_some())
                e.unwrap()->heal(amount);
        }

        /** @brief 摸 count 张进手牌；牌堆摸空即停，返回实际摸到的张数。 */
        inline int apply_draw(GameContext &ctx, const std::string &player, int count)
        {
            int drew = 0;
            for (int i = 0; i < count; ++i)
            {
                auto c = ctx.cards->draw();
                if (c.is_none())
                    break;
                card::Card card = std::move(c).unwrap();
                ctx.cards->add_to_hand(player, card);
                emit_card_drawn(ctx, player, card);
                ++drew;
            }
            return drew;
        }

        /** @brief 从某实体的任一区域移除指定牌（填 out 返回被移除的牌与来源区域）。 */
        inline bool remove_card_from_zones(
            GameContext &ctx, const std::string &entity_id,
            const std::string &instance_id, card::Card &out,
            Zone *from_zone = nullptr)
        {
            auto h = ctx.cards->remove_from_hand(entity_id, instance_id);
            if (h.is_some())
            {
                out = std::move(h).unwrap();
                if (from_zone)
                    *from_zone = Zone::Hand;
                return true;
            }
            auto e = ctx.cards->remove_from_equip(entity_id, instance_id);
            if (e.is_some())
            {
                out = std::move(e).unwrap();
                if (from_zone)
                    *from_zone = Zone::Equip;
                return true;
            }
            auto j = ctx.cards->remove_from_judge(entity_id, instance_id);
            if (j.is_some())
            {
                out = std::move(j).unwrap();
                if (from_zone)
                    *from_zone = Zone::Judge;
                return true;
            }
            return false;
        }

        /**
         * @brief 判定：从摸牌堆顶揭示一张（牌堆空则弃牌堆洗回）。
         * @return None 表示摸牌堆与弃牌堆皆空（无法判定）。
         * @note 依赖 ctx.rng 洗回；随机源为 null 时牌堆空则直接 None。
         */
        inline Option<card::Card> perform_judgement(GameContext &ctx)
        {
            if (ctx.cards->draw_size() == 0)
            {
                if (ctx.cards->discard_size() == 0)
                    return Option<card::Card>::None();
                if (ctx.rng)
                    ctx.cards->refill_draw(*ctx.rng);
            }
            return ctx.cards->draw();
        }
    }
}

#endif  // INCLUDE_TKW_GAME_STATE_HPP