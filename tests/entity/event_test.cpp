#include <doctest/doctest.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "entity/event.hpp"
#include "event/event.hpp"
#include "event/event_bus.hpp"
#include "event/handler.hpp"

using tkw::Event;
using tkw::EventBus;
using tkw::Handler;
using tkw::HandlerContext;
using tkw::EntityDamagedEvent;
using tkw::EntityDiedEvent;
using tkw::EntityDyingEvent;
using tkw::EntityHpChangedEvent;

TEST_CASE("entity_event: hp changed carries old/new/max and tag")
{
    EventBus bus;
    const EntityHpChangedEvent *received = nullptr;
    auto h = bus.subscribe(Handler<EntityHpChangedEvent>(
        [&](HandlerContext<EntityHpChangedEvent> &ctx) { received = &ctx.event; }));

    auto ev = std::make_shared<EntityHpChangedEvent>();
    ev->entity_id = "player";
    ev->old_cur = 10;
    ev->new_cur = -2;
    ev->max = 4;
    bus.publish(ev);

    REQUIRE(received != nullptr);
    CHECK(received->entity_id == "player");
    CHECK(received->old_cur == 10);
    CHECK(received->new_cur == -2);
    CHECK(received->max == 4);
    CHECK(received->type_tag() == "EntityHpChangedEvent");
}

TEST_CASE("entity_event: type tag resolvable through base Event pointer")
{
    auto dying = std::make_shared<EntityDyingEvent>();
    dying->target = "caocao";
    dying->current_hp = -1;

    const Event *base = dying.get();
    CHECK(base->type_tag() == "EntityDyingEvent");
}

TEST_CASE("entity_event: damaged carries source/target/amount/indirect, no state leak")
{
    EventBus bus;
    int status_calls = 0;
    const EntityDamagedEvent *dmg = nullptr;
    auto h1 = bus.subscribe(Handler<EntityHpChangedEvent>(
        [&](HandlerContext<EntityHpChangedEvent> &) { ++status_calls; }));
    auto h2 = bus.subscribe(Handler<EntityDamagedEvent>(
        [&](HandlerContext<EntityDamagedEvent> &ctx) { dmg = &ctx.event; }));

    auto ev = std::make_shared<EntityDamagedEvent>();
    ev->source = "yuanshao_1";
    ev->target = "caocao";
    ev->amount = 3;
    ev->indirect = true;
    bus.publish(ev);

    REQUIRE(dmg != nullptr);
    CHECK(dmg->source == "yuanshao_1");
    CHECK(dmg->target == "caocao");
    CHECK(dmg->amount == 3);
    CHECK(dmg->indirect);
    CHECK(status_calls == 0);
}

TEST_CASE("entity_event: dying carries target and current hp")
{
    EventBus bus;
    const EntityDyingEvent *received = nullptr;
    auto h = bus.subscribe(Handler<EntityDyingEvent>(
        [&](HandlerContext<EntityDyingEvent> &ctx) { received = &ctx.event; }));

    auto ev = std::make_shared<EntityDyingEvent>();
    ev->target = "caocao";
    ev->current_hp = -2;
    bus.publish(ev);

    REQUIRE(received != nullptr);
    CHECK(received->target == "caocao");
    CHECK(received->current_hp == -2);
    CHECK(received->type_tag() == "EntityDyingEvent");
}

TEST_CASE("entity_event: publishes are ordered by sequence")
{
    EventBus bus;
    std::vector<std::uint64_t> seqs;
    auto h = bus.subscribe(Handler<EntityDiedEvent>(
        [&](HandlerContext<EntityDiedEvent> &ctx) { seqs.push_back(ctx.event.get_sequence()); }));

    auto a = std::make_shared<EntityDiedEvent>();
    a->entity_id = "caocao";
    bus.publish(a);
    auto b = std::make_shared<EntityDiedEvent>();
    b->entity_id = "yuanshao_1";
    bus.publish(b);

    REQUIRE(seqs.size() == 2);
    CHECK(seqs[0] == 1);
    CHECK(seqs[1] == 2);
}
