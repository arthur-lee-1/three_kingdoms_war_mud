/**
 * @file test_game.hpp
 * @brief 共享测试夹具：TestGame（Game + 发牌/装备辅助）与 TestDecider（脚本化决策源）。
 */

#ifndef INCLUDE_TKW_TESTS_GAME_TEST_GAME_HPP
#define INCLUDE_TKW_TESTS_GAME_TEST_GAME_HPP

#include <doctest/doctest.h>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "card/card.hpp"
#include "card/catalog.hpp"
#include "card/def.hpp"
#include "config/resource.hpp"
#include "entity/hp.hpp"
#include "game/decision.hpp"
#include "game/table.hpp"
#include "util/rng.hpp"

namespace tkw
{
    namespace test
    {
        using namespace tkw::game;
        using tkw::Option;
        using tkw::card::Ability;
        using tkw::card::Card;
        using tkw::card::CardDefCatalog;
        using tkw::card::ResponseKind;
        using tkw::entity::Entity;
        using tkw::entity::Hp;

        /** 测试对局：Game 应用层 + 测试辅助（发牌/装备）。 */
        struct TestGame : Game
        {
            GameContext ctx;

            explicit TestGame(const char *deck_name, std::uint32_t seed = 1) :
                Game(load_catalog(deck_name), std::make_unique<tkw::SeededRng>(seed)),
                ctx(context())
            {
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
            std::vector<Ability> triggers;
            std::vector<PlayAction> plays;
            std::size_t play_cursor = 0;

            bool play_response(GameContext &, const std::string &, ResponseKind) override
            {
                return respond;
            }

            bool play_peach(
                GameContext &, const std::string &, const std::string &) override
            {
                return save;
            }

            bool play_counter(GameContext &, const std::string &) override
            {
                return counter;
            }

            bool trigger_effect(
                GameContext &, const std::string &, Ability ability) override
            {
                return std::find(triggers.begin(), triggers.end(), ability) !=
                       triggers.end();
            }

            Option<card::Card> pick_card_from_target(
                GameContext &ctx, const std::string &,
                const std::string &target) override
            {
                const auto &hand = ctx.cards->hand(target);
                if (hand.empty())
                    return Option<card::Card>::None();
                return Option<card::Card>::Some(hand.front());
            }

            Option<PlayAction> choose_play(GameContext &, const std::string &) override
            {
                if (play_cursor >= plays.size())
                    return Option<PlayAction>::None();
                return Option<PlayAction>::Some(plays[play_cursor++]);
            }

            std::vector<std::string> choose_discards(
                GameContext &ctx, const std::string &player, int count,
                DiscardReason) override
            {
                const auto &hand = ctx.cards->hand(player);
                std::vector<std::string> out;
                for (int i = 0; i < count && i < static_cast<int>(hand.size()); ++i)
                    out.push_back(hand[i].instance_id);
                return out;
            }
        };
    }
}

#endif  // INCLUDE_TKW_TESTS_GAME_TEST_GAME_HPP
