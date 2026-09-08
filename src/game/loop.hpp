/**
 * @file loop.hpp
 * @brief 对局主循环：开局准备（建牌堆/洗牌/发初始手牌）→ 回合轮转 →
 *        结束判定（只剩一名存活玩家）。
 * @note 玩家实体由调用方先行创建（含座位与体力）；本模块只负责发牌与轮转。
 *       死亡者被 EntityManager 移除后自动跳过（next_player 按存活实体环绕）。
 */

#ifndef INCLUDE_TKW_GAME_LOOP_HPP
#define INCLUDE_TKW_GAME_LOOP_HPP

#include <cstdint>
#include <random>
#include <string>
#include <utility>

#include "card/catalog.hpp"
#include "card/manager.hpp"
#include "entity/manager.hpp"
#include "game/context.hpp"
#include "game/decision.hpp"
#include "game/turn.hpp"
#include "util/types.hpp"

namespace tkw
{
    namespace game
    {
        /** @brief 对局流程错误。 */
        enum class LoopError : std::uint8_t
        {
            NoPlayers,  /**< 场上没有玩家 */
            TurnFailed, /**< 某回合流程报错（如非法出牌脚本） */
            MaxRounds,  /**< 超出最大回合数（无法分出胜负的僵局） */
        };

        template <typename T>
        using LoopResult = Result<T, LoopError>;

        /** @brief 对局结果。 */
        struct GameOutcome
        {
            std::string winner; /**< 最后存活玩家 id */
            int rounds = 0;     /**< 实际进行的回合数 */
        };

        /** @brief 存活玩家数。 */
        inline std::size_t alive_count(const GameContext &ctx)
        {
            return ctx.entities->size();
        }

        /** @brief 每名存活玩家发 count 张初始手牌（从堆顶摸）。 */
        inline void deal_initial_hands(GameContext &ctx, int count = 4)
        {
            for (const auto &ent : *ctx.entities)
            {
                for (int i = 0; i < count; ++i)
                {
                    auto c = ctx.cards->draw();
                    if (c.is_none())
                        break;
                    ctx.cards->add_to_hand(ent->get_id(), std::move(c).unwrap());
                }
            }
        }

        /** @brief 开局准备：构建牌堆 → 洗牌 → 发初始手牌。 */
        inline void prepare_game(GameContext &ctx, std::mt19937 &rng, int hand = 4)
        {
            ctx.cards->build_deck(*ctx.catalog);
            ctx.cards->shuffle_draw(rng);
            deal_initial_hands(ctx, hand);
        }

        /**
         * @brief 主循环：从 first_player 起轮转执行回合，直到只剩一名存活玩家。
         * @return Ok(GameOutcome) 或 Err(LoopError)。
         */
        inline LoopResult<GameOutcome> play_game(
            GameContext &ctx, DecisionSource &ai, std::mt19937 &rng,
            const std::string &first_player)
        {
            if (ctx.entities->empty())
                return LoopResult<GameOutcome>::Err(LoopError::NoPlayers);

            prepare_game(ctx, rng, 4);

            std::string current = first_player;
            int rounds = 0;
            while (ctx.entities->size() > 1)
            {
                auto r = execute_turn(ctx, ai, rng, current);
                if (r.is_err())
                    return LoopResult<GameOutcome>::Err(LoopError::TurnFailed);
                current = next_player(ctx, current);
                if (++rounds > 1000)
                    return LoopResult<GameOutcome>::Err(LoopError::MaxRounds);
            }

            GameOutcome gr;
            gr.rounds = rounds;
            for (const auto &ent : *ctx.entities)
                gr.winner = ent->get_id();
            return LoopResult<GameOutcome>::Ok(std::move(gr));
        }
    }
}

#endif  // INCLUDE_TKW_GAME_LOOP_HPP