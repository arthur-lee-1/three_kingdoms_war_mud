/**
 * @file state.hpp
 * @brief 对局状态的基础操作：扣血/回血/摸牌（不含濒死死亡，那些归 combat.hpp）。
 * @note 这些是「状态层」原语：只改实体状态与牌堆，不发布流程事件、
 *       不处理死亡。伤害→濒死→死亡的流程归 combat.hpp 的 deal_damage。
 */

#ifndef INCLUDE_TKW_GAME_STATE_HPP
#define INCLUDE_TKW_GAME_STATE_HPP

#include <string>
#include <utility>

#include "card/manager.hpp"
#include "entity/manager.hpp"
#include "game/context.hpp"
#include "util/types.hpp"

namespace tkw
{
    namespace game
    {
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
                ctx.cards->add_to_hand(player, std::move(c).unwrap());
                ++drew;
            }
            return drew;
        }
    }
}

#endif  // INCLUDE_TKW_GAME_STATE_HPP