#include <doctest/doctest.h>

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "entity/base.hpp"
#include "entity/event.hpp"
#include "entity/hp.hpp"
#include "event/event_bus.hpp"
#include "event/handler.hpp"

using tkw::EntityDiedEvent;
using tkw::EntityDyingEvent;
using tkw::EntityHpChangedEvent;
using tkw::EventBus;
using tkw::Handler;
using tkw::HandlerContext;

namespace entity = tkw::entity;

namespace
{
    struct ChangedRecord
    {
        int old_cur = 0;
        int new_cur = 0;
        int max = 0;
    };

    entity::Entity make_entity(const char *id, int hp, EventBus &bus)
    {
        return entity::Entity(std::string(id), 0, entity::Hp::make(hp), bus);
    }

    auto watch_hp_changes(EventBus &bus, std::vector<ChangedRecord> &seen)
    {
        return bus.subscribe(Handler<EntityHpChangedEvent>(
            [&](HandlerContext<EntityHpChangedEvent> &ctx)
            {
                seen.push_back({ctx.event.old_cur, ctx.event.new_cur, ctx.event.max});
            }));
    }
}

TEST_CASE("hp: set_cur fires EntityHpChangedEvent with old/new/max")
{
    EventBus bus;
    std::vector<ChangedRecord> seen;
    auto watch = watch_hp_changes(bus, seen);

    auto e = make_entity("hp_set_cur", 20, bus);
    CHECK(e.get_hp_bar().set_cur(7));

    REQUIRE(!seen.empty());
    const ChangedRecord &r = seen.back();
    CHECK(r.old_cur == 20);
    CHECK(r.new_cur == 7);
    CHECK(r.max == 20);
}

TEST_CASE("hp: no-op set_cur fires nothing")
{
    EventBus bus;
    std::vector<ChangedRecord> seen;
    auto watch = watch_hp_changes(bus, seen);

    auto e = make_entity("hp_noop", 20, bus);
    CHECK(e.get_hp_bar().set_cur(20));

    CHECK(seen.empty());
}

TEST_CASE("hp: set_cur above max rejected; below zero is legal (dying value)")
{
    EventBus bus;
    std::vector<ChangedRecord> seen;
    auto watch = watch_hp_changes(bus, seen);

    auto e = make_entity("hp_invalid", 20, bus);
    CHECK(!e.get_hp_bar().set_cur(21));
    CHECK(seen.empty());

    CHECK(e.get_hp_bar().set_cur(-1));  // 体力无下限，负值合法（濒死值）
    REQUIRE(seen.size() == 1);
    CHECK(seen.back().old_cur == 20);
    CHECK(seen.back().new_cur == -1);
    CHECK(seen.back().max == 20);
}

TEST_CASE("hp: sub may drive hp negative (dying value), add clamps at max")
{
    EventBus bus;
    std::vector<ChangedRecord> seen;
    auto watch = watch_hp_changes(bus, seen);

    auto e = make_entity("hp_addsub", 1, bus);
    CHECK(e.get_hp_bar().sub(3) == 3);  // 1 -> -2（濒死值）
    REQUIRE(seen.size() == 1);
    CHECK(seen.back().old_cur == 1);
    CHECK(seen.back().new_cur == -2);
    CHECK(seen.back().max == 1);

    CHECK(e.get_hp_bar().sub(99) == 99);  // -2 -> -101
    REQUIRE(seen.size() == 2);
    CHECK(seen.back().new_cur == -101);

    CHECK(e.get_hp_bar().add(200) == 102);  // -101 -> 1（夹到 max=1）
    REQUIRE(seen.size() == 3);
    CHECK(seen.back().new_cur == 1);

    CHECK(e.get_hp_bar().add(99) == 0);  // 已满
    CHECK(seen.size() == 3);
}

TEST_CASE("hp: going non-positive fires only HpChanged, no Dying/Died")
{
    EventBus bus;
    std::vector<ChangedRecord> seen;
    std::vector<std::uint64_t> dying_seq;
    std::vector<std::uint64_t> died_seq;
    auto chg_watch = bus.subscribe(Handler<EntityHpChangedEvent>(
        [&](HandlerContext<EntityHpChangedEvent> &ctx)
        {
            seen.push_back({ctx.event.old_cur, ctx.event.new_cur, ctx.event.max});
        }));
    auto dy_watch = bus.subscribe(Handler<EntityDyingEvent>(
        [&](HandlerContext<EntityDyingEvent> &ctx) { dying_seq.push_back(ctx.event.get_sequence()); }));
    auto dd_watch = bus.subscribe(Handler<EntityDiedEvent>(
        [&](HandlerContext<EntityDiedEvent> &ctx) { died_seq.push_back(ctx.event.get_sequence()); }));

    auto e = make_entity("hp_dying_value", 1, bus);
    e.get_hp_bar().sub(3);  // 1 -> -2

    REQUIRE(seen.size() == 1);
    CHECK(seen.back().new_cur == -2);
    CHECK(dying_seq.empty());  // Entity 不发濒死
    CHECK(died_seq.empty());  // Entity 不发死亡
}

TEST_CASE("hp: set_max clamps cur, fires only the clamp change")
{
    EventBus bus;
    std::vector<ChangedRecord> seen;
    auto watch = watch_hp_changes(bus, seen);

    auto e = make_entity("hp_set_max", 20, bus);
    CHECK(e.get_hp_bar().set_max(10));  // cur 20 被夹到 10

    REQUIRE(seen.size() == 1);  // 仅夹紧触发一次 cur 变化
    CHECK(seen.back().old_cur == 20);
    CHECK(seen.back().new_cur == 10);
    CHECK(seen.back().max == 10);

    CHECK(e.get_hp_bar().set_max(30));  // 只涨上限，cur 不变
    CHECK(seen.size() == 1);

    CHECK(!e.get_hp_bar().set_max(-1));
    CHECK(seen.size() == 1);
}

TEST_CASE("hp: set_max below cur then back up never resurrects cur")
{
    entity::Hp hp = entity::Hp::make(4);
    hp.set_cur(3);

    CHECK(hp.set_max(2));  // 3 -> 2（夹紧）
    CHECK(hp.get_cur() == 2);
    CHECK(hp.set_max(4));  // 上限回升不自动回血
    CHECK(hp.get_cur() == 2);
}

TEST_CASE("hp: make sets cur and max to the given value")
{
    entity::Hp hp = entity::Hp::make(4);

    CHECK(hp.get_cur() == 4);
    CHECK(hp.get_max() == 4);
}
