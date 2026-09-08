#include <doctest/doctest.h>

#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "event/event.hpp"
#include "event/event_bus.hpp"
#include "event/handler.hpp"

using tkw::Event;
using tkw::EventBus;
using tkw::Handler;
using tkw::HandlerContext;
using tkw::HandlerPriority;
using tkw::fanout;
using tkw::pipe;
using tkw::when;

namespace
{
    struct PingEvent : Event
    {
        int value = 0;
        std::string_view type_tag() const noexcept override { return "PingEvent"; }
    };

    struct PongEvent : Event
    {
        int value = 0;
        std::string_view type_tag() const noexcept override { return "PongEvent"; }
    };
}

TEST_CASE("handler: filter gates matches, action runs via invoke, priority is exposed")
{
    Handler<PingEvent> h(
        [](const PingEvent &e) { return e.value > 10; },
        [](HandlerContext<PingEvent> &ctx) { ctx.event.value += 1; },
        HandlerPriority::First);

    PingEvent e;
    e.value = 5;
    CHECK_FALSE(h.matches(e));
    e.value = 20;
    CHECK(h.matches(e));

    HandlerContext<PingEvent> ctx{e};
    h.invoke(ctx);
    CHECK(e.value == 21);
    CHECK_FALSE(ctx.stop);
    CHECK(h.priority() == HandlerPriority::First);
    CHECK(h.has_filter());

    Handler<PingEvent> plain([](HandlerContext<PingEvent> &) {});
    CHECK(!plain.has_filter());
    CHECK(plain.matches(e));
    CHECK(plain.priority() == HandlerPriority::Normal);
}

TEST_CASE("handler_bus: handlers run in priority order, ties by registration")
{
    EventBus bus;
    std::vector<std::string> order;
    auto h1 = bus.subscribe(Handler<PingEvent>(
        [&](HandlerContext<PingEvent> &) { order.push_back("normal_1"); }));
    auto h2 = bus.subscribe(Handler<PingEvent>(
        [&](HandlerContext<PingEvent> &) { order.push_back("last"); }, HandlerPriority::Last));
    auto h3 = bus.subscribe(Handler<PingEvent>(
        [&](HandlerContext<PingEvent> &) { order.push_back("first"); }, HandlerPriority::First));
    auto h4 = bus.subscribe(Handler<PingEvent>(
        [&](HandlerContext<PingEvent> &) { order.push_back("normal_2"); }));

    bus.publish(std::make_shared<PingEvent>());

    CHECK(order == std::vector<std::string>{ "first", "normal_1", "normal_2", "last" });
}

TEST_CASE("handler_bus: handle unsubscribes on reset and on destruction")
{
    EventBus bus;
    int calls = 0;
    auto h = bus.subscribe(Handler<PingEvent>([&](HandlerContext<PingEvent> &) { ++calls; }));

    bus.publish(std::make_shared<PingEvent>());
    CHECK(calls == 1);

    h.reset();
    CHECK_FALSE(h.active());
    bus.publish(std::make_shared<PingEvent>());
    CHECK(calls == 1);
}

TEST_CASE("handler_bus: handle destructor unsubscribes")
{
    EventBus bus;
    int calls = 0;
    {
        auto h = bus.subscribe(Handler<PingEvent>([&](HandlerContext<PingEvent> &) { ++calls; }));
        bus.publish(std::make_shared<PingEvent>());
    }  // h 析构 → 退订
    bus.publish(std::make_shared<PingEvent>());
    CHECK(calls == 1);
}

TEST_CASE("handler_bus: stop halts later handlers within one publish")
{
    EventBus bus;
    int second = 0;
    auto h1 = bus.subscribe(Handler<PingEvent>(
        [&](HandlerContext<PingEvent> &ctx) { ctx.stop = true; }, HandlerPriority::First));
    auto h2 = bus.subscribe(Handler<PingEvent>([&](HandlerContext<PingEvent> &) { ++second; }));

    bus.publish(std::make_shared<PingEvent>());
    CHECK(second == 0);
}

TEST_CASE("pipe: stages run in order, filters skip, stop short-circuits")
{
    std::vector<std::string> ran;
    auto h = pipe<PingEvent>(
        Handler<PingEvent>(
            [](const PingEvent &e) { return e.value > 3; },  // 1. calc：低值直接跳过
            [&](HandlerContext<PingEvent> &ctx)
            {
                ran.push_back("calc");
                ctx.event.value = 0;
            }),
        Handler<PingEvent>(
            [](const PingEvent &e) { return e.value == 0; },  // 2. zero：短路
            [&](HandlerContext<PingEvent> &ctx)
            {
                ran.push_back("zero");
                ctx.stop = true;
            }),
        Handler<PingEvent>([&](HandlerContext<PingEvent> &) { ran.push_back("settle"); }));

    PingEvent high;
    high.value = 7;
    HandlerContext<PingEvent> ctx_high{high};
    h.invoke(ctx_high);
    CHECK(ran == std::vector<std::string>{ "calc", "zero" });  // 短路，settle 未执行
    CHECK(ctx_high.stop);

    ran.clear();
    PingEvent low;
    low.value = 2;
    HandlerContext<PingEvent> ctx_low{low};
    h.invoke(ctx_low);
    CHECK(ran == std::vector<std::string>{ "settle" });  // calc 被 filter 跳过
}

TEST_CASE("fanout: every handler runs regardless of stop")
{
    int first = 0;
    int second = 0;
    auto h = fanout<PingEvent>(
        Handler<PingEvent>(
            [&](HandlerContext<PingEvent> &ctx)
            {
                ++first;
                ctx.stop = true;
            }),
        Handler<PingEvent>([&](HandlerContext<PingEvent> &) { ++second; }));

    PingEvent e;
    HandlerContext<PingEvent> ctx{e};
    h.invoke(ctx);

    CHECK(first == 1);
    CHECK(second == 1);  // fanout 不看 stop，各自独立
    CHECK(ctx.stop);
}

TEST_CASE("when: outer predicate composes with the handler's own filter")
{
    int calls = 0;
    auto inner = Handler<PingEvent>(
        [](const PingEvent &e) { return e.value % 2 == 0; },
        [&](HandlerContext<PingEvent> &) { ++calls; });

    auto h = when([](const PingEvent &e) { return e.value > 10; }, std::move(inner));

    PingEvent e;
    e.value = 12;
    CHECK(h.matches(e));     // >10 且偶数
    e.value = 14;
    CHECK(h.matches(e));
    e.value = 11;
    CHECK_FALSE(h.matches(e));  // >10 但奇数（内层 filter 拦截）
    e.value = 2;
    CHECK_FALSE(h.matches(e));  // 偶数但 <=10（外层 predicate 拦截）

    e.value = 12;
    HandlerContext<PingEvent> ctx{e};
    h.invoke(ctx);
    CHECK(calls == 1);
    CHECK(h.priority() == HandlerPriority::Normal);
}

TEST_CASE("handler_bus: handler may republish (re-entrant dispatch, thorns pattern)")
{
    EventBus bus;
    std::vector<std::string> order;
    auto h1 = bus.subscribe(Handler<PingEvent>(
        [&](HandlerContext<PingEvent> &ctx)
        {
            order.push_back("ping");
            auto pong = std::make_shared<PongEvent>();
            pong->value = ctx.event.value;
            bus.publish(pong);
        }));
    auto h2 = bus.subscribe(Handler<PongEvent>(
        [&](HandlerContext<PongEvent> &ctx)
        {
            order.push_back("pong");
            CHECK(ctx.event.value == 42);
        }));

    auto ping = std::make_shared<PingEvent>();
    ping->value = 42;
    bus.publish(ping);

    CHECK(order == std::vector<std::string>{ "ping", "pong" });
}

TEST_CASE("handler_bus: mid-dispatch unsubscribe only takes effect next round (snapshot)")
{
    EventBus bus;
    int second = 0;
    EventBus::Handle h2;  // 前置声明：h1 的 lambda 才能按引用捕获
    auto h1 = bus.subscribe(Handler<PingEvent>(
        [&](HandlerContext<PingEvent> &)
        {
            if (h2.active())
                h2.reset();  // 分发过程中退订 h2
        },
        HandlerPriority::First));
    h2 = bus.subscribe(Handler<PingEvent>([&](HandlerContext<PingEvent> &) { ++second; }));

    bus.publish(std::make_shared<PingEvent>());
    CHECK(second == 1);  // 快照语义：本轮 h2 已排入，仍执行

    bus.publish(std::make_shared<PingEvent>());
    CHECK(second == 1);  // 下一轮不再触发
}
