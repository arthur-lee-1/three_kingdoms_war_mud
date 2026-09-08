#include <doctest/doctest.h>

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "event/event.hpp"
#include "event/event_bus.hpp"
#include "event/handler.hpp"

using tkw::Event;
using tkw::EventBus;
using tkw::Handler;
using tkw::HandlerContext;

namespace
{
    struct SpellCastEvent : Event
    {
        std::string spell_id;
        std::string_view type_tag() const noexcept override { return "SpellCastEvent"; }
    };

    struct EnemyHitEvent : Event
    {
        int damage = 0;
        std::string_view type_tag() const noexcept override { return "EnemyHitEvent"; }
    };

    struct PlainEvent : Event
    {
    };
}

TEST_CASE("event: default sequence is zero")
{
    Event e;
    CHECK(e.get_sequence() == 0);
}

TEST_CASE("event: derived event overrides tag and works via base pointer")
{
    auto cast = std::make_shared<SpellCastEvent>();
    cast->spell_id = "fireball";

    EventBus bus;
    bus.publish(cast);  // 序号仅能由总线写入

    const Event *base = cast.get();
    CHECK(base->type_tag() == "SpellCastEvent");
    CHECK(base->get_sequence() == 1);
}

TEST_CASE("event: derived event without override inherits base tag")
{
    PlainEvent p;
    CHECK(p.type_tag() == "Event");
}

TEST_CASE("event_bus: publish invokes matching listener with same object")
{
    EventBus bus;
    bool called = false;
    const SpellCastEvent *received = nullptr;

    // 句柄为 RAII：必须保活到断言结束，临时句柄会在语句末尾即退订
    auto h = bus.subscribe(Handler<SpellCastEvent>(
        [&](HandlerContext<SpellCastEvent> &ctx)
        {
            called = true;
            received = &ctx.event;
        }));

    auto event = std::make_shared<SpellCastEvent>();
    event->spell_id = "fireball";
    bus.publish(event);

    CHECK(called);
    REQUIRE(received != nullptr);
    CHECK(received == event.get());
    CHECK(received->spell_id == "fireball");
}

TEST_CASE("event_bus: exact type dispatch, no cross-type delivery")
{
    EventBus bus;
    int cast_calls = 0;
    int hit_calls = 0;

    auto cast_h = bus.subscribe(Handler<SpellCastEvent>(
        [&](HandlerContext<SpellCastEvent> &) { ++cast_calls; }));
    auto hit_h = bus.subscribe(Handler<EnemyHitEvent>(
        [&](HandlerContext<EnemyHitEvent> &) { ++hit_calls; }));

    bus.publish(std::make_shared<SpellCastEvent>());
    CHECK(cast_calls == 1);
    CHECK(hit_calls == 0);

    bus.publish(std::make_shared<EnemyHitEvent>());
    CHECK(cast_calls == 1);
    CHECK(hit_calls == 1);
}

TEST_CASE("event_bus: subscribing to base does not receive derived events")
{
    EventBus bus;
    int base_calls = 0;
    auto h = bus.subscribe(Handler<Event>([&](HandlerContext<Event> &) { ++base_calls; }));

    bus.publish(std::make_shared<SpellCastEvent>());
    CHECK(base_calls == 0);  // 精确类型分发，基类订阅不命中细化事件
}

TEST_CASE("event_bus: multiple handlers all invoked")
{
    EventBus bus;
    int count = 0;
    auto h1 = bus.subscribe(Handler<SpellCastEvent>([&](HandlerContext<SpellCastEvent> &) { ++count; }));
    auto h2 = bus.subscribe(Handler<SpellCastEvent>([&](HandlerContext<SpellCastEvent> &) { ++count; }));

    bus.publish(std::make_shared<SpellCastEvent>());
    CHECK(count == 2);
}

TEST_CASE("event_bus: publish without subscribers still assigns sequence")
{
    EventBus bus;
    bus.publish(std::make_shared<EnemyHitEvent>());
    CHECK(bus.sequence() == 1);
}

TEST_CASE("event_bus: sequence increments globally across types")
{
    EventBus bus;
    std::vector<std::uint64_t> seen;
    auto cast_h = bus.subscribe(Handler<SpellCastEvent>(
        [&](HandlerContext<SpellCastEvent> &ctx) { seen.push_back(ctx.event.get_sequence()); }));
    auto hit_h = bus.subscribe(Handler<EnemyHitEvent>(
        [&](HandlerContext<EnemyHitEvent> &ctx) { seen.push_back(ctx.event.get_sequence()); }));

    bus.publish(std::make_shared<SpellCastEvent>());
    bus.publish(std::make_shared<EnemyHitEvent>());
    bus.publish(std::make_shared<SpellCastEvent>());

    REQUIRE(seen.size() == 3);
    CHECK(seen[0] == 1);
    CHECK(seen[1] == 2);
    CHECK(seen[2] == 3);
    CHECK(bus.sequence() == 3);
}

TEST_CASE("event_bus: independent buses have independent counters and listeners")
{
    EventBus a;
    EventBus b;
    int a_calls = 0;
    auto h = a.subscribe(Handler<SpellCastEvent>([&](HandlerContext<SpellCastEvent> &) { ++a_calls; }));

    a.publish(std::make_shared<SpellCastEvent>());
    b.publish(std::make_shared<SpellCastEvent>());

    CHECK(a_calls == 1);  // b 的发布不影响 a 的监听者
    CHECK(a.sequence() == 1);
    CHECK(b.sequence() == 1);
}
