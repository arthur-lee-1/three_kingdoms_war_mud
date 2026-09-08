/**
 * @file combat.hpp
 * @brief 战斗流程：伤害 → 濒死救场（桃）→ 死亡声明与击杀奖励。
 * @note 这是 entity/event.hpp 注释里「由 combat 发布」的职责归属：
 *       - hp 扣到非正 → 进入濒死：从濒死角色起按座位序轮询打桃；
 *       - 一轮无人可救/不救 → 死亡：区域牌弃置、发布 EntityDiedEvent、移除实体；
 *       - 击杀者（伤害来源为存活玩家）摸 3 张。
 * @note 无武将技能：能救人的只有「桃」（def_id == "tao"）。
 */

#ifndef INCLUDE_TKW_GAME_COMBAT_HPP
#define INCLUDE_TKW_GAME_COMBAT_HPP

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include "card/catalog.hpp"
#include "card/def.hpp"
#include "card/manager.hpp"
#include "entity/event.hpp"
#include "entity/manager.hpp"
#include "event/event_bus.hpp"
#include "game/context.hpp"
#include "game/decision.hpp"
#include "game/state.hpp"
#include "util/types.hpp"

namespace tkw
{
    namespace game
    {
        /** @brief 该定义是否可作濒死救场牌（数据标记 rescue，不再认 id）。 */
        inline bool is_rescue_def(const card::CardDef &def)
        {
            return def.rescue;
        }

        /** @brief 玩家手牌中是否有桃。 */
        inline bool has_peach(const GameContext &ctx, const std::string &player)
        {
            for (const auto &c : ctx.cards->hand(player))
            {
                const auto def = ctx.catalog->find(c.def_id);
                if (def.is_some() && is_rescue_def(*def.unwrap()))
                    return true;
            }
            return false;
        }

        /** @brief 消耗玩家手牌中的一张桃（移除+弃置+事件）。 */
        inline bool consume_peach(GameContext &ctx, const std::string &player)
        {
            for (const auto &c : ctx.cards->hand(player))
            {
                const auto def = ctx.catalog->find(c.def_id);
                if (def.is_some() && is_rescue_def(*def.unwrap()))
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

        /** @brief 死亡清场：手牌/装备/判定区全部置入弃牌堆，移除实体并发布死亡事件。 */
        inline void declare_death(GameContext &ctx, const std::string &player)
        {
            for (const auto &c : ctx.cards->discard_all(player))
                emit_card_discarded(ctx, player, c);
            auto ev = std::make_shared<EntityDiedEvent>();
            ev->entity_id = player;
            ctx.bus->publish(ev);
            ctx.entities->remove(player);
        }

        /**
         * @brief 濒死救场：hp ≤ 0 时从濒死角色起按座位序轮询打桃。
         * @return true = 死亡；false = 救回（hp > 0）。
         */
        inline bool resolve_dying(
            GameContext &ctx, DecisionSource &ai, const std::string &dying)
        {
            const auto de = ctx.entities->find(dying);
            if (de.is_none())
                return true;
            {
                auto ev = std::make_shared<EntityDyingEvent>();
                ev->target = dying;
                ev->current_hp = de.unwrap()->get_hp();
                ctx.bus->publish(ev);
            }

            // 座位序，从濒死角色开始
            const auto order = ctx.entities->order_from(dying);

            int rounds = 0;
            while (true)
            {
                const auto e = ctx.entities->find(dying);
                if (e.is_none())
                    return true;
                if (e.unwrap()->get_hp() > 0)
                    return false;

                bool progress = false;
                for (const auto &saver : order)
                {
                    if (!has_peach(ctx, saver))
                        continue;
                    if (!ai.play_peach(ctx, saver, dying))
                        continue;
                    consume_peach(ctx, saver);
                    apply_heal(ctx, dying, 1);
                    progress = true;
                    const auto cur = ctx.entities->find(dying);
                    if (cur.is_none())
                        return true;
                    if (cur.unwrap()->get_hp() > 0)
                        return false;
                }
                if (!progress)
                    break;
                if (++rounds > 64)
                    break;  // 保险（桃数量有限，理论上到不了）
            }

            declare_death(ctx, dying);
            return true;
        }

        /**
         * @brief 造成伤害（流程入口）：扣血 → 濒死判定 → 死亡与击杀奖励。
         * @param source 伤害来源（空串 = 无来源如闪电；为存活玩家时击杀得奖励）。
         */
        inline void deal_damage(
            GameContext &ctx, DecisionSource &ai, const std::string &source,
            const std::string &target, int amount)
        {
            const auto e = ctx.entities->find(target);
            if (e.is_none())
                return;
            e.unwrap()->take_damage(source, amount, false);
            if (e.unwrap()->get_hp() > 0)
                return;

            const bool died = resolve_dying(ctx, ai, target);
            if (died && !source.empty())
            {
                const auto killer = ctx.entities->find(source);
                if (killer.is_some())
                    apply_draw(ctx, source, 3);  // 击杀奖励
            }
        }
    }
}

#endif  // INCLUDE_TKW_GAME_COMBAT_HPP