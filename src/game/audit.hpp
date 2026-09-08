/**
 * @file audit.hpp
 * @brief 牌堆能力审计：找出引擎尚不能结算的主动效果卡。
 * @note 语义归属 game 层，catalog 保持不依赖 resolver；应用层在开局前
 *       用它提示「牌堆含未实现卡」。
 */

#ifndef INCLUDE_TKW_GAME_AUDIT_HPP
#define INCLUDE_TKW_GAME_AUDIT_HPP

#include <string>
#include <vector>

#include "card/catalog.hpp"
#include "card/def.hpp"
#include "game/resolver.hpp"

namespace tkw
{
    namespace game
    {
        /**
         * @brief 返回牌堆中「本应可主动打出但引擎尚未实现」的卡 id（deck 序）。
         * @note 装备牌打出即装备、闪/无懈为响应牌，均不计入未实现；
         *       延时锦囊的判定阶段已实现，但出牌阶段尚无法放置，故计入。
         */
        inline std::vector<std::string> unsupported_cards(
            const card::CardDefCatalog &catalog)
        {
            std::vector<std::string> out;
            for (const auto &def : catalog)
            {
                if (def.type == card::CardType::Equipment)
                    continue;
                if (def.effect.is_some())
                {
                    if (is_unimplemented_active_kind(def.effect.unwrap().kind))
                        out.push_back(def.id);
                }
                else if (def.judge.is_some())
                {
                    out.push_back(def.id);
                }
            }
            return out;
        }
    }
}

#endif  // INCLUDE_TKW_GAME_AUDIT_HPP
