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
#include "game/loop.hpp"
#include "game/resolver.hpp"
#include "game/turn.hpp"
#include "util/rng.hpp"

namespace
{
    using namespace tkw::game;
    using tkw::EntityManager;
    using tkw::Option;
    using tkw::card::Card;
    using tkw::card::CardEffectKind;
    using tkw::card::CardDefCatalog;
    using tkw::card::CardManager;
    using tkw::card::ResponseKind;
    using tkw::card::Suit;
    using tkw::entity::Entity;
    using tkw::entity::Hp;

    /** 测试对局：bus + entities + cards + catalog + ctx 聚合。 */
    struct TestGame
    {
        tkw::EventBus bus;
        EntityManager entities;
        CardManager cards;
        CardDefCatalog catalog;
        tkw::SeededRng rng{1};
        GameContext ctx;

        TestGame(const char *deck_name) :
            bus(), entities(bus), cards(),
            catalog(load_catalog(deck_name))
        {
            ctx.bus = &bus;
            ctx.entities = &entities;
            ctx.cards = &cards;
            ctx.catalog = &catalog;
            ctx.rng = &rng;
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

    /** 确定性决策源：respond=是否总是打出响应牌；save=是否有桃就救；
     *  counter=有无懈就出；triggers=会发动的装备效果；plays=出牌脚本
     *  （依次执行，耗尽即结束出牌）；弃牌=手牌前 count 张。 */
    struct TestDecider : DecisionSource
    {
        bool respond = false;
        bool save = false;
        bool counter = false;
        std::vector<CardEffectKind> triggers;
        std::vector<PlayAction> plays;
        std::size_t play_cursor = 0;

        bool play_response(GameContext &, const std::string &, ResponseKind) override
        {
            return respond;
        }

        bool play_peach(GameContext &, const std::string &, const std::string &) override
        {
            return save;
        }

        bool play_counter(GameContext &, const std::string &) override
        {
            return counter;
        }

        bool trigger_effect(
            GameContext &, const std::string &, CardEffectKind kind) override
        {
            return std::find(triggers.begin(), triggers.end(), kind) != triggers.end();
        }

        Card pick_card_from_target(
            GameContext &ctx, const std::string &, const std::string &target) override
        {
            const auto &hand = ctx.cards->hand(target);
            REQUIRE(!hand.empty());
            return hand.front();
        }

        Option<PlayAction> choose_play(GameContext &, const std::string &) override
        {
            if (play_cursor >= plays.size())
                return Option<PlayAction>::None();
            return Option<PlayAction>::Some(plays[play_cursor++]);
        }

        std::vector<std::string> choose_discards(
            GameContext &ctx, const std::string &player, int count) override
        {
            const auto &hand = ctx.cards->hand(player);
            std::vector<std::string> out;
            for (int i = 0; i < count && i < static_cast<int>(hand.size()); ++i)
                out.push_back(hand[i].instance_id);
            return out;
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

// ── 回合流程 ──────────────────────────────────────────────────────────

TEST_CASE("game: turn draws two and trims to hand limit")
{
    TestGame g("deck");
    g.add_player("a", 0, 3);  // 手牌上限 3
    g.add_player("b", 1, 4);
    g.cards.build_deck(g.catalog);
    g.give("a", "sha", "s#1");
    g.give("a", "sha", "s#2");
    g.give("a", "shan", "s#3");

    TestDecider decider;  // 不出牌
    auto r = execute_turn(g.ctx, decider, "a");
    REQUIRE(r.is_ok());
    CHECK(g.cards.hand_size("a") == 3);   // 3 + 摸2 = 5，弃到上限 3
    CHECK(g.cards.discard_size() == 2);   // 弃了 2 张
}

TEST_CASE("game: sha limit one per turn")
{
    TestGame g("deck");
    g.add_player("a", 0, 4);
    auto *b = g.add_player("b", 1, 4);
    g.cards.build_deck(g.catalog);
    g.give("a", "sha", "s#1");
    g.give("a", "sha", "s#2");

    TestDecider decider;
    decider.plays = {PlayAction{"s#1", {"b"}}, PlayAction{"s#2", {"b"}}};
    auto r = execute_turn(g.ctx, decider, "a");
    REQUIRE(r.is_err());
    CHECK(r.unwrap_err() == TurnError::ShaLimitExceeded);
    CHECK(b->get_hp() == 3);            // 第一刀命中
    CHECK(g.cards.hand_size("a") == 3); // 2杀+摸2=4，打出1张剩3
}

TEST_CASE("game: liangnu lifts sha limit")
{
    TestGame g("deck");
    g.add_player("a", 0, 4);
    auto *b = g.add_player("b", 1, 4);
    g.cards.build_deck(g.catalog);
    g.equip("a", "liangnu", "e#0");
    g.give("a", "sha", "s#1");
    g.give("a", "sha", "s#2");

    TestDecider decider;
    decider.plays = {PlayAction{"s#1", {"b"}}, PlayAction{"s#2", {"b"}}};
    auto r = execute_turn(g.ctx, decider, "a");
    REQUIRE(r.is_ok());
    CHECK(b->get_hp() == 2);            // 两刀全中
    CHECK(g.cards.hand_size("a") == 2); // 4 - 2 = 2（上限 4 不弃）
}

TEST_CASE("game: equip weapon and replace same slot")
{
    TestGame g("deck");
    g.add_player("a", 0, 4);
    g.add_player("b", 1, 4);
    g.cards.build_deck(g.catalog);
    g.give("a", "qinglong", "e#1");
    g.give("a", "qinggang", "e#2");

    TestDecider decider;
    decider.plays = {PlayAction{"e#1", {}}, PlayAction{"e#2", {}}};
    auto r = execute_turn(g.ctx, decider, "a");
    REQUIRE(r.is_ok());
    CHECK(g.cards.equip_size("a") == 1);              // 同槽位只留一件
    CHECK(g.cards.equip("a")[0].def_id == "qinggang");
    CHECK(g.cards.discard_size() >= 1);               // 被替换的武器已弃置
}

TEST_CASE("game: lesi non-heart skips play phase")
{
    TestGame g("deck");
    g.add_player("a", 0, 3);
    auto *b = g.add_player("b", 1, 4);
    g.give("a", "sha", "s#1");
    g.cards.add_to_judge("a", Card{"L#0", "lesi", Suit::Spade, 6});

    // 种牌堆：堆顶是判定牌（黑桃6 → 非红桃跳过出牌），其下两张供摸牌
    g.cards.add_to_draw(Card{"d#0", "sha", Suit::Club, 2});
    g.cards.add_to_draw(Card{"d#1", "shan", Suit::Diamond, 2});
    g.cards.add_to_draw(Card{"j#0", "sha", Suit::Spade, 6});

    TestDecider decider;
    decider.plays = {PlayAction{"s#1", {"b"}}};
    auto r = execute_turn(g.ctx, decider, "a");
    REQUIRE(r.is_ok());
    CHECK(b->get_hp() == 4);            // 出牌阶段被跳过
    CHECK(g.cards.hand_size("a") == 3); // 杀 + 摸的 2 张
    CHECK(g.cards.judge_size("a") == 0);
}

TEST_CASE("game: lesi heart judge allows play")
{
    TestGame g("deck");
    g.add_player("a", 0, 3);
    auto *b = g.add_player("b", 1, 4);
    g.give("a", "sha", "s#1");
    g.cards.add_to_judge("a", Card{"L#0", "lesi", Suit::Spade, 6});

    g.cards.add_to_draw(Card{"d#0", "sha", Suit::Club, 2});
    g.cards.add_to_draw(Card{"d#1", "shan", Suit::Diamond, 2});
    g.cards.add_to_draw(Card{"j#0", "sha", Suit::Heart, 3});

    TestDecider decider;
    decider.plays = {PlayAction{"s#1", {"b"}}};
    auto r = execute_turn(g.ctx, decider, "a");
    REQUIRE(r.is_ok());
    CHECK(b->get_hp() == 3);            // 杀正常打出
    CHECK(g.cards.hand_size("a") == 2); // 杀打出，剩摸的 2 张
}

TEST_CASE("game: lightning strikes on spade 2-9")
{
    TestGame g("deck");
    auto *a = g.add_player("a", 0, 4);
    g.add_player("b", 1, 4);
    g.cards.add_to_judge("a", Card{"L#0", "shandian", Suit::Spade, 1});

    g.cards.add_to_draw(Card{"d#0", "sha", Suit::Club, 2});
    g.cards.add_to_draw(Card{"d#1", "shan", Suit::Diamond, 2});
    g.cards.add_to_draw(Card{"j#0", "sha", Suit::Spade, 8});

    TestDecider decider;
    auto r = execute_turn(g.ctx, decider, "a");
    REQUIRE(r.is_ok());
    CHECK(a->get_hp() == 1);            // 雷伤 3
    CHECK(g.cards.judge_size("a") == 0);
    CHECK(g.cards.judge_size("b") == 0);
}

TEST_CASE("game: lightning passes to next player")
{
    TestGame g("deck");
    auto *a = g.add_player("a", 0, 4);
    g.add_player("b", 1, 4);
    g.cards.add_to_judge("a", Card{"L#0", "shandian", Suit::Spade, 1});

    g.cards.add_to_draw(Card{"d#0", "sha", Suit::Club, 2});
    g.cards.add_to_draw(Card{"d#1", "shan", Suit::Diamond, 2});
    g.cards.add_to_draw(Card{"j#0", "sha", Suit::Heart, 5});

    TestDecider decider;
    auto r = execute_turn(g.ctx, decider, "a");
    REQUIRE(r.is_ok());
    CHECK(a->get_hp() == 4);              // 未劈中
    CHECK(g.cards.judge_size("a") == 0);
    CHECK(g.cards.judge_size("b") == 1);  // 移到下家
    CHECK(g.cards.judge("b")[0].def_id == "shandian");
}

// ── 濒死与死亡 ──────────────────────────────────────────────────────

TEST_CASE("game: dying rescued by self peach")
{
    TestGame g("deck");
    g.add_player("a", 0, 4);
    auto *b = g.add_player("b", 1, 4);
    b->take_damage("a", 3, false);  // b: 4 → 1
    g.give("b", "tao", "t#0");
    g.give("a", "sha", "s#1");

    TestDecider decider;
    decider.save = true;
    const auto played = g.cards.hand("a")[0];
    auto r = resolve_play(g.ctx, decider, "a", played, {"b"});
    REQUIRE(r.is_ok());
    CHECK(b->get_hp() == 1);                   // 0 → 桃救回 1
    CHECK(g.entities.find("b").is_some());     // 存活
    CHECK(g.cards.hand_size("b") == 0);        // 桃已消耗
}

TEST_CASE("game: dying rescued by another player's peach")
{
    TestGame g("deck");
    g.add_player("a", 0, 4);
    auto *b = g.add_player("b", 1, 4);
    g.add_player("c", 2, 4);
    b->take_damage("a", 3, false);  // b: 1
    g.give("c", "tao", "t#0");
    g.give("a", "sha", "s#1");

    TestDecider decider;
    decider.save = true;
    const auto played = g.cards.hand("a")[0];
    auto r = resolve_play(g.ctx, decider, "a", played, {"b"});
    REQUIRE(r.is_ok());
    CHECK(b->get_hp() == 1);                   // 救回
    CHECK(g.entities.find("b").is_some());
    CHECK(g.cards.hand_size("c") == 0);        // c 的桃被消耗
}

TEST_CASE("game: unrescued death removes entity and rewards killer")
{
    TestGame g("deck");
    g.add_player("a", 0, 4);
    g.add_player("b", 1, 4);
    g.cards.build_deck(g.catalog);
    auto b = g.entities.find("b").unwrap();
    b->take_damage("a", 3, false);  // b: 1
    g.give("b", "sha", "s#1");      // b 手牌，死后弃置
    g.give("a", "sha", "s#2");

    TestDecider decider;  // save=false
    const auto played = g.cards.hand("a")[0];
    auto r = resolve_play(g.ctx, decider, "a", played, {"b"});
    REQUIRE(r.is_ok());
    CHECK(g.entities.find("b").is_none());        // 死亡移除
    CHECK(g.cards.hand_size("a") == 3);           // 击杀奖励摸 3
    CHECK(g.cards.discard_size() >= 2);           // a 的杀 + b 的手牌
}

TEST_CASE("game: death discards equipment and judgement zones")
{
    TestGame g("deck");
    g.add_player("a", 0, 4);
    auto *b = g.add_player("b", 1, 4);
    b->take_damage("a", 3, false);  // b: 1
    g.equip("b", "qinglong", "e#0");
    g.cards.add_to_judge("b", Card{"L#0", "lesi", Suit::Spade, 6});
    g.give("a", "sha", "s#2");

    TestDecider decider;
    const auto played = g.cards.hand("a")[0];
    auto r = resolve_play(g.ctx, decider, "a", played, {"b"});
    REQUIRE(r.is_ok());
    CHECK(g.entities.find("b").is_none());
    CHECK(g.cards.equip_size("b") == 0);
    CHECK(g.cards.judge_size("b") == 0);
    CHECK(g.cards.discard_size() >= 2);  // 装备 + 判定牌
}

TEST_CASE("game: dying and died events published")
{
    TestGame g("deck");
    g.add_player("a", 0, 4);
    g.add_player("b", 1, 4);
    g.entities.find("b").unwrap()->take_damage("a", 3, false);  // b: 1
    g.give("a", "sha", "s#2");

    int dying = 0;
    int died = 0;
    auto h1 = g.bus.subscribe(tkw::Handler<tkw::EntityDyingEvent>(
        [&](tkw::HandlerContext<tkw::EntityDyingEvent> &) { ++dying; }));
    auto h2 = g.bus.subscribe(tkw::Handler<tkw::EntityDiedEvent>(
        [&](tkw::HandlerContext<tkw::EntityDiedEvent> &) { ++died; }));

    TestDecider decider;  // 不救 → 死亡
    const auto played = g.cards.hand("a")[0];
    auto r = resolve_play(g.ctx, decider, "a", played, {"b"});
    REQUIRE(r.is_ok());
    CHECK(dying == 1);
    CHECK(died == 1);

    // 救回时只发 Dying，不发 Died
    dying = 0;
    died = 0;
    g.add_player("c", 2, 4);
    g.entities.find("c").unwrap()->take_damage("a", 3, false);  // c: 1
    g.give("c", "tao", "t#0");
    g.give("a", "sha", "s#3");
    TestDecider saver;
    saver.save = true;
    const auto played2 = g.cards.hand("a")[0];
    auto r2 = resolve_play(g.ctx, saver, "a", played2, {"c"});
    REQUIRE(r2.is_ok());
    CHECK(dying == 1);
    CHECK(died == 0);
}

// ── 无懈可击 ─────────────────────────────────────────────────────────

TEST_CASE("game: wuxie cancels aoe effect on one target only")
{
    TestGame g("deck");
    g.add_player("a", 0, 4);
    auto *b = g.add_player("b", 1, 4);
    auto *c = g.add_player("c", 2, 4);
    g.give("a", "nanman", "n#0");
    g.give("b", "wuxie", "w#0");

    TestDecider decider;
    decider.counter = true;
    const auto played = g.cards.hand("a")[0];
    auto r = resolve_play(g.ctx, decider, "a", played, {"b", "c"});
    REQUIRE(r.is_ok());
    CHECK(b->get_hp() == 4);   // b 被无懈抵消
    CHECK(c->get_hp() == 3);   // c 无无懈 → 受伤
    CHECK(g.cards.hand_size("b") == 0);  // 无懈已消耗
}

TEST_CASE("game: wuxie cancels guohe so target keeps cards")
{
    TestGame g("deck");
    g.add_player("a", 0, 4);
    auto *b = g.add_player("b", 1, 4);
    g.give("a", "guohe", "g#0");
    g.give("b", "sha", "s#1");
    g.give("b", "wuxie", "w#0");

    TestDecider decider;
    decider.counter = true;
    const auto played = g.cards.hand("a")[0];
    auto r = resolve_play(g.ctx, decider, "a", played, {"b"});
    REQUIRE(r.is_ok());
    CHECK(g.cards.hand_size("b") == 1);  // 过拆被抵消，杀还在（无懈已打）
}

TEST_CASE("game: wuxie chain flips outcome (second wuxie counters first)")
{
    TestGame g("deck");
    g.add_player("a", 0, 4);
    auto *b = g.add_player("b", 1, 4);
    g.add_player("c", 2, 4);
    g.give("a", "guohe", "g#0");
    g.give("b", "sha", "s#1");
    g.give("b", "wuxie", "w#0");  // b 出无懈抵消过拆
    g.give("c", "wuxie", "w#1");  // c 再出无懈抵消 b 的无懈 → 过拆生效
    g.give("c", "tao", "t#0");

    TestDecider decider;
    decider.counter = true;
    const auto played = g.cards.hand("a")[0];
    auto r = resolve_play(g.ctx, decider, "a", played, {"b"});
    REQUIRE(r.is_ok());
    CHECK(g.cards.hand_size("b") == 0);  // b 的无懈与杀都被拆掉
    CHECK(g.cards.hand_size("c") == 1);  // c 只剩桃
}

TEST_CASE("game: tao is a basic card and cannot be countered")
{
    TestGame g("deck");
    auto *a = g.add_player("a", 0, 4);
    g.add_player("b", 1, 4);
    a->take_damage("b", 2, false);  // a: 2
    g.give("a", "tao", "t#0");
    g.give("a", "wuxie", "w#0");

    TestDecider decider;
    decider.counter = true;
    const auto played = g.cards.hand("a")[0];  // 桃
    auto r = resolve_play(g.ctx, decider, "a", played, {"a"});
    REQUIRE(r.is_ok());
    CHECK(a->get_hp() == 3);   // 桃生效（基本牌不可无懈）
    CHECK(g.cards.hand_size("a") == 1);  // 无懈未被消耗
}

// ── 对局主循环 ───────────────────────────────────────────────────────

TEST_CASE("game: next_player wraps and skips removed players")
{
    TestGame g("deck");
    g.add_player("a", 0, 4);
    g.add_player("b", 1, 4);
    g.add_player("c", 2, 4);
    g.add_player("d", 3, 4);
    CHECK(next_player(g.ctx, "a") == "b");
    CHECK(next_player(g.ctx, "d") == "a");
    g.entities.remove("b");
    CHECK(next_player(g.ctx, "a") == "c");  // b 已移除 → 跳过
    CHECK(next_player(g.ctx, "d") == "a");
}

TEST_CASE("game: prepare_game deals four initial cards to each")
{
    TestGame g("deck");
    g.add_player("a", 0, 4);
    g.add_player("b", 1, 4);
    prepare_game(g.ctx, 4);
    CHECK(g.cards.hand_size("a") == 4);
    CHECK(g.cards.hand_size("b") == 4);
    CHECK(g.cards.draw_size() == 108 - 8);
    CHECK(alive_count(g.ctx) == 2);
}

TEST_CASE("game: play_game ends when one player kills the other")
{
    TestGame g("deck");
    g.add_player("a", 0, 1);
    g.add_player("b", 1, 1);
    g.give("a", "sha", "s#1");  // 发牌前给一张杀

    TestDecider decider;
    decider.plays = {PlayAction{"s#1", {"b"}}};
    auto r = play_game(g.ctx, decider, "a");
    REQUIRE(r.is_ok());
    CHECK(r.unwrap().winner == "a");
    CHECK(r.unwrap().rounds >= 1);
    CHECK(g.entities.find("b").is_none());   // b 已死亡移除
    CHECK(g.entities.find("a").is_some());
}

TEST_CASE("game: play_game with no players is an error")
{
    TestGame g("deck");
    TestDecider decider;
    auto r = play_game(g.ctx, decider, "a");
    REQUIRE(r.is_err());
    CHECK(r.unwrap_err() == LoopError::NoPlayers);
}

// ── 杀结算：装备效果 ─────────────────────────────────────────────────

TEST_CASE("game: renwang blocks black sha")
{
    TestGame g("deck");
    g.add_player("a", 0, 4);
    auto *b = g.add_player("b", 1, 4);
    g.equip("b", "renwang", "e#0");
    g.give("a", "sha", "s#1");  // copies[0] = 黑桃7（黑杀）

    TestDecider decider;
    const auto played = g.cards.hand("a")[0];
    auto r = resolve_play(g.ctx, decider, "a", played, {"b"});
    REQUIRE(r.is_ok());
    CHECK(b->get_hp() == 4);  // 黑杀无效
}

TEST_CASE("game: renwang does not block red sha")
{
    TestGame g("deck");
    g.add_player("a", 0, 4);
    auto *b = g.add_player("b", 1, 4);
    g.equip("b", "renwang", "e#0");
    g.cards.add_to_hand("a", Card{"s#1", "sha", Suit::Heart, 10});  // 红杀

    TestDecider decider;
    const auto played = g.cards.hand("a")[0];
    auto r = resolve_play(g.ctx, decider, "a", played, {"b"});
    REQUIRE(r.is_ok());
    CHECK(b->get_hp() == 3);  // 红杀命中
}

TEST_CASE("game: qinggang pierces renwang")
{
    TestGame g("deck");
    g.add_player("a", 0, 4);
    auto *b = g.add_player("b", 1, 4);
    g.equip("a", "qinggang", "e#0");
    g.equip("b", "renwang", "e#1");
    g.give("a", "sha", "s#1");  // 黑杀

    TestDecider decider;
    const auto played = g.cards.hand("a")[0];
    auto r = resolve_play(g.ctx, decider, "a", played, {"b"});
    REQUIRE(r.is_ok());
    CHECK(b->get_hp() == 3);  // 青釭剑无视防具
}

TEST_CASE("game: bagua red judge counts as jink")
{
    TestGame g("deck");
    g.add_player("a", 0, 4);
    auto *b = g.add_player("b", 1, 4);
    g.equip("b", "bagua", "e#0");
    g.give("a", "sha", "s#1");
    g.cards.add_to_draw(Card{"j#0", "sha", Suit::Heart, 3});  // 判定：红桃

    TestDecider decider;
    const auto played = g.cards.hand("a")[0];
    auto r = resolve_play(g.ctx, decider, "a", played, {"b"});
    REQUIRE(r.is_ok());
    CHECK(b->get_hp() == 4);  // 红判定视为闪，未受伤
}

TEST_CASE("game: bagua black judge does not dodge")
{
    TestGame g("deck");
    g.add_player("a", 0, 4);
    auto *b = g.add_player("b", 1, 4);
    g.equip("b", "bagua", "e#0");
    g.give("a", "sha", "s#1");
    g.cards.add_to_draw(Card{"j#0", "sha", Suit::Spade, 6});  // 判定：黑桃

    TestDecider decider;  // 不打闪
    const auto played = g.cards.hand("a")[0];
    auto r = resolve_play(g.ctx, decider, "a", played, {"b"});
    REQUIRE(r.is_ok());
    CHECK(b->get_hp() == 3);  // 黑判定无效，又无闪 → 受伤
}

TEST_CASE("game: guanshi discards two cards to force the sha")
{
    TestGame g("deck");
    g.add_player("a", 0, 4);
    auto *b = g.add_player("b", 1, 4);
    g.equip("a", "guanshi", "e#0");
    g.give("a", "sha", "s#1");
    g.give("a", "sha", "s#2");
    g.give("a", "shan", "s#3");
    g.give("b", "shan", "s#4");

    TestDecider decider;
    decider.respond = true;    // 目标打出闪
    decider.triggers = {CardEffectKind::DiscardTwoForceDamage};
    const auto played = g.cards.hand("a")[0];  // 杀
    auto r = resolve_play(g.ctx, decider, "a", played, {"b"});
    REQUIRE(r.is_ok());
    CHECK(b->get_hp() == 3);  // 贯石斧令杀依然命中
    CHECK(g.cards.hand_size("a") == 0);  // 弃了两张手牌
}

TEST_CASE("game: qilin discards target horse after damage")
{
    TestGame g("deck");
    g.add_player("a", 0, 4);
    auto *b = g.add_player("b", 1, 4);
    g.equip("a", "qilin", "e#0");
    g.equip("b", "chitu", "e#1");
    g.give("a", "sha", "s#1");

    TestDecider decider;
    decider.triggers = {CardEffectKind::DiscardHorseOnDamage};
    const auto played = g.cards.hand("a")[0];
    auto r = resolve_play(g.ctx, decider, "a", played, {"b"});
    REQUIRE(r.is_ok());
    CHECK(b->get_hp() == 3);              // 命中
    CHECK(g.cards.equip_size("b") == 0);  // 坐骑被弃
}

TEST_CASE("game: hanbing converts damage into discarding two cards")
{
    TestGame g("deck");
    g.add_player("a", 0, 4);
    auto *b = g.add_player("b", 1, 4);
    g.equip("a", "hanbing", "e#0");
    g.give("a", "sha", "s#1");
    g.give("b", "sha", "s#2");
    g.give("b", "shan", "s#3");

    TestDecider decider;
    decider.triggers = {CardEffectKind::DamageAsDiscard};
    const auto played = g.cards.hand("a")[0];
    auto r = resolve_play(g.ctx, decider, "a", played, {"b"});
    REQUIRE(r.is_ok());
    CHECK(b->get_hp() == 4);         // 防止了伤害
    CHECK(g.cards.hand_size("b") == 0);  // 改为弃两张牌
}

TEST_CASE("game: qinglong follows up with another sha after jink")
{
    TestGame g("deck");
    g.add_player("a", 0, 4);
    auto *b = g.add_player("b", 1, 4);
    g.equip("a", "qinglong", "e#0");
    g.give("a", "sha", "s#1");
    g.give("a", "sha", "s#2");
    g.give("b", "shan", "s#3");  // 目标只有一张闪

    TestDecider decider;
    decider.respond = true;   // 第一刀被闪，第二刀无闪可出
    decider.triggers = {CardEffectKind::ExtraShaAfterJink};
    const auto played = g.cards.hand("a")[0];
    auto r = resolve_play(g.ctx, decider, "a", played, {"b"});
    REQUIRE(r.is_ok());
    CHECK(b->get_hp() == 3);            // 续杀命中
    CHECK(g.cards.hand_size("a") == 0); // 两张杀都打出去了
    CHECK(g.cards.hand_size("b") == 0); // 闪已消耗
}