/**
 * @file turn.hpp
 * @brief 回合流程：判定 → 摸牌 → 出牌（含杀次数限制/装备）→ 弃牌。
 * @note 规则约定：
 *       - 判定阶段按判定区顺序结算延时锦囊；乐不思蜀判定非红桃跳过出牌，
 *         闪电判定黑桃2~9 则造成雷伤、否则移入下家判定区；
 *       - 杀每回合限一次，装备诸葛连弩后不限制；
 *       - 弃牌阶段手牌上限 = 体力上限。
 * @note 死亡/濒死救场不在本模块（hp 可被扣到非正，死亡声明归后续流程）。
 */

#ifndef INCLUDE_TKW_GAME_TURN_HPP
#define INCLUDE_TKW_GAME_TURN_HPP

#include <algorithm>
#include <cstdint>
#include <limits>
#include <random>
#include <string>
#include <utility>
#include <vector>

#include "card/card.hpp"
#include "card/def.hpp"
#include "card/manager.hpp"
#include "game/context.hpp"
#include "game/decision.hpp"
#include "game/distance.hpp"
#include "game/equip.hpp"
#include "game/resolver.hpp"
#include "util/types.hpp"

namespace tkw
{
    namespace game
    {
        /** @brief 回合流程错误。 */
        enum class TurnError : std::uint8_t
        {
            UnknownPlayer,       /**< 实体不存在 */
            UnknownCard,         /**< 目录中找不到该卡定义 */
            CardNotInHand,       /**< 要打出的牌不在手牌中 */
            InvalidTarget,       /**< 目标不在合法目标集合内 */
            ShaLimitExceeded,    /**< 本回合杀次数已达上限 */
            NotEquipment,        /**< 装备动作目标不是装备牌 */
            PlayRejected,        /**< 结算器拒绝该效果 */
            DiscardInsufficient, /**< 弃牌数量不足/引用了不存在的牌 */
            JudgeEmptyDeck,      /**< 判定时摸牌堆与弃牌堆皆空 */
        };

        template <typename T>
        using TurnResult = Result<T, TurnError>;

        /** @brief 延时锦囊判定结果。 */
        enum class DelayedOutcome : std::uint8_t
        {
            Normal,          /**< 判定后无特殊效果（乐不思蜀为红桃） */
            SkipPlay,        /**< 跳过出牌阶段（乐不思蜀非红桃） */
            LightningStruck, /**< 闪电劈中 */
            PassedToNext,    /**< 闪电未劈中，移至下家判定区 */
        };

        // ── 判定 ────────────────────────────────────────────────────────

        /** @brief 判定：从摸牌堆顶揭示一张（牌堆空则弃牌堆洗回）。 */
        inline TurnResult<card::Card> perform_judgement(
            GameContext &ctx, std::mt19937 &rng)
        {
            if (ctx.cards->draw_size() == 0)
            {
                if (ctx.cards->discard_size() == 0)
                    return TurnResult<card::Card>::Err(TurnError::JudgeEmptyDeck);
                ctx.cards->refill_draw(rng);
            }
            auto c = ctx.cards->draw();
            if (c.is_none())
                return TurnResult<card::Card>::Err(TurnError::JudgeEmptyDeck);
            return TurnResult<card::Card>::Ok(std::move(c).unwrap());
        }

        /** @brief 下家（环座位序；死亡跳过留待后续）。 */
        inline std::string next_player(const GameContext &ctx, const std::string &player)
        {
            const auto e = ctx.entities->find(player);
            if (e.is_none())
                return player;
            const int n = static_cast<int>(ctx.entities->size());
            const int next_seat = (e.unwrap()->get_seat() + 1) % n;
            for (const auto &ent : *ctx.entities)
                if (ent->get_seat() == next_seat)
                    return ent->get_id();
            return player;
        }

        /**
         * @brief 结算玩家判定区的一张延时锦囊（判定牌进弃牌堆；延时牌按结果
         *        弃置或移入下家判定区，从玩家判定区移除）。
         */
        inline TurnResult<DelayedOutcome> resolve_delayed(
            GameContext &ctx,
            std::mt19937 &rng,
            const std::string &player,
            const card::Card &delayed)
        {
            const auto def_opt = ctx.catalog->find(delayed.def_id);
            if (def_opt.is_none())
                return TurnResult<DelayedOutcome>::Err(TurnError::UnknownCard);
            const auto &eff = def_opt.unwrap()->effect;

            // 先从判定区移除延时牌（各分支决定弃置或移送下家）
            auto removed = ctx.cards->remove_from_judge(player, delayed.instance_id);
            const card::Card delayed_card =
                removed.is_some() ? std::move(removed).unwrap() : delayed;

            auto judge = perform_judgement(ctx, rng);
            if (judge.is_err())
                return TurnResult<DelayedOutcome>::Err(judge.unwrap_err());
            const card::Card judge_card = std::move(judge).unwrap();
            ctx.cards->discard(judge_card);  // 判定牌进弃牌堆

            if (eff.is_none())
            {
                ctx.cards->discard(delayed_card);
                return TurnResult<DelayedOutcome>::Ok(DelayedOutcome::Normal);
            }

            switch (eff.unwrap().kind)
            {
            case card::CardEffectKind::DelayedPlaySkip:
                // 乐不思蜀：非红桃 → 跳过出牌阶段
                ctx.cards->discard(delayed_card);
                if (judge_card.suit != card::Suit::Heart)
                    return TurnResult<DelayedOutcome>::Ok(DelayedOutcome::SkipPlay);
                return TurnResult<DelayedOutcome>::Ok(DelayedOutcome::Normal);

            case card::CardEffectKind::Lightning:
            {
                const bool struck = judge_card.suit == card::Suit::Spade &&
                                    judge_card.number >= 2 && judge_card.number <= 9;
                if (struck)
                {
                    ctx.cards->discard(delayed_card);
                    apply_damage(ctx, "闪电", player, eff.unwrap().amount);
                    return TurnResult<DelayedOutcome>::Ok(
                        DelayedOutcome::LightningStruck);
                }
                // 未劈中 → 移入下家判定区
                ctx.cards->add_to_judge(next_player(ctx, player), delayed_card);
                return TurnResult<DelayedOutcome>::Ok(DelayedOutcome::PassedToNext);
            }

            default:
                ctx.cards->discard(delayed_card);
                return TurnResult<DelayedOutcome>::Ok(DelayedOutcome::Normal);
            }
        }

        // ── 出牌阶段辅助 ────────────────────────────────────────────────

        /** @brief 该定义是否为「杀」（effect.kind == Damage）。 */
        inline bool is_sha(const card::CardDef &def)
        {
            return def.effect.is_some() &&
                   def.effect.unwrap().kind == card::CardEffectKind::Damage;
        }

        /** @brief 本回合杀次数上限（诸葛连弩 = 不限）。 */
        inline int sha_limit(const GameContext &ctx, const std::string &player)
        {
            if (has_equipment_effect(ctx, player, card::CardEffectKind::NoShaLimit))
                return std::numeric_limits<int>::max();
            return 1;
        }

        /** @brief 从手牌找一张牌（返回副本，便于随后按 instance_id 消费）。 */
        inline Option<card::Card> find_in_hand(
            const GameContext &ctx,
            const std::string &player,
            const std::string &instance_id)
        {
            for (const auto &c : ctx.cards->hand(player))
                if (c.instance_id == instance_id)
                    return Option<card::Card>::Some(c);
            return Option<card::Card>::None();
        }

        /**
         * @brief 装备动作：手牌装备到装备区；同槽位已有装备则先弃置旧装备。
         */
        inline TurnResult<void> equip_card(
            GameContext &ctx, const std::string &player, const card::Card &card)
        {
            const auto def = ctx.catalog->find(card.def_id);
            if (def.is_none() || def.unwrap()->equip.is_none())
                return TurnResult<void>::Err(TurnError::NotEquipment);
            const auto slot = def.unwrap()->equip.unwrap().slot;

            for (const auto &c : ctx.cards->equip(player))
            {
                const auto d = ctx.catalog->find(c.def_id);
                if (d.is_some() && d.unwrap()->equip.is_some() &&
                    d.unwrap()->equip.unwrap().slot == slot)
                {
                    auto old = ctx.cards->remove_from_equip(player, c.instance_id);
                    if (old.is_some())
                        ctx.cards->discard(std::move(old).unwrap());
                }
            }

            auto removed = ctx.cards->remove_from_hand(player, card.instance_id);
            if (removed.is_none())
                return TurnResult<void>::Err(TurnError::CardNotInHand);
            ctx.cards->add_to_equip(player, std::move(removed).unwrap());
            return TurnResult<void>::Ok();
        }

        // ── 回合入口 ────────────────────────────────────────────────────

        /**
         * @brief 执行 player 的一个完整回合：判定 → 摸2 → 出牌 → 弃牌。
         * @note 出牌阶段循环向 DecisionSource 要动作直到结束；非法动作
         *       （手牌不存在/目标非法/超杀次数）立即报错并中止本回合。
         */
        inline TurnResult<void> execute_turn(
            GameContext &ctx,
            DecisionSource &ai,
            std::mt19937 &rng,
            const std::string &player)
        {
            const auto p = ctx.entities->find(player);
            if (p.is_none())
                return TurnResult<void>::Err(TurnError::UnknownPlayer);

            // 1. 判定阶段
            bool skip_play = false;
            const auto judge_zone = ctx.cards->judge(player);  // 拷贝
            for (const auto &delayed : judge_zone)
            {
                auto r = resolve_delayed(ctx, rng, player, delayed);
                if (r.is_err())
                    return TurnResult<void>::Err(r.unwrap_err());
                if (r.unwrap() == DelayedOutcome::SkipPlay)
                    skip_play = true;
            }

            // 2. 摸牌阶段
            apply_draw(ctx, player, 2);

            // 3. 出牌阶段
            if (!skip_play)
            {
                int sha_played = 0;
                while (true)
                {
                    auto action = ai.choose_play(ctx, player);
                    if (action.is_none())
                        break;

                    const auto card =
                        find_in_hand(ctx, player, action.unwrap().instance_id);
                    if (card.is_none())
                        return TurnResult<void>::Err(TurnError::CardNotInHand);

                    const auto def_opt = ctx.catalog->find(card.unwrap().def_id);
                    if (def_opt.is_none())
                        return TurnResult<void>::Err(TurnError::UnknownCard);
                    const card::CardDef &def = *def_opt.unwrap();

                    if (def.type == card::CardType::Equipment)
                    {
                        auto er = equip_card(ctx, player, card.unwrap());
                        if (er.is_err())
                            return TurnResult<void>::Err(er.unwrap_err());
                        continue;
                    }

                    if (is_sha(def))
                    {
                        if (sha_played >= sha_limit(ctx, player))
                            return TurnResult<void>::Err(TurnError::ShaLimitExceeded);
                    }

                    const auto valid = valid_targets(ctx, player, def);
                    for (const auto &t : action.unwrap().targets)
                        if (std::find(valid.begin(), valid.end(), t) == valid.end())
                            return TurnResult<void>::Err(TurnError::InvalidTarget);

                    auto rr = resolve_play(
                        ctx, ai, player, card.unwrap(), action.unwrap().targets);
                    if (rr.is_err())
                        return TurnResult<void>::Err(TurnError::PlayRejected);
                    if (is_sha(def))
                        ++sha_played;
                }
            }

            // 4. 弃牌阶段：手牌上限 = 体力上限
            const int hand_limit = p.unwrap()->get_hp_bar().get_max();
            const int over = static_cast<int>(ctx.cards->hand_size(player)) - hand_limit;
            if (over > 0)
            {
                const auto discards = ai.choose_discards(ctx, player, over);
                if (static_cast<int>(discards.size()) != over)
                    return TurnResult<void>::Err(TurnError::DiscardInsufficient);
                for (const auto &id : discards)
                {
                    auto removed = ctx.cards->remove_from_hand(player, id);
                    if (removed.is_none())
                        return TurnResult<void>::Err(TurnError::DiscardInsufficient);
                    ctx.cards->discard(std::move(removed).unwrap());
                }
            }
            return TurnResult<void>::Ok();
        }
    }
}

#endif  // INCLUDE_TKW_GAME_TURN_HPP