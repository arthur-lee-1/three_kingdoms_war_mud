/**
 * @file decision.hpp
 * @brief 玩家决策源（策略接口）：结算器需要「玩家做什么选择」时调用。
 * @note 实现即玩家策略：测试注入确定性假策略，将来 CLI/网络层注入真人输入。
 *       响应牌的**消费**由结算器负责（单一写者），本接口只做决定。
 */

#ifndef INCLUDE_TKW_GAME_DECISION_HPP
#define INCLUDE_TKW_GAME_DECISION_HPP

#include <string>

#include "card/def.hpp"
#include "game/context.hpp"

namespace tkw
{
    namespace game
    {
        /**
         * @class DecisionSource
         * @brief 结算期间的玩家决策接口。
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
                GameContext &ctx, const std::string &entity_id, card::ResponseKind kind) = 0;

            /**
             * @brief 从目标区域选一张牌（过河拆桥弃置 / 顺手牵羊获得）。
             * @return 选中的牌（须存在于 target 的某个区域；结算器按其
             *         instance_id 移除并决定弃置或收入手牌）。
             */
            virtual card::Card pick_card_from_target(
                GameContext &ctx, const std::string &source, const std::string &target) = 0;
        };
    }
}

#endif  // INCLUDE_TKW_GAME_DECISION_HPP