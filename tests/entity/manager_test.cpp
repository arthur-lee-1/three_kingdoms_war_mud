#include <doctest/doctest.h>

#include <string>
#include <vector>

#include "entity/error.hpp"
#include "entity/hp.hpp"
#include "entity/manager.hpp"
#include "event/event_bus.hpp"

using tkw::EntityManager;
using tkw::EventBus;
using tkw::entity::EntityError;
using tkw::entity::Hp;

namespace
{
    Hp hp4()
    {
        return Hp::make(4);
    }
}

TEST_CASE("manager: create binds entity; duplicate id is DuplicateId")
{
    EventBus bus;
    EntityManager mgr(bus);

    auto r = mgr.create("caocao", 0, hp4());
    REQUIRE(r.is_ok());
    CHECK(r.unwrap()->get_id() == "caocao");
    CHECK(r.unwrap()->get_seat() == 0);
    CHECK(r.unwrap()->get_hp() == 4);

    auto dup = mgr.create("caocao", 1, hp4());
    REQUIRE(dup.is_err());
    CHECK(dup.unwrap_err() == EntityError::DuplicateId);
    CHECK(mgr.size() == 1);
}

TEST_CASE("manager: find/contains/size/empty")
{
    EventBus bus;
    EntityManager mgr(bus);
    CHECK(mgr.empty());
    CHECK(mgr.find("caocao").is_none());

    auto r = mgr.create("caocao", 0, hp4());
    REQUIRE(r.is_ok());
    CHECK(mgr.contains("caocao"));
    CHECK(mgr.find("caocao").contains(r.unwrap()));
    CHECK(mgr.size() == 1);
    CHECK_FALSE(mgr.empty());
}

TEST_CASE("manager: remove is idempotent and keeps other pointers stable")
{
    EventBus bus;
    EntityManager mgr(bus);
    auto ra = mgr.create("a", 0, hp4());
    auto rb = mgr.create("b", 1, hp4());
    REQUIRE(ra.is_ok());
    REQUIRE(rb.is_ok());
    auto *pa = ra.unwrap();
    auto *pb = rb.unwrap();

    mgr.remove("a");
    CHECK(!mgr.contains("a"));
    CHECK(mgr.find("b").contains(pb));
    CHECK(mgr.size() == 1);

    mgr.remove("a");  // 幂等
    CHECK(mgr.size() == 1);
    (void)pa;
}

TEST_CASE("manager: iteration follows creation order (not seat)")
{
    EventBus bus;
    EntityManager mgr(bus);
    for (int i = 0; i < 4; ++i)
    {
        auto r = mgr.create("p" + std::to_string(i), i, hp4());
        REQUIRE(r.is_ok());
    }

    std::vector<int> seats;
    for (auto &up : mgr)
        seats.push_back(up->get_seat());
    CHECK(seats == std::vector<int>{0, 1, 2, 3});
}

TEST_CASE("manager: ordered_ids/next/order_from follow seat, not creation order")
{
    EventBus bus;
    EntityManager mgr(bus);
    // 创建序：c(2), a(0), d(3), b(1) —— 与座位号不一致
    REQUIRE(mgr.create("c", 2, hp4()).is_ok());
    REQUIRE(mgr.create("a", 0, hp4()).is_ok());
    REQUIRE(mgr.create("d", 3, hp4()).is_ok());
    REQUIRE(mgr.create("b", 1, hp4()).is_ok());

    CHECK(mgr.ordered_ids() == std::vector<std::string>({"a", "b", "c", "d"}));
    CHECK(mgr.next("a") == "b");
    CHECK(mgr.next("d") == "a");  // 环绕
    CHECK(mgr.order_from("c") == std::vector<std::string>({"c", "d", "a", "b"}));

    mgr.remove("b");
    CHECK(mgr.ordered_ids() == std::vector<std::string>({"a", "c", "d"}));
    CHECK(mgr.next("a") == "c");  // 跳过已移除
}
