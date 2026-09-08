/**
 * @file simple.hpp
 * @brief 确定性贪心策略：CLI 冒烟运行与回放测试用（无随机、无隐藏状态）。
 * @note 只做「合法且能推进」的动作：出杀（每回合至多一张）、可结算锦囊、
 *       装备。不出无懈（避免自抵消）、不主动发动需额外选择的武器效果。
 * @note 回合边界按 player 变化识别（next_player 保证相邻回合不同人），
 *       用于重置每回合杀次数——这是 DecisionSource 缺少回合态的临时手段，
 *       待决策视图（阶段 8）提供回合上下文后移除。
 */

#ifndef INCLUDE_TKW_GAME_AI_SIMPLE_HPP
#define INCLUDE_TKW_GAME_AI_SIMPLE_HPP

#include <string>
#include <utility>
#include <vector>

#include "card/card.hpp"
#include "card/def.hpp"
#include "game/context.hpp"
#include "game/decision.hpp"
#include "game/resolver.hpp"
#include "util/types.hpp"

namespace tkw
{
    namespace game
    {
        /** @brief 确定性贪心 AI（CLI 与回放测试共用）。 */
        class SimpleAI : public DecisionSource
        {
        public:
            bool play_response(
                GameContext &, const std::string &, card::ResponseKind) override
            {
                return true;
            }

            bool play_peach(
                GameContext &, const std::string &, const std::string &) override
            {
                return true;
            }

            bool play_counter(GameContext &, const std::string &) override
            {
                return false;
            }

            bool trigger_effect(
                GameContext &, const std::string &, card::Ability) override
            {
                return true;
            }

            card::Card pick_card_from_target(
                GameContext &ctx, const std::string &,
                const std::string &target) override
            {
                if (!ctx.cards->hand(target).empty())
                    return ctx.cards->hand(target).front();
                if (!ctx.cards->equip(target).empty())
                    return ctx.cards->equip(target).front();
                if (!ctx.cards->judge(target).empty())
                    return ctx.cards->judge(target).front();
                return card::Card{};
            }

            Option<PlayAction> choose_play(
                GameContext &ctx, const std::string &player) override
            {
                if (player != turn_owner_)
                {
                    turn_owner_ = player;
                    sha_played_ = false;
                }

                for (const auto &c : ctx.cards->hand(player))
                {
                    const auto def_opt = ctx.catalog->find(c.def_id);
                    if (def_opt.is_none())
                        continue;
                    const card::CardDef &def = *def_opt.unwrap();

                    if (def.type == card::CardType::Equipment)
                        return Option<PlayAction>::Some(PlayAction{c.instance_id, {}});

                    if (def.effect.is_none())
                        continue;
                    const auto kind = def.effect.unwrap().kind;

                    if (kind == card::CardEffectKind::Damage)
                    {
                        if (sha_played_)
                            continue;
                        const auto targets = valid_targets(ctx, player, def);
                        if (targets.empty())
                            continue;
                        sha_played_ = true;
                        return Option<PlayAction>::Some(
                            PlayAction{c.instance_id, {lowest_hp(ctx, targets)}});
                    }

                    if (kind == card::CardEffectKind::Heal)
                    {
                        const auto scope =
                            def.effect.unwrap().scope.unwrap_or(card::Scope::Self);
                        const auto me = ctx.entities->find(player);
                        if (scope == card::Scope::Self && me.is_some() &&
                            me.unwrap()->get_hp() >=
                                me.unwrap()->get_hp_bar().get_max())
                            continue;  // 满血不打桃
                    }

                    if (!is_active_kind(kind))
                        continue;

                    const auto targets = valid_targets(ctx, player, def);
                    if (targets.empty())
                        continue;

                    if (needs_target_card(kind))
                    {
                        std::vector<std::string> with_cards;
                        for (const auto &t : targets)
                            if (has_any_card(ctx, t))
                                with_cards.push_back(t);
                        if (with_cards.empty())
                            continue;
                        return Option<PlayAction>::Some(
                            PlayAction{c.instance_id, std::move(with_cards)});
                    }

                    return Option<PlayAction>::Some(
                        PlayAction{c.instance_id, targets});
                }
                return Option<PlayAction>::None();
            }

            std::vector<std::string> choose_discards(
                GameContext &ctx, const std::string &player, int count) override
            {
                std::vector<std::string> out;
                for (const auto &c : ctx.cards->hand(player))
                {
                    if (static_cast<int>(out.size()) >= count)
                        break;
                    out.push_back(c.instance_id);
                }
                return out;
            }

        private:
            static int hp_of(const GameContext &ctx, const std::string &id)
            {
                const auto e = ctx.entities->find(id);
                return e.is_some() ? e.unwrap()->get_hp() : 0;
            }

            /** @brief 集火：选体力最低的目标（同血取列表序，保证确定性）。 */
            static std::string lowest_hp(
                const GameContext &ctx, const std::vector<std::string> &targets)
            {
                std::string best = targets.front();
                int best_hp = hp_of(ctx, best);
                for (const auto &t : targets)
                {
                    const int h = hp_of(ctx, t);
                    if (h < best_hp)
                    {
                        best = t;
                        best_hp = h;
                    }
                }
                return best;
            }

            static bool is_active_kind(card::CardEffectKind k)
            {
                switch (k)
                {
                case card::CardEffectKind::AoeDamage:
                case card::CardEffectKind::Heal:
                case card::CardEffectKind::Draw:
                case card::CardEffectKind::DiscardTarget:
                case card::CardEffectKind::Steal:
                case card::CardEffectKind::Duel:
                    return true;
                default:
                    return false;
                }
            }

            static bool needs_target_card(card::CardEffectKind k)
            {
                return k == card::CardEffectKind::DiscardTarget ||
                       k == card::CardEffectKind::Steal;
            }

            static bool has_any_card(const GameContext &ctx, const std::string &id)
            {
                return ctx.cards->hand_size(id) > 0 || ctx.cards->equip_size(id) > 0 ||
                       ctx.cards->judge_size(id) > 0;
            }

            std::string turn_owner_;
            bool sha_played_ = false;
        };
    }
}

#endif  // INCLUDE_TKW_GAME_AI_SIMPLE_HPP
