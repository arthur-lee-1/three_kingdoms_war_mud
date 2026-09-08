/**
 * @file event_log.hpp
 * @brief 回放用事件日志：订阅全部实体/卡牌事件，按发布顺序记录成文本行。
 */

#ifndef INCLUDE_TKW_TESTS_GAME_EVENT_LOG_HPP
#define INCLUDE_TKW_TESTS_GAME_EVENT_LOG_HPP

#include <string>
#include <vector>

#include "entity/event.hpp"
#include "event/event_bus.hpp"
#include "event/handler.hpp"
#include "game/card_event.hpp"

namespace tkw
{
    namespace test
    {
        namespace
        {
            std::string zone_name(Zone z)
            {
                switch (z)
                {
                case Zone::Draw: return "draw";
                case Zone::Discard: return "discard";
                case Zone::Hand: return "hand";
                case Zone::Equip: return "equip";
                case Zone::Judge: return "judge";
                case Zone::Limbo: return "limbo";
                }
                return "?";
            }
        }

        /** @brief 订阅总线全部事件并记录为稳定文本行（顺序 = 发布顺序）。 */
        class EventLog
        {
        public:
            explicit EventLog(EventBus &bus) { subscribe_all(bus); }

            const std::vector<std::string> &lines() const noexcept { return lines_; }

        private:
            std::vector<EventBus::Handle> handles_;
            std::vector<std::string> lines_;

            template <typename E, typename F>
            void record(EventBus &bus, F fmt)
            {
                handles_.push_back(bus.subscribe(Handler<E>(
                    [this, fmt](HandlerContext<E> &c)
                    { lines_.push_back(fmt(c.event)); })));
            }

            void subscribe_all(EventBus &bus)
            {
                record<EntityHpChangedEvent>(
                    bus,
                    [](const EntityHpChangedEvent &e)
                    {
                        return "hp " + e.entity_id + " " + std::to_string(e.old_cur) +
                               "->" + std::to_string(e.new_cur);
                    });
                record<EntityDamagedEvent>(
                    bus,
                    [](const EntityDamagedEvent &e)
                    {
                        return "damaged " + e.target + " by " + e.source + " " +
                               std::to_string(e.amount);
                    });
                record<EntityHealedEvent>(
                    bus,
                    [](const EntityHealedEvent &e)
                    {
                        return "healed " + e.target + " " + std::to_string(e.amount);
                    });
                record<EntityDyingEvent>(
                    bus,
                    [](const EntityDyingEvent &e)
                    { return "dying " + e.target; });
                record<EntityDiedEvent>(
                    bus,
                    [](const EntityDiedEvent &e) { return "died " + e.entity_id; });
                record<CardDrawnEvent>(
                    bus,
                    [](const CardDrawnEvent &e)
                    { return "draw " + e.entity + " " + e.def_id; });
                record<CardPlayedEvent>(
                    bus,
                    [](const CardPlayedEvent &e)
                    { return "play " + e.user + " " + e.def_id; });
                record<CardDiscardedEvent>(
                    bus,
                    [](const CardDiscardedEvent &e)
                    { return "discard " + e.entity + " " + e.def_id; });
                record<CardMovedEvent>(
                    bus,
                    [](const CardMovedEvent &e)
                    {
                        return "move " + e.from_entity + ":" + zone_name(e.from) + "->" +
                               e.to_entity + ":" + zone_name(e.to) + " " + e.def_id;
                    });
            }
        };
    }
}

#endif  // INCLUDE_TKW_TESTS_GAME_EVENT_LOG_HPP
