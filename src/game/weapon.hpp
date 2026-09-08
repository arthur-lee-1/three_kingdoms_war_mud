/**
 * @file weapon.hpp
 * @brief 「杀」的完整结算：响应窗口（闪/八卦阵）+ 装备效果挂钩。
 * @note 规则约定（简单版）：
 *       - 仁王盾：黑杀无效（青釭剑无视防具可穿透）；
 *       - 八卦阵：需出闪时可判定，红桃/方块视为闪（青釭剑下失效）；
 *       - 青龙偃月刀：被闪后可再对同一目标使用一张杀；
 *       - 贯石斧：被闪后可弃两张牌令杀依然命中；
 *       - 寒冰剑：命中前可防止伤害改为弃置目标两张牌；
 *       - 麒麟弓：造成伤害后可弃置目标一匹坐骑。
 * @note 雌雄双股剑依赖性别系统、方天画戟影响目标选择，均未在此结算。
 */

#ifndef INCLUDE_TKW_GAME_WEAPON_HPP
#define INCLUDE_TKW_GAME_WEAPON_HPP

#include <string>
#include <utility>
#include <vector>

#include "card/catalog.hpp"
#include "card/def.hpp"
#include "card/manager.hpp"
#include "game/combat.hpp"
#include "game/context.hpp"
#include "game/decision.hpp"
#include "game/equip.hpp"
#include "game/response.hpp"
#include "game/state.hpp"
#include "util/types.hpp"

namespace tkw
{
    namespace game
    {
        /** @brief 攻击者手牌中的第一张杀（青龙偃月刀续杀用）。 */
        inline Option<card::Card> find_sha_in_hand(
            const GameContext &ctx, const std::string &player)
        {
            for (const auto &c : ctx.cards->hand(player))
            {
                const auto def = ctx.catalog->find(c.def_id);
                if (def.is_some() && is_response_def(*def.unwrap(), card::ResponseKind::Sha))
                    return Option<card::Card>::Some(c);
            }
            return Option<card::Card>::None();
        }

        /** @brief 弃置目标 count 张牌（寒冰剑）。 */
        inline void discard_target_cards(
            GameContext &ctx, DecisionSource &ai,
            const std::string &attacker, const std::string &target, int count)
        {
            for (int i = 0; i < count; ++i)
            {
                const auto picked = ai.pick_card_from_target(ctx, attacker, target);
                card::Card removed;
                if (remove_card_from_zones(ctx, target, picked.instance_id, removed))
                {
                    ctx.cards->discard(removed);
                    emit_card_discarded(ctx, target, removed);
                }
            }
        }

        /** @brief 弃置目标装备区的一匹坐骑（麒麟弓）。 */
        inline bool discard_first_horse(GameContext &ctx, const std::string &target)
        {
            for (const auto &c : ctx.cards->equip(target))
            {
                const auto def = ctx.catalog->find(c.def_id);
                if (def.is_some() && def.unwrap()->equip.is_some() &&
                    def.unwrap()->equip.unwrap().slot == card::EquipSlot::Horse)
                {
                    auto removed = ctx.cards->remove_from_equip(target, c.instance_id);
                    if (removed.is_some())
                    {
                        card::Card card = std::move(removed).unwrap();
                        ctx.cards->discard(card);
                        emit_card_discarded(ctx, target, card);
                    }
                    return true;
                }
            }
            return false;
        }

        /**
         * @brief 「杀」结算：attacker 对 target 使用杀。
         * @param sha 该杀的卡牌对象（花色用于仁王盾黑杀判定）。
         * @param amount 伤害量（config 驱动，当前数据均为 1）。
         */
        inline void resolve_sha(
            GameContext &ctx, DecisionSource &ai, const std::string &attacker,
            const card::Card &sha, const std::string &target, int amount)
        {
            const bool ignore_armor =
                has_ability(ctx, attacker, card::Ability::IgnoreArmor);

            // 仁王盾：黑色的杀对你无效（青釭剑可穿透）
            if (!ignore_armor &&
                has_ability(ctx, target, card::Ability::BlackShaImmune) &&
                is_black_suit(sha.suit))
                return;

            // 响应窗口：八卦阵判定（判定条件来自装备数据）或打出闪
            bool responded = false;
            if (!ignore_armor)
            {
                const card::CardDef *armor =
                    find_equipment(ctx, target, card::Ability::JudgementJink);
                if (armor && armor->judge.is_some())
                {
                    auto judge = perform_judgement(ctx);
                    if (judge.is_some())
                    {
                        const card::Card judge_card = std::move(judge).unwrap();
                        ctx.cards->discard(judge_card);  // 判定牌进弃牌堆
                        emit_card_discarded(ctx, target, judge_card);
                        if (judge_result(armor->judge.unwrap(), judge_card) ==
                            card::JudgeAction::Jink)
                            responded = true;
                    }
                }
            }
            if (!responded)
                responded = request_response(ctx, ai, target, card::ResponseKind::Jink);

            // 青龙偃月刀：目标打出闪后可再对同一目标使用一张杀
            if (responded &&
                has_ability(ctx, attacker, card::Ability::ExtraShaAfterJink) &&
                ai.trigger_effect(ctx, attacker, card::Ability::ExtraShaAfterJink))
            {
                auto extra = find_sha_in_hand(ctx, attacker);
                if (extra.is_some())
                {
                    auto removed = ctx.cards->remove_from_hand(attacker, extra.unwrap().instance_id);
                    if (removed.is_some())
                    {
                        card::Card extra_card = std::move(removed).unwrap();
                        ctx.cards->discard(extra_card);
                        emit_card_played(ctx, attacker, extra_card);
                        emit_card_discarded(ctx, attacker, extra_card);
                    }
                    resolve_sha(ctx, ai, attacker, extra.unwrap(), target, amount);
                }
            }

            // 贯石斧：目标打出闪后可弃两张牌令杀依然命中
            if (responded &&
                has_ability(ctx, attacker, card::Ability::DiscardTwoForceDamage) &&
                ai.trigger_effect(ctx, attacker, card::Ability::DiscardTwoForceDamage))
            {
                const auto discards = ai.choose_discards(ctx, attacker, 2);
                for (const auto &id : discards)
                {
                    auto removed = ctx.cards->remove_from_hand(attacker, id);
                    if (removed.is_some())
                    {
                        card::Card card = std::move(removed).unwrap();
                        ctx.cards->discard(card);
                        emit_card_discarded(ctx, attacker, card);
                    }
                }
                responded = false;  // 强制命中
            }

            if (!responded)
            {
                // 寒冰剑：防止伤害改为弃置目标两张牌
                if (has_ability(ctx, attacker, card::Ability::DamageAsDiscard) &&
                    ai.trigger_effect(ctx, attacker, card::Ability::DamageAsDiscard))
                {
                    discard_target_cards(ctx, ai, attacker, target, 2);
                    return;
                }

                deal_damage(ctx, ai, attacker, target, amount);

                // 麒麟弓：造成伤害后可弃置目标一匹坐骑
                if (has_ability(ctx, attacker, card::Ability::DiscardHorseOnDamage) &&
                    ai.trigger_effect(
                        ctx, attacker, card::Ability::DiscardHorseOnDamage))
                    discard_first_horse(ctx, target);
            }
        }
    }
}

#endif  // INCLUDE_TKW_GAME_WEAPON_HPP