/**
 * @file counter.hpp
 * @brief 无懈可击：抵消一张锦囊牌对一名角色产生的效果。
 * @note 规则简化实现：
 *       - 从使用锦囊的玩家（当前回合角色）起，按座位序轮询「是否出无懈」；
 *       - 每出一张无懈翻转「是否被抵消」状态；一整轮无人出则结算；
 *       - 最后状态 = 出无懈次数的奇偶（链式相抵），true = 被抵消。
 * @note 只抵消锦囊牌（type == Trick），基本牌（杀/闪/桃）不可无懈。
 */

#ifndef INCLUDE_TKW_GAME_COUNTER_HPP
#define INCLUDE_TKW_GAME_COUNTER_HPP

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include "card/catalog.hpp"
#include "card/def.hpp"
#include "card/manager.hpp"
#include "game/card_event.hpp"
#include "game/context.hpp"
#include "game/decision.hpp"
#include "util/types.hpp"

namespace tkw
{
    namespace game
    {
        /** @brief 该定义是否可作无懈响应牌（数据标记 counter，不再认 id）。 */
        inline bool is_counter_def(const card::CardDef &def)
        {
            return def.counter;
        }

        inline bool has_counter_card(const GameContext &ctx, const std::string &player)
        {
            for (const auto &c : ctx.cards->hand(player))
            {
                const auto def = ctx.catalog->find(c.def_id);
                if (def.is_some() && is_counter_def(*def.unwrap()))
                    return true;
            }
            return false;
        }

        inline bool consume_counter(GameContext &ctx, const std::string &player)
        {
            for (const auto &c : ctx.cards->hand(player))
            {
                const auto def = ctx.catalog->find(c.def_id);
                if (def.is_some() && is_counter_def(*def.unwrap()))
                {
                    auto removed = ctx.cards->remove_from_hand(player, c.instance_id);
                    if (removed.is_some())
                    {
                        card::Card card = std::move(removed).unwrap();
                        ctx.cards->discard(card);
                        emit_card_discarded(ctx, player, card);
                    }
                    return true;
                }
            }
            return false;
        }

        /** @brief 座位序（从 start 开始环绕）。 */
        inline std::vector<std::string> seat_order_from(
            const GameContext &ctx, const std::string &start)
        {
            return ctx.entities->order_from(start);
        }

        /** @brief 询问某玩家是否打出无懈（有牌且决定出则消费）。 */
        inline bool try_play_counter(
            GameContext &ctx, DecisionSource &ai, const std::string &player)
        {
            if (!has_counter_card(ctx, player))
                return false;
            if (!ai.play_counter(ctx, player))
                return false;
            consume_counter(ctx, player);
            return true;
        }

        /**
         * @brief 无懈响应窗口（链式）。
         * @param trick 被结算的锦囊定义（规则扩展缝：将来可按锦囊/目标定制
         *        可无懈性；当前实现只用它做语义占位）。
         * @param start 从该玩家起按座位序询问（= 使用锦囊的玩家）。
         * @return true = 被无懈抵消（奇数张无懈）。
         * @note 窗口粒度 = 每个受影响目标一次（调用方按目标调用）；这是
         *       现行规则的简化，官方规则以目标为单位的表述等价于此。
         */
        inline bool resolve_nullification(
            GameContext &ctx, DecisionSource &ai, const card::CardDef &trick,
            const std::string &start)
        {
            (void)trick;
            const auto order = seat_order_from(ctx, start);
            bool cancelled = false;
            for (int round = 0; round < 32; ++round)
            {
                bool any = false;
                for (const auto &p : order)
                {
                    if (try_play_counter(ctx, ai, p))
                    {
                        cancelled = !cancelled;
                        any = true;
                    }
                }
                if (!any)
                    break;
            }
            return cancelled;
        }
    }
}

#endif  // INCLUDE_TKW_GAME_COUNTER_HPP