#include <doctest/doctest.h>

#include <string>
#include <vector>

#include "card/card.hpp"
#include "card/catalog.hpp"
#include "card/def.hpp"
#include "card/manager.hpp"
#include "config/resource.hpp"
#include "entity/hp.hpp"
#include "entity/manager.hpp"
#include "event/event_bus.hpp"
#include "game/context.hpp"
#include "game/decision.hpp"
#include "game/distance.hpp"
#include "game/resolver.hpp"

namespace
{
    using namespace tkw::game;
    using tkw::EntityManager;
    using tkw::card::Card;
    using tkw::card::CardDefCatalog;
    using tkw::card::CardManager;
    using tkw::card::ResponseKind;
    using tkw::entity::Entity;
    using tkw::entity::Hp;

    /** 测试对局：bus + entities + cards + catalog + ctx 聚合。 */
    struct TestGame
    {
        tkw::EventBus bus;
        EntityManager entities;
        CardManager cards;
        CardDefCatalog catalog;
        GameContext ctx;

        TestGame(const char *deck_name) :
            bus(), entities(bus), cards(),
            catalog(load_catalog(deck_name))
        {
            ctx.bus = &bus;
            ctx.entities = &entities;
            ctx.cards = &cards;
            ctx.catalog = &catalog;
        }

        Entity *add_player(const std::string &id, int seat, int hp)
        {
            auto r = entities.create(id, seat, Hp::make(hp));
            REQUIRE(r.is_ok());
            return r.unwrap();
        }

        void give(const std::string &id, const std::string &def_id, const char *inst)
        {
            const auto def = catalog.find(def_id);
            REQUIRE(def.is_some());
            const auto &copy = def.unwrap()->copies[0];
            cards.add_to_hand(id, Card{inst, def_id, copy.suit, copy.number});
        }

        void equip(const std::string &id, const std::string &def_id, const char *inst)
        {
            const auto def = catalog.find(def_id);
            REQUIRE(def.is_some());
            const auto &copy = def.unwrap()->copies[0];
            cards.add_to_equip(id, Card{inst, def_id, copy.suit, copy.number});
        }

        static CardDefCatalog load_catalog(const char *name)
        {
            tkw::config::ResourceStore store(TKW_TEST_RESOURCE_DIR);
            auto r = CardDefCatalog::load(store, name);
            REQUIRE(r.is_ok());
            return std::move(r).unwrap();
        }
    };

    /** 确定性决策源：respond=是否总是打出响应牌；选牌=目标手牌第一张。 */
    struct TestDecider : DecisionSource
    {
        bool respond = false;

        bool play_response(GameContext &, const std::string &, ResponseKind) override
        {
            return respond;
        }

        Card pick_card_from_target(
            GameContext &ctx, const std::string &, const std::string &target) override
        {
            const auto &hand = ctx.cards->hand(target);
            REQUIRE(!hand.empty());
            return hand.front();
        }
    };
}

TEST_CASE("game: seat distance on a circle")
{
    TestGame g("deck");
    g.add_player("a", 0, 4);
    g.add_player("b", 1, 4);
    g.add_player("c", 2, 4);
    g.add_player("d", 3, 4);

    CHECK(seat_distance(g.ctx, "a", "a") == 0);
    CHECK(seat_distance(g.ctx, "a", "b") == 1);
    CHECK(seat_distance(g.ctx, "a", "c") == 2);
    CHECK(seat_distance(g.ctx, "a", "d") == 1);
    CHECK(seat_distance(g.ctx, "b", "d") == 2);
}

TEST_CASE("game: attack range base and weapon")
{
    TestGame g("deck");
    g.add_player("a", 0, 4);
    g.add_player("b", 1, 4);
    g.add_player("c", 2, 4);
    g.add_player("d", 3, 4);

    // 无武器：基础攻击距离 1
    CHECK(in_attack_range(g.ctx, "a", "b"));
    CHECK(in_attack_range(g.ctx, "a", "d"));
    CHECK(!in_attack_range(g.ctx, "a", "c"));

    // 青龙偃月刀 range 3
    g.equip("a", "qinglong", "e#0");
    CHECK(in_attack_range(g.ctx, "a", "c"));
}

TEST_CASE("game: horses adjust attack distance")
{
    TestGame g("deck");
    g.add_player("a", 0, 4);
    g.add_player("b", 1, 4);
    g.add_player("c", 2, 4);
    g.add_player("d", 3, 4);

    // a 装 -1马（赤兔）：a 到 c 距离 2-1=1 ≤ 1
    g.equip("a", "chitu", "h#1");
    CHECK(in_attack_range(g.ctx, "a", "c"));

    // b 装 +1马（绝影）：c 到 b 距离 1+1=2 > 1
    g.equip("b", "jueying", "h#2");
    CHECK(!in_attack_range(g.ctx, "c", "b"));
    // a→b 仍为 1（-1 与 +1 相抵）
    CHECK(in_attack_range(g.ctx, "a", "b"));
}

TEST_CASE("game: valid_targets by scope and range")
{
    TestGame g("deck");
    g.add_player("a", 0, 4);
    g.add_player("b", 1, 4);
    g.add_player("c", 2, 4);
    g.add_player("d", 3, 4);

    auto sha = g.catalog.find("sha").unwrap();
    auto wuzhong = g.catalog.find("wuzhong").unwrap();
    auto taoyuan = g.catalog.find("taoyuan").unwrap();
    auto nanman = g.catalog.find("nanman").unwrap();

    auto t_sha = valid_targets(g.ctx, "a", *sha);          // 攻击距离 1 → b, d
    CHECK(t_sha == std::vector<std::string>({"b", "d"}));
    auto t_wu = valid_targets(g.ctx, "a", *wuzhong);       // 仅自己
    CHECK(t_wu == std::vector<std::string>({"a"}));
    auto t_ty = valid_targets(g.ctx, "a", *taoyuan);       // 全部
    CHECK(t_ty == std::vector<std::string>({"a", "b", "c", "d"}));
    auto t_nm = valid_targets(g.ctx, "a", *nanman);        // 其他全部
    CHECK(t_nm == std::vector<std::string>({"b", "c", "d"}));
}

TEST_CASE("game: sha hits when no jink")
{
    TestGame g("deck");
    g.add_player("a", 0, 4);
    auto *b = g.add_player("b", 1, 4);
    g.give("a", "sha", "s#0");

    TestDecider decider;  // 不响应
    const auto played = g.cards.hand("a")[0];
    auto r = resolve_play(g.ctx, decider, "a", played, {"b"});
    REQUIRE(r.is_ok());
    CHECK(b->get_hp() == 3);
    CHECK(g.cards.hand_size("a") == 0);
    CHECK(g.cards.discard_size() == 1);  // 打出的杀已弃置
}

TEST_CASE("game: sha blocked by jink")
{
    TestGame g("deck");
    g.add_player("a", 0, 4);
    auto *b = g.add_player("b", 1, 4);
    g.give("a", "sha", "s#0");
    g.give("b", "shan", "s#1");

    TestDecider decider;
    decider.respond = true;
    const auto played = g.cards.hand("a")[0];
    auto r = resolve_play(g.ctx, decider, "a", played, {"b"});
    REQUIRE(r.is_ok());
    CHECK(b->get_hp() == 4);                    // 未受伤
    CHECK(g.cards.hand_size("b") == 0);         // 闪已消耗
    CHECK(g.cards.discard_size() == 2);         // 杀 + 闪
}

TEST_CASE("game: sha out of range is rejected and card kept")
{
    TestGame g("deck");
    g.add_player("a", 0, 4);
    g.add_player("b", 1, 4);
    g.add_player("c", 2, 4);
    g.add_player("d", 3, 4);
    g.give("a", "sha", "s#0");

    TestDecider decider;
    const auto played = g.cards.hand("a")[0];
    auto r = resolve_play(g.ctx, decider, "a", played, {"c"});  // 距离 2
    REQUIRE(r.is_err());
    CHECK(r.unwrap_err() == EffectError::OutOfRange);
    CHECK(g.cards.hand_size("a") == 1);  // 牌未消耗
}

TEST_CASE("game: tao heals")
{
    TestGame g("deck");
    auto *a = g.add_player("a", 0, 3);
    g.add_player("b", 1, 4);
    a->take_damage("b", 1, false);
    CHECK(a->get_hp() == 2);
    g.give("a", "tao", "t#0");

    TestDecider decider;
    const auto played = g.cards.hand("a")[0];
    auto r = resolve_play(g.ctx, decider, "a", played, {"a"});
    REQUIRE(r.is_ok());
    CHECK(a->get_hp() == 3);
}

TEST_CASE("game: wuzhong draws two")
{
    TestGame g("deck");
    g.add_player("a", 0, 4);
    g.cards.build_deck(g.catalog);
    g.give("a", "wuzhong", "w#0");
    const auto before = g.cards.hand_size("a");

    TestDecider decider;
    const auto played = g.cards.hand("a")[0];
    auto r = resolve_play(g.ctx, decider, "a", played, {"a"});
    REQUIRE(r.is_ok());
    CHECK(g.cards.hand_size("a") == before - 1 + 2);  // 打出 1 张 + 摸 2 张
    CHECK(g.cards.draw_size() == 108 - 2);
}

TEST_CASE("game: guohe discards target card")
{
    TestGame g("deck");
    g.add_player("a", 0, 4);
    g.add_player("b", 1, 4);
    g.give("a", "guohe", "g#0");
    g.give("b", "sha", "s#1");
    g.give("b", "shan", "s#2");

    TestDecider decider;
    const auto played = g.cards.hand("a")[0];
    auto r = resolve_play(g.ctx, decider, "a", played, {"b"});
    REQUIRE(r.is_ok());
    CHECK(g.cards.hand_size("b") == 1);   // 弃了一张
    CHECK(g.cards.discard_size() == 2);   // 过河拆桥 + 被弃的牌
}

TEST_CASE("game: shunshou steals target card to hand")
{
    TestGame g("deck");
    g.add_player("a", 0, 4);
    g.add_player("b", 1, 4);
    g.give("a", "shunshou", "ss#0");
    g.give("b", "tao", "t#1");

    TestDecider decider;
    const auto played = g.cards.hand("a")[0];
    auto r = resolve_play(g.ctx, decider, "a", played, {"b"});
    REQUIRE(r.is_ok());
    CHECK(g.cards.hand_size("b") == 0);
    CHECK(g.cards.hand_size("a") == 1);   // 顺来的桃
    CHECK(g.cards.hand("a")[0].def_id == "tao");
}

TEST_CASE("game: nanman hits all others without sha")
{
    TestGame g("deck");
    g.add_player("a", 0, 4);
    auto *b = g.add_player("b", 1, 4);
    auto *c = g.add_player("c", 2, 4);
    g.give("a", "nanman", "n#0");
    g.give("b", "sha", "s#1");  // b 有杀

    TestDecider decider;  // 不响应
    const auto played = g.cards.hand("a")[0];
    auto r = resolve_play(g.ctx, decider, "a", played, {"b", "c"});
    REQUIRE(r.is_ok());
    CHECK(b->get_hp() == 3);
    CHECK(c->get_hp() == 3);

    // 响应：b 打出杀免伤，c 无杀受伤
    TestDecider yes;
    yes.respond = true;
    g.give("a", "nanman", "n#1");
    const auto played2 = g.cards.hand("a")[0];
    auto r2 = resolve_play(g.ctx, yes, "a", played2, {"b", "c"});
    REQUIRE(r2.is_ok());
    CHECK(b->get_hp() == 3);  // 第二次 b 已无杀，仍受伤
    CHECK(c->get_hp() == 2);
}

TEST_CASE("game: juedou target without sha takes damage")
{
    TestGame g("deck");
    g.add_player("a", 0, 4);
    auto *b = g.add_player("b", 1, 4);
    g.give("a", "juedou", "j#0");

    TestDecider decider;
    const auto played = g.cards.hand("a")[0];
    auto r = resolve_play(g.ctx, decider, "a", played, {"b"});
    REQUIRE(r.is_ok());
    CHECK(b->get_hp() == 3);  // 目标先开始，不出杀 → 受 a 造成的 1 点伤害
}

TEST_CASE("game: juedou exchange of sha")
{
    TestGame g("deck");
    auto *a = g.add_player("a", 0, 4);
    auto *b = g.add_player("b", 1, 4);
    g.give("a", "juedou", "j#0");
    g.give("a", "sha", "s#1");
    g.give("b", "sha", "s#2");

    TestDecider decider;
    decider.respond = true;
    const auto played = g.cards.hand("a")[0];  // 决斗
    auto r = resolve_play(g.ctx, decider, "a", played, {"b"});
    REQUIRE(r.is_ok());
    CHECK(a->get_hp() == 4);
    CHECK(b->get_hp() == 3);  // b 的杀耗尽后受 1 点伤害
}

TEST_CASE("game: unsupported kind is rejected and card kept")
{
    TestGame g("deck");
    g.add_player("a", 0, 4);
    g.add_player("b", 1, 4);
    g.give("a", "jiedao", "j#0");  // 借刀杀人：未实现

    TestDecider decider;
    const auto played = g.cards.hand("a")[0];
    auto r = resolve_play(g.ctx, decider, "a", played, {"b"});
    REQUIRE(r.is_err());
    CHECK(r.unwrap_err() == EffectError::UnsupportedKind);
    CHECK(g.cards.hand_size("a") == 1);  // 未消耗
}