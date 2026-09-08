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

using tkw::EntityDamagedEvent;
using tkw::EntityDiedEvent;
using tkw::EntityDyingEvent;
using tkw::EntityHealedEvent;
using tkw::EntityHpChangedEvent;
using tkw::EventBus;
using tkw::Handler;
using tkw::HandlerContext;

namespace entity = tkw::entity;

namespace
{
    struct DamageRecord
    {
        std::string source;
        std::string target;
        int amount = 0;
        bool indirect = false;
        std::uint64_t seq = 0;
    };

    struct ChangedRecord
    {
        int old_cur = 0;
        int new_cur = 0;
        std::uint64_t seq = 0;
    };

    entity::Entity make_entity(const char *id, int hp, EventBus &bus)
    {
        return entity::Entity(std::string(id), 0, entity::Hp::make(hp), bus);
    }
}

TEST_CASE("effect: take_damage publishes reason event before state event")
{
    EventBus bus;
    std::vector<DamageRecord> dmg;
    std::vector<ChangedRecord> seen;
    auto dmg_watch = bus.subscribe(Handler<EntityDamagedEvent>(
        [&](HandlerContext<EntityDamagedEvent> &ctx)
        {
            const auto &e = ctx.event;
            dmg.push_back({e.source, e.target, e.amount, e.indirect, e.get_sequence()});
        }));
    auto chg_watch = bus.subscribe(Handler<EntityHpChangedEvent>(
        [&](HandlerContext<EntityHpChangedEvent> &ctx)
        {
            seen.push_back(
                {ctx.event.old_cur, ctx.event.new_cur, ctx.event.get_sequence()});
        }));

    auto e = make_entity("fx_damage", 4, bus);
    CHECK(e.take_damage("yuanshao_1", 3, false) == 3);

    REQUIRE(dmg.size() == 1);
    CHECK(dmg.back().source == "yuanshao_1");
    CHECK(dmg.back().target == "fx_damage");
    CHECK(dmg.back().amount == 3);
    CHECK(!dmg.back().indirect);
    REQUIRE(seen.size() == 1);
    CHECK(seen.back().old_cur == 4);
    CHECK(seen.back().new_cur == 1);
    CHECK(dmg.back().seq < seen.back().seq);  // 原因层先于状态层
    CHECK(e.get_hp() == 1);
}

TEST_CASE("effect: take_damage with amount <= 0 is a silent no-op")
{
    EventBus bus;
    int dmg_calls = 0;
    std::vector<int> seen;
    auto dmg_watch = bus.subscribe(Handler<EntityDamagedEvent>(
        [&](HandlerContext<EntityDamagedEvent> &) { ++dmg_calls; }));
    auto chg_watch = bus.subscribe(Handler<EntityHpChangedEvent>(
        [&](HandlerContext<EntityHpChangedEvent> &ctx)
        { seen.push_back(ctx.event.new_cur); }));

    auto e = make_entity("fx_nodamage", 4, bus);
    CHECK(e.take_damage("x", 0, false) == 0);
    CHECK(e.take_damage("x", -5, false) == 0);

    CHECK(dmg_calls == 0);
    CHECK(seen.empty());
    CHECK(e.get_hp() == 4);
}

TEST_CASE("effect: take_damage drives hp negative, Entity emits no Dying/Died")
{
    EventBus bus;
    std::vector<ChangedRecord> seen;
    int dying_calls = 0;
    int died_calls = 0;
    auto chg_watch = bus.subscribe(Handler<EntityHpChangedEvent>(
        [&](HandlerContext<EntityHpChangedEvent> &ctx)
        {
            seen.push_back(
                {ctx.event.old_cur, ctx.event.new_cur, ctx.event.get_sequence()});
        }));
    auto dy_watch = bus.subscribe(Handler<EntityDyingEvent>(
        [&](HandlerContext<EntityDyingEvent> &) { ++dying_calls; }));
    auto dd_watch = bus.subscribe(Handler<EntityDiedEvent>(
        [&](HandlerContext<EntityDiedEvent> &) { ++died_calls; }));

    auto e = make_entity("fx_dying", 1, bus);
    CHECK(e.take_damage("yuanshao_1", 3, false) == 3);  // 1 -> -2（濒死值）

    REQUIRE(seen.size() == 1);
    CHECK(seen.back().old_cur == 1);
    CHECK(seen.back().new_cur == -2);
    CHECK(dying_calls == 0);  // 濒死事件归 combat 每轮发
    CHECK(died_calls == 0);   // 死亡事件归 combat 窗口关闭后发
    CHECK(e.get_hp() == -2);
}

TEST_CASE("effect: indirect damage is flagged on the reason event")
{
    EventBus bus;
    const EntityDamagedEvent *dmg = nullptr;
    auto h = bus.subscribe(Handler<EntityDamagedEvent>(
        [&](HandlerContext<EntityDamagedEvent> &ctx) { dmg = &ctx.event; }));

    auto e = make_entity("fx_indirect", 2, bus);
    e.take_damage("caocao", 1, true);  // 连环传导：间接伤害

    REQUIRE(dmg != nullptr);
    CHECK(dmg->indirect);
    CHECK(e.get_hp() == 1);
}

TEST_CASE("effect: peach from dying at state level (-2 -> -1 -> 0 -> 1)")
{
    EventBus bus;
    std::vector<int> healed;
    std::vector<int> hp_trace;
    auto heal_watch = bus.subscribe(Handler<EntityHealedEvent>(
        [&](HandlerContext<EntityHealedEvent> &ctx) { healed.push_back(ctx.event.amount); }));
    auto chg_watch = bus.subscribe(Handler<EntityHpChangedEvent>(
        [&](HandlerContext<EntityHpChangedEvent> &ctx)
        { hp_trace.push_back(ctx.event.new_cur); }));

    auto e = make_entity("fx_peach", 1, bus);
    e.take_damage("yuanshao_1", 3, false);  // 1 -> -2
    REQUIRE(e.get_hp() == -2);

    CHECK(e.heal(1) == 1);  // 桃 1：-2 -> -1（仍在濒死，combat 再进一轮）
    CHECK(e.get_hp() == -1);
    CHECK(e.heal(1) == 1);  // -1 -> 0（仍非正）
    CHECK(e.get_hp() == 0);
    CHECK(e.heal(1) == 1);  // 0 -> 1（救回）
    CHECK(e.get_hp() == 1);

    REQUIRE(healed.size() == 3);
    CHECK(healed == std::vector<int>{1, 1, 1});
    CHECK(hp_trace == std::vector<int>{-2, -1, 0, 1});
}

TEST_CASE("effect: heal clamps at max hp, silent when full or negative")
{
    EventBus bus;
    std::vector<int> healed;
    auto heal_watch = bus.subscribe(Handler<EntityHealedEvent>(
        [&](HandlerContext<EntityHealedEvent> &ctx) { healed.push_back(ctx.event.amount); }));

    auto e = make_entity("fx_heal", 4, bus);
    CHECK(e.heal(-1) == 0);   // 负值非法输入，静默
    CHECK(e.heal(5) == 0);   // 已满，静默
    CHECK(healed.empty());

    e.take_damage("x", 2, false);  // 4 -> 2
    CHECK(e.heal(10) == 2);        // 夹到 tot，实际恢复 2
    REQUIRE(healed.size() == 1);
    CHECK(healed.back() == 2);
    CHECK(e.get_hp() == 4);
}
