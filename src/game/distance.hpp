/**
 * @file distance.hpp
 * @brief 距离与攻击范围：座次环距离 + 装备（武器攻击范围 / 坐骑 ±1）修正。
 * @note 规则约定：
 *       - 座次距离 = min(|a-b|, n-|a-b|)（环）；
 *       - 攻击范围 = 武器 range，无武器为 1；
 *       - -1马（进攻马）：使用者计算到他人距离 -1；
 *       - +1马（防御马）：他人计算到自己的距离 +1；
 *       - 距离下限为 1。
 */

#ifndef INCLUDE_TKW_GAME_DISTANCE_HPP
#define INCLUDE_TKW_GAME_DISTANCE_HPP

#include <algorithm>
#include <cstdlib>
#include <string>
#include <vector>

#include "card/def.hpp"
#include "game/context.hpp"
#include "util/types.hpp"

namespace tkw
{
    namespace game
    {
        /** @brief 从装备区解析出的距离相关摘要。 */
        struct EquipSummary
        {
            int weapon_range = 0;         /**< 0 = 无武器（基础攻击距离 1） */
            bool offensive_horse = false; /**< -1马 */
            bool defensive_horse = false; /**< +1马 */
        };

        /** @brief 座次距离（环）：min(|a-b|, n-|a-b|)。 */
        inline int seat_distance(
            const GameContext &ctx, const std::string &a, const std::string &b)
        {
            const auto ea = ctx.entities->find(a);
            const auto eb = ctx.entities->find(b);
            if (ea.is_none() || eb.is_none())
                return 0;
            const int n = static_cast<int>(ctx.entities->size());
            const int d = std::abs(ea.unwrap()->get_seat() - eb.unwrap()->get_seat());
            return std::min(d, n - d);
        }

        /** @brief 解析某实体装备区：武器 range 与坐骑方向（经 catalog）。 */
        inline EquipSummary summarize_equipment(
            const GameContext &ctx, const std::string &entity_id)
        {
            EquipSummary s;
            for (const auto &c : ctx.cards->equip(entity_id))
            {
                const auto def = ctx.catalog->find(c.def_id);
                if (def.is_none())
                    continue;
                const auto &eq = def.unwrap()->equip;
                if (eq.is_none())
                    continue;
                const auto &e = eq.unwrap();
                if (e.slot == card::EquipSlot::Weapon)
                    s.weapon_range = e.range;
                else if (e.slot == card::EquipSlot::Horse)
                {
                    if (e.direction.contains(card::HorseDirection::Offensive))
                        s.offensive_horse = true;
                    else if (e.direction.contains(card::HorseDirection::Defensive))
                        s.defensive_horse = true;
                }
            }
            return s;
        }

        /**
         * @brief 距离判定：from 到 to 的距离（含马修正）是否 ≤ range。
         * @note 顺手牵羊（range=1）等按距离结算的牌走这里。
         */
        inline bool distance_le(
            const GameContext &ctx, const std::string &from,
            const std::string &to, int range)
        {
            int d = seat_distance(ctx, from, to);
            const auto fs = summarize_equipment(ctx, from);
            const auto ts = summarize_equipment(ctx, to);
            if (fs.offensive_horse)
                --d;
            if (ts.defensive_horse)
                ++d;
            d = std::max(d, 1);
            return d <= range;
        }

        /** @brief 攻击距离判定：from 能否攻击 to（武器 range，无武器为 1）。 */
        inline bool in_attack_range(
            const GameContext &ctx, const std::string &from, const std::string &to)
        {
            const int range = summarize_equipment(ctx, from).weapon_range;
            return distance_le(ctx, from, to, range > 0 ? range : 1);
        }
    }
}

#endif  // INCLUDE_TKW_GAME_DISTANCE_HPP