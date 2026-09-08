/**
 * @file response.hpp
 * @brief 响应窗口：目标打出「杀/闪」等响应牌。
 * @note 响应牌的消费（移除+弃置）由本模块负责（单一写者），
 *       是否响应由 DecisionSource 决定。
 */

#ifndef INCLUDE_TKW_GAME_RESPONSE_HPP
#define INCLUDE_TKW_GAME_RESPONSE_HPP

#include <string>
#include <utility>

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
        /** @brief 该定义是否可作为指定响应牌（杀=effect.kind==Damage，闪==Jink）。 */
        inline bool is_response_def(const card::CardDef &def, card::ResponseKind kind)
        {
            if (def.effect.is_none())
                return false;
            const auto k = def.effect.unwrap().kind;
            switch (kind)
            {
            case card::ResponseKind::Sha:
                return k == card::CardEffectKind::Damage;
            case card::ResponseKind::Jink:
                return k == card::CardEffectKind::Jink;
            }
            return false;
        }

        /** @brief 实体手牌中是否存在指定响应牌。 */
        inline bool has_response_card(
            const GameContext &ctx, const std::string &entity_id, card::ResponseKind kind)
        {
            for (const auto &c : ctx.cards->hand(entity_id))
            {
                const auto def = ctx.catalog->find(c.def_id);
                if (def.is_some() && is_response_def(*def.unwrap(), kind))
                    return true;
            }
            return false;
        }

        /**
         * @brief 开响应窗口：先看实体是否有响应牌，有则询问决策源，
         *        决定打出则消费该牌（移除+弃置）。返回是否成功响应。
         */
        inline bool request_response(
            GameContext &ctx, DecisionSource &ai,
            const std::string &entity_id, card::ResponseKind kind)
        {
            if (!has_response_card(ctx, entity_id, kind))
                return false;
            if (!ai.play_response(ctx, entity_id, kind))
                return false;
            for (const auto &c : ctx.cards->hand(entity_id))
            {
                const auto def = ctx.catalog->find(c.def_id);
                if (def.is_some() && is_response_def(*def.unwrap(), kind))
                {
                    auto removed = ctx.cards->remove_from_hand(entity_id, c.instance_id);
                    if (removed.is_some())
                        ctx.cards->discard(std::move(removed).unwrap());
                    return true;
                }
            }
            return false;
        }
    }
}

#endif  // INCLUDE_TKW_GAME_RESPONSE_HPP