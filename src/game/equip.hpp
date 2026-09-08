/**
 * @file equip.hpp
 * @brief 装备区查询：按被动能力解析已装备的牌。
 * @note 经 catalog 解析装备牌的定义（本模块不持有目录，仅查询）。
 */

#ifndef INCLUDE_TKW_GAME_EQUIP_HPP
#define INCLUDE_TKW_GAME_EQUIP_HPP

#include <algorithm>
#include <string>

#include "card/def.hpp"
#include "game/context.hpp"

namespace tkw
{
    namespace game
    {
        /**
         * @brief 返回实体装备区中第一件带指定能力的装备定义；无则 nullptr。
         * @note 需要读取装备上的判定描述（如八卦阵）时用本函数。
         */
        inline const card::CardDef *find_equipment(
            const GameContext &ctx,
            const std::string &entity_id,
            card::Ability ability)
        {
            for (const auto &c : ctx.cards->equip(entity_id))
            {
                const auto def = ctx.catalog->find(c.def_id);
                if (def.is_none())
                    continue;
                const card::CardDef &d = *def.unwrap();
                if (std::find(d.abilities.begin(), d.abilities.end(), ability) !=
                    d.abilities.end())
                    return &d;
            }
            return nullptr;
        }

        /** @brief 实体装备区是否存在带指定能力的装备。 */
        inline bool has_ability(
            const GameContext &ctx,
            const std::string &entity_id,
            card::Ability ability)
        {
            return find_equipment(ctx, entity_id, ability) != nullptr;
        }
    }
}

#endif  // INCLUDE_TKW_GAME_EQUIP_HPP
