#include <doctest/doctest.h>

#include <algorithm>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

#include <pjh_platform/fs.hpp>

#include "card/card.hpp"
#include "card/catalog.hpp"
#include "card/def.hpp"
#include "card/manager.hpp"
#include "config/resource.hpp"

namespace
{
    using namespace tkw::card;

    Card make_card(std::string inst, std::string def, Suit s, int n)
    {
        return Card{std::move(inst), std::move(def), s, n};
    }
}

TEST_CASE("card: CardStack push/pop/top semantics")
{
    CardStack stack;
    CHECK(stack.empty());
    CHECK(stack.pop().is_none());
    CHECK(stack.top().is_none());

    stack.push(make_card("a#0", "sha", Suit::Spade, 7));
    stack.push(make_card("a#1", "sha", Suit::Heart, 10));

    CHECK(stack.size() == 2);
    REQUIRE(stack.top().is_some());
    CHECK(stack.top().unwrap()->instance_id == "a#1");  // 顶 = 最后 push

    auto c = stack.pop();
    REQUIRE(c.is_some());
    CHECK(c.unwrap().instance_id == "a#1");
    CHECK(stack.size() == 1);
}

TEST_CASE("card: CardStack shuffle is a permutation of the pile")
{
    CardStack stack;
    for (int i = 0; i < 30; ++i)
        stack.push(make_card("c#" + std::to_string(i), "sha", Suit::Spade, 1));

    std::vector<std::string> before;
    for (int i = 0; i < 30; ++i)
        before.push_back(stack.pop().unwrap().instance_id);

    for (const auto &id : before)
        stack.push(make_card(id, "sha", Suit::Spade, 1));

    std::mt19937 rng(42);
    stack.shuffle(rng);

    std::vector<std::string> after;
    for (int i = 0; i < 30; ++i)
        after.push_back(stack.pop().unwrap().instance_id);

    CHECK(std::is_permutation(before.begin(), before.end(), after.begin()));
    CHECK(after != before);
}

TEST_CASE("card: manager draw/discard")
{
    CardManager mgr;
    mgr.add_to_hand("p1", make_card("h#0", "sha", Suit::Spade, 7));
    mgr.discard(mgr.remove_from_hand("p1", "h#0").unwrap());
    CHECK(mgr.discard_size() == 1);

    CHECK(mgr.draw().is_none());  // 摸牌堆为空，不自动洗回弃牌堆
    CHECK(mgr.draw_size() == 0);
}

TEST_CASE("card: manager hand zone add/remove/query")
{
    CardManager mgr;
    mgr.add_to_hand("p1", make_card("h#0", "sha", Suit::Spade, 7));
    mgr.add_to_hand("p1", make_card("h#1", "shan", Suit::Diamond, 2));
    mgr.add_to_hand("p2", make_card("h#2", "tao", Suit::Heart, 3));

    CHECK(mgr.hand_size("p1") == 2);
    CHECK(mgr.hand_size("p2") == 1);
    CHECK(mgr.hand_size("ghost") == 0);
    CHECK(mgr.hand("p1").size() == 2);
    CHECK(mgr.hand("ghost").empty());

    auto gone = mgr.remove_from_hand("p1", "h#0");
    REQUIRE(gone.is_some());
    CHECK(gone.unwrap().def_id == "sha");
    CHECK(mgr.hand_size("p1") == 1);
    CHECK(mgr.hand("p1")[0].instance_id == "h#1");

    CHECK(mgr.remove_from_hand("p1", "h#0").is_none());  // 已移除
    CHECK(mgr.remove_from_hand("ghost", "h#2").is_none());
}

TEST_CASE("card: manager equip/judge zones")
{
    CardManager mgr;
    mgr.add_to_equip("p1", make_card("e#0", "liangnu", Suit::Club, 1));
    mgr.add_to_equip("p1", make_card("e#1", "chitu", Suit::Heart, 5));
    mgr.add_to_judge("p1", make_card("j#0", "lesi", Suit::Spade, 6));

    CHECK(mgr.equip_size("p1") == 2);
    CHECK(mgr.judge_size("p1") == 1);

    auto removed = mgr.remove_from_equip("p1", "e#0");
    REQUIRE(removed.is_some());
    CHECK(removed.unwrap().def_id == "liangnu");
    CHECK(mgr.equip_size("p1") == 1);

    CHECK(mgr.remove_from_judge("p1", "j#0").is_some());
    CHECK(mgr.judge_size("p1") == 0);
}

#ifdef TKW_TEST_RESOURCE_DIR
TEST_CASE("card: build_deck materialises every copy of every def")
{
    tkw::config::ResourceStore store(TKW_TEST_RESOURCE_DIR);
    auto cat = tkw::card::CardDefCatalog::load(store, "deck").unwrap();

    tkw::card::CardManager mgr;
    mgr.build_deck(cat);

    CHECK(mgr.draw_size() == 108);
    CHECK(mgr.discard_size() == 0);

    std::vector<std::string> ids;
    std::unordered_map<std::string, int> drawn;
    for (std::size_t i = 0; i < 108; ++i)
    {
        auto c = mgr.draw();
        REQUIRE(c.is_some());
        ids.push_back(c.unwrap().instance_id);
        drawn[c.unwrap().def_id]++;
    }
    CHECK(mgr.draw().is_none());

    // instance_id 全局唯一
    std::sort(ids.begin(), ids.end());
    CHECK(std::adjacent_find(ids.begin(), ids.end()) == ids.end());

    // 每张定义的实体牌数量 == 其 copies 数
    for (const auto &[def_id, def] : cat)
        CHECK(drawn[def_id] == static_cast<int>(def.copies.size()));
}
#endif  // TKW_TEST_RESOURCE_DIR