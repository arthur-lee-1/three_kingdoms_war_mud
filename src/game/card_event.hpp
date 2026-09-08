/**
 * @file card_event.hpp
 * @brief 卡牌域事件：摸牌/打出/弃置/区域转移，供日志、回放、AI 观测消费。
 * @note 事件在 gameplay 的单一写点发布（容器 CardManager 保持哑状态）；
 *       发布辅助统一带 bus 空检查，便于直接构造 GameContext 的测试。
 */

#ifndef INCLUDE_TKW_GAME_CARD_EVENT_HPP
#define INCLUDE_TKW_GAME_CARD_EVENT_HPP

#include <cstdint>
#include <memory>
#include <string>

#include "card/card.hpp"
#include "event/event.hpp"
#include "event/event_bus.hpp"
#include "event/marco.hpp"
#include "game/context.hpp"

namespace tkw
{
    /** @brief 卡牌所在区域（事件用；Limbo = 不在任何区域/正在转移）。 */
    enum class Zone : std::uint8_t
    {
        Draw,
        Discard,
        Hand,
        Equip,
        Judge,
        Limbo,
    };

    DEFINE_EVENT_START(Card, Event)
    DEFINE_EVENT_END(Card)

    /** @brief 摸牌：一张牌从摸牌堆进入某实体手牌。 */
    DEFINE_EVENT_START(CardDrawn, CardEvent)
public:
    std::string entity;
    std::string instance_id;
    std::string def_id;
    DEFINE_EVENT_END(CardDrawn)

    /** @brief 打出：某实体主动打出一张牌（基本/锦囊/装备）。 */
    DEFINE_EVENT_START(CardPlayed, CardEvent)
public:
    std::string user;
    std::string instance_id;
    std::string def_id;
    DEFINE_EVENT_END(CardPlayed)

    /** @brief 弃置：一张牌进入弃牌堆（entity 可空 = 判定/无主）。 */
    DEFINE_EVENT_START(CardDiscarded, CardEvent)
public:
    std::string entity;
    std::string instance_id;
    std::string def_id;
    DEFINE_EVENT_END(CardDiscarded)

    /** @brief 区域转移：一张牌从一个区域移到另一个区域（装备/顺牵/延时移送）。 */
    DEFINE_EVENT_START(CardMoved, CardEvent)
public:
    std::string from_entity;
    std::string to_entity;
    std::string instance_id;
    std::string def_id;
    Zone from = Zone::Limbo;
    Zone to = Zone::Limbo;
    DEFINE_EVENT_END(CardMoved)

    namespace game
    {
        inline void emit_card_drawn(
            GameContext &ctx, const std::string &entity, const card::Card &c)
        {
            if (!ctx.bus)
                return;
            auto ev = std::make_shared<CardDrawnEvent>();
            ev->entity = entity;
            ev->instance_id = c.instance_id;
            ev->def_id = c.def_id;
            ctx.bus->publish(ev);
        }

        inline void emit_card_played(
            GameContext &ctx, const std::string &user, const card::Card &c)
        {
            if (!ctx.bus)
                return;
            auto ev = std::make_shared<CardPlayedEvent>();
            ev->user = user;
            ev->instance_id = c.instance_id;
            ev->def_id = c.def_id;
            ctx.bus->publish(ev);
        }

        inline void emit_card_discarded(
            GameContext &ctx, const std::string &entity, const card::Card &c)
        {
            if (!ctx.bus)
                return;
            auto ev = std::make_shared<CardDiscardedEvent>();
            ev->entity = entity;
            ev->instance_id = c.instance_id;
            ev->def_id = c.def_id;
            ctx.bus->publish(ev);
        }

        inline void emit_card_moved(
            GameContext &ctx, const std::string &from_entity,
            const std::string &to_entity, const card::Card &c, Zone from, Zone to)
        {
            if (!ctx.bus)
                return;
            auto ev = std::make_shared<CardMovedEvent>();
            ev->from_entity = from_entity;
            ev->to_entity = to_entity;
            ev->instance_id = c.instance_id;
            ev->def_id = c.def_id;
            ev->from = from;
            ev->to = to;
            ctx.bus->publish(ev);
        }
    }
}

#endif  // INCLUDE_TKW_GAME_CARD_EVENT_HPP
