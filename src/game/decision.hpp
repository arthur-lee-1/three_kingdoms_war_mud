/**
 * @file decision.hpp
 * @brief 玩家决策源（策略接口）：结算器/回合流程需要「玩家做什么选择」时调用。
 * @note 实现即玩家策略：测试注入确定性假策略，将来 CLI/网络层注入真人输入。
 *       响应牌/出牌的**实际消费**由结算器负责（单一写者），本接口只做决定。
 */

#ifndef INCLUDE_TKW_GAME_DECISION_HPP
#define INCLUDE_TKW_GAME_DECISION_HPP

#include <string>
#include <vector>

#include "card/def.hpp"
#include "game/context.hpp"

namespace tkw
{
    namespace game
    {
        /** @brief 出牌阶段的动作：打出某张手牌并指定目标。 */
        struct PlayAction
        {
            std::string instance_id;          /**< 要打出的手牌 */
            std::vector<std::string> targets; /**< 目标实体 id（装备牌为空） */
        };

        /**
         * @class DecisionSource
         * @brief 结算/回合期间的玩家决策接口。
         */
        class DecisionSource
        {
        public:
            virtual ~DecisionSource() = default;

            /**
             * @brief 响应窗口：entity_id 是否打出指定响应牌（杀/闪）。
             * @note 实现应只在「打算且能够打出」时返回 true（结算器会先检查
             *       手牌里是否有对应的响应牌再询问）。
             */
            virtual bool play_response(
                GameContext &ctx,
                const std::string &entity_id,
                card::ResponseKind kind) = 0;

            /**
             * @brief 从目标区域选一张牌（过河拆桥弃置 / 顺手牵羊获得）。
             * @return 选中的牌（须存在于 target 的某个区域；结算器按其
             *         instance_id 移除并决定弃置或收入手牌）。
             */
            virtual card::Card pick_card_from_target(
                GameContext &ctx,
                const std::string &source,
                const std::string &target) = 0;

            /**
             * @brief 出牌阶段：选择打出一张手牌及其目标；None = 结束出牌。
             * @note 回合流程负责校验合法性（手牌存在/目标合法/杀次数限制），
             *       不合法的动作会被拒绝并报错。
             */
            virtual Option<PlayAction> choose_play(
                GameContext &ctx, const std::string &player) = 0;

            /**
             * @brief 弃牌阶段：弃置 count 张手牌。
             * @note 回合流程按 count 逐张校验并弃置；数量不符/引用不存在会报错。
             */
            virtual std::vector<std::string> choose_discards(
                GameContext &ctx, const std::string &player, int count) = 0;

            /**
             * @brief 濒死救场：saver 是否对濒死的 dying 打出一张桃。
             * @note 实现应只在「打算且能够打出」时返回 true（combat 会先检查
             *       saver 手牌有桃再询问，并负责消费）。
             */
            virtual bool play_peach(
                GameContext &ctx, const std::string &saver, const std::string &dying) = 0;
        };
    }
}

#endif  // INCLUDE_TKW_GAME_DECISION_HPP