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
#include "game/context.hpp"
#include "game/decision.hpp"
#include "util/types.hpp"

namespace tkw
{
    namespace game
    {
        /** @brief 该定义是否为无懈可击。 */
        inline bool is_counter_def(const card::CardDef &def)
        {
            return def.id == "wuxie";
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
                        ctx.cards->discard(std::move(removed).unwrap());
                    return true;
                }
            }
            return false;
        }

        /** @brief 座位序（从 start 开始环绕）。 */
        inline std::vector<std::string> seat_order_from(
            const GameContext &ctx, const std::string &start)
        {
            std::vector<std::string> order;
            for (const auto &ent : *ctx.entities)
                order.push_back(ent->get_id());
            const auto it = std::find(order.begin(), order.end(), start);
            if (it != order.end())
                std::rotate(order.begin(), it, order.end());
            return order;
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
         * @param start 从该玩家起按座位序询问（= 使用锦囊的玩家）。
         * @return true = 被无懈抵消（奇数张无懈）。
         */
        inline bool resolve_nullification(
            GameContext &ctx, DecisionSource &ai, const std::string &start)
        {
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