/**
 * @file weapon.hpp
 * @brief 「杀」结算管线：防具 → 响应 → 被闪后 → 命中前 → 伤害 → 命中后。
 * @note 6 件装备的效果不再写成 if 链，而是注册到 sha_hook_table()：
 *       每项声明「能力 / 插桩点 / 挂在哪一方 / 回调」，新增武器只加一行。
 * @note 规则约定（简单版）：
 *       - 仁王盾：黑杀无效（青釭剑无视防具可穿透）；
 *       - 八卦阵：需出闪时可判定，判定描述来自装备数据（当前为红色=闪）；
 *       - 青龙偃月刀：被闪后可再对同一目标使用一张杀；
 *       - 贯石斧：被闪后可弃两张牌令杀依然命中；
 *       - 寒冰剑：命中前可防止伤害改为弃置目标两张牌；
 *       - 麒麟弓：造成伤害后可弃置目标一匹坐骑。
 * @note 雌雄双股剑依赖性别、方天画戟影响目标选择，均未在此结算。
 */

#ifndef INCLUDE_TKW_GAME_WEAPON_HPP
#define INCLUDE_TKW_GAME_WEAPON_HPP

#include <cstdint>
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
        /** @brief 「杀」结算上下文：钩子读写的共享状态。 */
        struct ShaContext
        {
            GameContext &ctx;
            DecisionSource &ai;
            const card::Card &sha;
            std::string attacker;
            std::string target;
            int amount = 1;
            bool ignore_armor = false; /**< 攻击方无视防具（青釭剑） */
            bool responded = false;    /**< 目标已打出/视为闪 */
            bool blocked = false;      /**< 防具直接无效（仁王盾） */
            bool prevented = false;    /**< 伤害被替代（寒冰剑） */
        };

        inline void resolve_sha(
            GameContext &ctx, DecisionSource &ai, const std::string &attacker,
            const card::Card &sha, const std::string &target, int amount);

        // ── 装备效果（钩子实现）────────────────────────────────────────

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

        /** @brief 仁王盾：黑色的杀对你无效（青釭剑可穿透）。 */
        inline void hook_renwang(ShaContext &sc)
        {
            if (!sc.ignore_armor && is_black_suit(sc.sha.suit))
                sc.blocked = true;
        }

        /** @brief 八卦阵：需出闪时判定，判定描述来自装备数据。 */
        inline void hook_bagua(ShaContext &sc)
        {
            if (sc.ignore_armor || sc.responded)
                return;
            const card::CardDef *armor = find_equipment(
                sc.ctx, sc.target, card::Ability::JudgementJink);
            if (!armor || armor->judge.is_none())
                return;
            auto judge = perform_judgement(sc.ctx);
            if (judge.is_none())
                return;
            const card::Card judge_card = std::move(judge).unwrap();
            sc.ctx.cards->discard(judge_card);
            emit_card_discarded(sc.ctx, sc.target, judge_card);
            if (judge_result(armor->judge.unwrap(), judge_card) ==
                card::JudgeAction::Jink)
                sc.responded = true;
        }

        /** @brief 青龙偃月刀：目标打出闪后可再对同一目标使用一张杀。 */
        inline void hook_qinglong(ShaContext &sc)
        {
            if (!sc.ai.trigger_effect(
                    sc.ctx, sc.attacker, card::Ability::ExtraShaAfterJink))
                return;
            auto extra = find_sha_in_hand(sc.ctx, sc.attacker);
            if (extra.is_none())
                return;
            auto removed =
                sc.ctx.cards->remove_from_hand(sc.attacker, extra.unwrap().instance_id);
            if (removed.is_some())
            {
                card::Card extra_card = std::move(removed).unwrap();
                sc.ctx.cards->discard(extra_card);
                emit_card_played(sc.ctx, sc.attacker, extra_card);
                emit_card_discarded(sc.ctx, sc.attacker, extra_card);
            }
            resolve_sha(
                sc.ctx, sc.ai, sc.attacker, extra.unwrap(), sc.target, sc.amount);
        }

        /** @brief 贯石斧：目标打出闪后可弃两张牌令杀依然命中。 */
        inline void hook_guanshi(ShaContext &sc)
        {
            if (!sc.ai.trigger_effect(
                    sc.ctx, sc.attacker, card::Ability::DiscardTwoForceDamage))
                return;
            const auto discards = sc.ai.choose_discards(sc.ctx, sc.attacker, 2);
            for (const auto &id : discards)
            {
                auto removed = sc.ctx.cards->remove_from_hand(sc.attacker, id);
                if (removed.is_some())
                {
                    card::Card card = std::move(removed).unwrap();
                    sc.ctx.cards->discard(card);
                    emit_card_discarded(sc.ctx, sc.attacker, card);
                }
            }
            sc.responded = false;  // 强制命中
        }

        /** @brief 寒冰剑：防止伤害改为弃置目标两张牌。 */
        inline void hook_hanbing(ShaContext &sc)
        {
            if (!sc.ai.trigger_effect(
                    sc.ctx, sc.attacker, card::Ability::DamageAsDiscard))
                return;
            discard_target_cards(sc.ctx, sc.ai, sc.attacker, sc.target, 2);
            sc.prevented = true;
        }

        /** @brief 麒麟弓：造成伤害后可弃置目标一匹坐骑。 */
        inline void hook_qilin(ShaContext &sc)
        {
            if (sc.ai.trigger_effect(
                    sc.ctx, sc.attacker, card::Ability::DiscardHorseOnDamage))
                discard_first_horse(sc.ctx, sc.target);
        }

        // ── 管线 ────────────────────────────────────────────────────────

        /** @brief 插桩点（顺序即规则顺序）。 */
        enum class ShaPhase : std::uint8_t
        {
            Armor,     /**< 防具拦截 */
            Respond,   /**< 响应窗口（判定/出闪） */
            PostJink,  /**< 被闪之后 */
            PreDamage, /**< 命中之前 */
            OnHit,     /**< 造成伤害之后 */
        };

        /** @brief 钩子注册项：能力 / 插桩点 / 挂在哪一方 / 回调。 */
        struct ShaHook
        {
            card::Ability ability;
            ShaPhase phase;
            bool attacker_side;
            void (*fn)(ShaContext &);
        };

        /** @brief 全部装备钩子（新增武器 = 加一行）。 */
        inline const std::vector<ShaHook> &sha_hook_table()
        {
            static const std::vector<ShaHook> table = {
                {card::Ability::BlackShaImmune, ShaPhase::Armor, false, hook_renwang},
                {card::Ability::JudgementJink, ShaPhase::Respond, false, hook_bagua},
                {card::Ability::ExtraShaAfterJink, ShaPhase::PostJink, true,
                 hook_qinglong},
                {card::Ability::DiscardTwoForceDamage, ShaPhase::PostJink, true,
                 hook_guanshi},
                {card::Ability::DamageAsDiscard, ShaPhase::PreDamage, true, hook_hanbing},
                {card::Ability::DiscardHorseOnDamage, ShaPhase::OnHit, true, hook_qilin},
            };
            return table;
        }

        /** @brief 执行某插桩点上所有已装备能力的钩子。 */
        inline void run_sha_phase(ShaContext &sc, ShaPhase phase)
        {
            for (const auto &h : sha_hook_table())
            {
                if (h.phase != phase)
                    continue;
                const std::string &owner = h.attacker_side ? sc.attacker : sc.target;
                if (has_ability(sc.ctx, owner, h.ability))
                    h.fn(sc);
            }
        }

        /**
         * @brief 「杀」结算主流程：attacker 对 target 使用杀。
         * @param sha 该杀的卡牌对象（花色用于仁王盾黑杀判定）。
         * @param amount 伤害量（config 驱动，当前数据均为 1）。
         */
        inline void resolve_sha(
            GameContext &ctx, DecisionSource &ai, const std::string &attacker,
            const card::Card &sha, const std::string &target, int amount)
        {
            ShaContext sc{ctx, ai, sha, attacker, target, amount};
            sc.ignore_armor = has_ability(ctx, attacker, card::Ability::IgnoreArmor);

            run_sha_phase(sc, ShaPhase::Armor);
            if (sc.blocked)
                return;

            run_sha_phase(sc, ShaPhase::Respond);
            if (!sc.responded)
                sc.responded =
                    request_response(ctx, ai, target, card::ResponseKind::Jink);

            if (sc.responded)
                run_sha_phase(sc, ShaPhase::PostJink);

            if (!sc.responded)
            {
                run_sha_phase(sc, ShaPhase::PreDamage);
                if (sc.prevented)
                    return;
                deal_damage(ctx, ai, attacker, target, amount);
                run_sha_phase(sc, ShaPhase::OnHit);
            }
        }
    }
}

#endif  // INCLUDE_TKW_GAME_WEAPON_HPP
