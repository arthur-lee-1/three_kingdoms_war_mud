/**
 * @file resolver.hpp
 * @brief 效果结算器：按 CardEffectKind 分派，把「打出的牌」作用于对局状态。
 * @note 只实现核心结算（杀/闪/桃/无中生有/过拆/顺牵/南蛮/万箭/决斗/桃园）；
 *       无懈/延时/装备特殊效果等返回 UnsupportedKind，作为明确未接缝。
 *       响应牌消费、目标校验、距离校验都在本模块（单一写者），
 *       玩家主观选择经 DecisionSource 询问。
 */

#ifndef INCLUDE_TKW_GAME_RESOLVER_HPP
#define INCLUDE_TKW_GAME_RESOLVER_HPP

#include <algorithm>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "card/catalog.hpp"
#include "card/def.hpp"
#include "card/manager.hpp"
#include "entity/manager.hpp"
#include "event/event_bus.hpp"
#include "game/combat.hpp"
#include "game/context.hpp"
#include "game/counter.hpp"
#include "game/decision.hpp"
#include "game/distance.hpp"
#include "game/state.hpp"
#include "util/types.hpp"

namespace tkw
{
    namespace game
    {
        /** @brief 结算错误。 */
        enum class EffectError : std::uint8_t
        {
            UnknownCard,     /**< 目录中找不到该卡定义 */
            UnsupportedKind, /**< 该 effect.kind 尚未实现 */
            NoTarget,        /**< 需要至少一个目标 */
            OutOfRange,      /**< 目标不在攻击范围/距离内 */
            InvalidChoice,   /**< 决策源选中的牌不存在于目标区域 */
        };

        template <typename T>
        using GameResult = Result<T, EffectError>;

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

        /** @brief 从 target 的任一区域移除指定牌（填 out 返回被移除的牌）。 */
        inline bool remove_card_from_zones(
            GameContext &ctx, const std::string &entity_id,
            const std::string &instance_id, card::Card &out)
        {
            auto h = ctx.cards->remove_from_hand(entity_id, instance_id);
            if (h.is_some())
            {
                out = std::move(h).unwrap();
                return true;
            }
            auto e = ctx.cards->remove_from_equip(entity_id, instance_id);
            if (e.is_some())
            {
                out = std::move(e).unwrap();
                return true;
            }
            auto j = ctx.cards->remove_from_judge(entity_id, instance_id);
            if (j.is_some())
            {
                out = std::move(j).unwrap();
                return true;
            }
            return false;
        }

        // ── 目标选择 ────────────────────────────────────────────────────

        /** @brief 按 effect.scope 返回该牌在当前局面下的合法目标集合（含距离过滤）。 */
        inline std::vector<std::string> valid_targets(
            const GameContext &ctx, const std::string &player, const card::CardDef &def)
        {
            std::vector<std::string> out;
            if (def.effect.is_none())
                return out;
            const auto &eff = def.effect.unwrap();
            const auto scope = eff.scope.unwrap_or(card::Scope::Self);

            auto all_others = [&]()
            {
                for (const auto &e : *ctx.entities)
                    if (e->get_id() != player)
                        out.push_back(e->get_id());
            };

            switch (scope)
            {
            case card::Scope::Self:
                out.push_back(player);
                break;
            case card::Scope::All:
                for (const auto &e : *ctx.entities)
                    out.push_back(e->get_id());
                break;
            case card::Scope::AllOthers:
                all_others();
                break;
            case card::Scope::OneOther:
                all_others();
                if (eff.kind == card::CardEffectKind::Damage)
                {
                    out.erase(
                        std::remove_if(out.begin(), out.end(),
                                       [&](const std::string &t)
                                       { return !in_attack_range(ctx, player, t); }),
                        out.end());
                }
                else if (eff.kind == card::CardEffectKind::Steal)
                {
                    out.erase(
                        std::remove_if(out.begin(), out.end(),
                                       [&](const std::string &t)
                                       { return !distance_le(ctx, player, t, eff.range); }),
                        out.end());
                }
                break;
            }
            return out;
        }

        // ── 结算入口 ────────────────────────────────────────────────────

        /**
         * @brief 结算「player 打出 played 牌，指定 targets」。
         * @note 校验失败（未知卡/越范围/未实现/空目标）不消耗该牌；
         *       校验通过后先把打出的牌弃置，再应用效果。
         */
        inline GameResult<void> resolve_play(
            GameContext &ctx, DecisionSource &ai, const std::string &player,
            const card::Card &played, const std::vector<std::string> &targets)
        {
            const auto def_opt = ctx.catalog->find(played.def_id);
            if (def_opt.is_none())
                return GameResult<void>::Err(EffectError::UnknownCard);
            const card::CardDef &def = *def_opt.unwrap();
            if (def.effect.is_none())
                return GameResult<void>::Ok();  // 装备/响应牌无主动出牌效果
            const card::CardEffect &eff = def.effect.unwrap();

            // 预校验：目标非空 + 距离
            if (targets.empty())
                return GameResult<void>::Err(EffectError::NoTarget);
            if (eff.kind == card::CardEffectKind::Damage)
            {
                for (const auto &t : targets)
                    if (!in_attack_range(ctx, player, t))
                        return GameResult<void>::Err(EffectError::OutOfRange);
            }
            else if (eff.kind == card::CardEffectKind::Steal)
            {
                for (const auto &t : targets)
                    if (!distance_le(ctx, player, t, eff.range))
                        return GameResult<void>::Err(EffectError::OutOfRange);
            }

            // 未实现的效果：不消耗打出的牌
            switch (eff.kind)
            {
            case card::CardEffectKind::Damage:
            case card::CardEffectKind::AoeDamage:
            case card::CardEffectKind::Heal:
            case card::CardEffectKind::Draw:
            case card::CardEffectKind::DiscardTarget:
            case card::CardEffectKind::Steal:
            case card::CardEffectKind::Duel:
                break;
            default:
                return GameResult<void>::Err(EffectError::UnsupportedKind);
            }

            // 打出的牌弃置（若在手牌中）
            auto played_removed = ctx.cards->remove_from_hand(player, played.instance_id);
            if (played_removed.is_some())
                ctx.cards->discard(std::move(played_removed).unwrap());

            // 无懈可击只抵消锦囊牌；基本牌（杀/闪/桃）不可无懈
            const bool is_trick = def.type == card::CardType::Trick;
            const auto nullified = [&]()
            {
                return is_trick && resolve_nullification(ctx, ai, player);
            };

            switch (eff.kind)
            {
            case card::CardEffectKind::Damage:
            {
                for (const auto &t : targets)
                {
                    bool responded = false;
                    if (eff.response.is_some())
                        responded = request_response(ctx, ai, t, eff.response.unwrap());
                    if (!responded)
                        deal_damage(ctx, ai, player, t, eff.amount);
                }
                return GameResult<void>::Ok();
            }
            case card::CardEffectKind::AoeDamage:
            {
                for (const auto &t : targets)
                {
                    if (nullified())
                        continue;
                    bool responded = false;
                    if (eff.response.is_some())
                        responded = request_response(ctx, ai, t, eff.response.unwrap());
                    if (!responded)
                        deal_damage(ctx, ai, player, t, eff.amount);
                }
                return GameResult<void>::Ok();
            }
            case card::CardEffectKind::Heal:
                for (const auto &t : targets)
                {
                    if (nullified())
                        continue;
                    apply_heal(ctx, t, eff.amount);
                }
                return GameResult<void>::Ok();
            case card::CardEffectKind::Draw:
                if (nullified())
                    return GameResult<void>::Ok();
                apply_draw(ctx, player, eff.count);
                return GameResult<void>::Ok();
            case card::CardEffectKind::DiscardTarget:
            {
                for (const auto &t : targets)
                {
                    if (nullified())
                        continue;
                    const auto picked = ai.pick_card_from_target(ctx, player, t);
                    card::Card removed;
                    if (!remove_card_from_zones(ctx, t, picked.instance_id, removed))
                        return GameResult<void>::Err(EffectError::InvalidChoice);
                    ctx.cards->discard(std::move(removed));
                }
                return GameResult<void>::Ok();
            }
            case card::CardEffectKind::Steal:
            {
                for (const auto &t : targets)
                {
                    if (nullified())
                        continue;
                    const auto picked = ai.pick_card_from_target(ctx, player, t);
                    card::Card removed;
                    if (!remove_card_from_zones(ctx, t, picked.instance_id, removed))
                        return GameResult<void>::Err(EffectError::InvalidChoice);
                    ctx.cards->add_to_hand(player, std::move(removed));
                }
                return GameResult<void>::Ok();
            }
            case card::CardEffectKind::Duel:
            {
                if (nullified())
                    return GameResult<void>::Ok();
                // 目标先开始，轮流打出杀；先不出的受对方 1 点伤害。
                std::string attacker = player;
                std::string defender = targets.front();
                for (int round = 0; round < 64; ++round)
                {
                    if (!request_response(ctx, ai, defender, card::ResponseKind::Sha))
                    {
                        deal_damage(ctx, ai, attacker, defender, eff.amount);
                        return GameResult<void>::Ok();
                    }
                    std::swap(attacker, defender);
                }
                return GameResult<void>::Err(EffectError::UnsupportedKind);
            }
            default:
                return GameResult<void>::Err(EffectError::UnsupportedKind);
            }
        }
    }
}

#endif  // INCLUDE_TKW_GAME_RESOLVER_HPP