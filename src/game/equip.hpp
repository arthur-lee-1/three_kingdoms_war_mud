/**
 * @file equip.hpp
 * @brief 装备区查询：按效果类别/槽位解析已装备的牌。
 * @note 经 catalog 解析装备牌的定义（本模块不持有目录，仅查询）。
 */

#ifndef INCLUDE_TKW_GAME_EQUIP_HPP
#define INCLUDE_TKW_GAME_EQUIP_HPP

#include <string>

#include "card/def.hpp"
#include "game/context.hpp"

namespace tkw
{
    namespace game
    {
        /** @brief 实体装备区是否存在携带指定效果类别的装备（如连弩的无次数限制）。 */
        inline bool has_equipment_effect(
            const GameContext &ctx,
            const std::string &entity_id,
            card::CardEffectKind kind)
        {
            for (const auto &c : ctx.cards->equip(entity_id))
            {
                const auto def = ctx.catalog->find(c.def_id);
                if (def.is_none())
                    continue;
                const auto &eff = def.unwrap()->effect;
                if (eff.is_some() && eff.unwrap().kind == kind)
                    return true;
            }
            return false;
        }
    }
}

#endif  // INCLUDE_TKW_GAME_EQUIP_HPP