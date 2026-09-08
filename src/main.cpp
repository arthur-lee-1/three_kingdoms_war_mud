/**
 * @file main.cpp
 * @brief CLI 入口（pjh_cli）：根命令跑一局，audit 审计牌堆，deal 位置参数跑局，
 *        repl 进入交互模式。
 */

#include <cstdint>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include <pjh_cli.hpp>

#include "card/catalog.hpp"
#include "config/error.hpp"
#include "config/resource.hpp"
#include "entity/event.hpp"
#include "entity/hp.hpp"
#include "event/handler.hpp"
#include "game/ai/simple.hpp"
#include "game/audit.hpp"
#include "game/card_event.hpp"
#include "game/loop.hpp"
#include "game/table.hpp"
#include "util/rng.hpp"

namespace
{
    using pjh::cli::App;
    using pjh::cli::CliError;
    using pjh::cli::CliFailure;
    using pjh::cli::CliResult;
    using pjh::cli::ExtraArgsPolicy;
    using pjh::cli::fixed_string;
    using pjh::cli::InteractiveConsole;
    using pjh::cli::ParseContext;
    using pjh::cli::Visibility;

    struct Options
    {
        std::filesystem::path deck = "resources";
        int players = 4;
        int hand = 4;
        std::uint32_t seed = 42;
        bool verbose = false;
    };

    /** 从解析上下文读参数（子命令经父链继承根命令的选项）。 */
    Options options_from(ParseContext &ctx)
    {
        Options opt;
        opt.deck = ctx.get_or<std::filesystem::path, fixed_string("deck")>(
            std::filesystem::path("resources"));
        opt.players = ctx.get_or<int, fixed_string("players")>(4);
        opt.hand = ctx.get_or<int, fixed_string("hand")>(4);
        opt.seed =
            static_cast<std::uint32_t>(ctx.get_or<int, fixed_string("seed")>(42));
        opt.verbose = ctx.get_or<bool, fixed_string("verbose")>(false);
        return opt;
    }

    CliResult<void> run_game(const Options &opt)
    {
        tkw::config::ResourceStore store(opt.deck);
        auto catalog = tkw::card::CardDefCatalog::load(store, "deck");
        if (catalog.is_err())
        {
            const auto &e = catalog.unwrap_err();
            return CliFailure{CliError(
                "加载牌堆失败 (kind=" + std::to_string(static_cast<int>(e.kind)) +
                "): " + e.detail)};
        }

        tkw::game::Game game(
            std::move(catalog).unwrap(),
            std::make_unique<tkw::SeededRng>(opt.seed));

        const auto unsupported = tkw::game::unsupported_cards(game.catalog);
        if (!unsupported.empty())
        {
            std::cerr << "警告: 牌堆含 " << unsupported.size()
                      << " 张引擎未实现的卡:";
            for (const auto &id : unsupported)
                std::cerr << ' ' << id;
            std::cerr << "\n";
        }

        for (int i = 0; i < opt.players; ++i)
        {
            auto r =
                game.add_player("P" + std::to_string(i), i, tkw::entity::Hp::make(4));
            if (r.is_err())
                return CliFailure{CliError("创建玩家失败: P" + std::to_string(i))};
        }

        auto ctx = game.context();

        // --verbose：直接订阅本局总线，打印卡牌/死亡事件
        std::vector<tkw::EventBus::Handle> log_handles;
        if (opt.verbose)
        {
            log_handles.push_back(game.bus.subscribe(tkw::Handler<tkw::CardPlayedEvent>(
                [](tkw::HandlerContext<tkw::CardPlayedEvent> &c)
                { std::cout << "[打出] " << c.event.user << " " << c.event.def_id << "\n"; })));
            log_handles.push_back(
                game.bus.subscribe(tkw::Handler<tkw::CardDiscardedEvent>(
                    [](tkw::HandlerContext<tkw::CardDiscardedEvent> &c) {
                        std::cout << "[弃置] " << c.event.entity << " " << c.event.def_id
                                  << "\n";
                    })));
            log_handles.push_back(game.bus.subscribe(tkw::Handler<tkw::CardDrawnEvent>(
                [](tkw::HandlerContext<tkw::CardDrawnEvent> &c)
                { std::cout << "[摸牌] " << c.event.entity << " " << c.event.def_id << "\n"; })));
            log_handles.push_back(game.bus.subscribe(tkw::Handler<tkw::EntityDiedEvent>(
                [](tkw::HandlerContext<tkw::EntityDiedEvent> &c)
                { std::cout << "[阵亡] " << c.event.entity_id << "\n"; })));
        }

        tkw::game::SimpleAI ai;
        auto outcome = tkw::game::play_game(ctx, ai, "P0", opt.hand);
        if (outcome.is_err())
        {
            if (outcome.unwrap_err() == tkw::game::LoopError::MaxRounds)
            {
                std::cout << "平局（达到最大回合数）\n";
                return CliResult<void>::Ok();
            }
            return CliFailure{CliError(
                "对局失败 (LoopError=" +
                std::to_string(static_cast<int>(outcome.unwrap_err())) + ")")};
        }

        const auto &out = outcome.unwrap();
        std::cout << "胜者: " << out.winner << "，回合数: " << out.rounds << "\n";
        return CliResult<void>::Ok();
    }

    CliResult<void> audit_deck(const Options &opt)
    {
        tkw::config::ResourceStore store(opt.deck);
        auto catalog = tkw::card::CardDefCatalog::load(store, "deck");
        if (catalog.is_err())
            return CliFailure{CliError("加载牌堆失败: " + catalog.unwrap_err().detail)};

        const auto unsupported = tkw::game::unsupported_cards(catalog.unwrap());
        if (unsupported.empty())
        {
            std::cout << "牌堆全部可结算\n";
            return CliResult<void>::Ok();
        }
        std::cout << "未实现卡（" << unsupported.size() << " 张）:\n";
        for (const auto &id : unsupported)
            std::cout << "  " << id << "\n";
        return CliResult<void>::Ok();
    }
}

int main(int argc, char **argv)
{
    App app("tkw", "0.1.0", "三国杀式卡牌对局引擎");
    app.set_extra_args(ExtraArgsPolicy::Error);  // 未知命令/多余参数即报错

    // 根命令选项（无子命令时直接跑一局，兼容旧用法）
    app.option<fixed_string("deck")>(
        "--deck", 'd', "资源目录（含 deck.json 与 cards/）",
        std::filesystem::path("resources"));
    app.option<fixed_string("players")>("--players", 'p', "玩家数（2-8）")
        .integer()
        .min(2)
        .max(8)
        .default_value(4);
    app.option<fixed_string("hand")>("--hand", "初始手牌数")
        .integer()
        .min(0)
        .max(20)
        .default_value(4);
    app.option<fixed_string("seed")>("--seed", 's', "随机种子")
        .integer()
        .min(0)
        .default_value(42);
    app.option<fixed_string("verbose")>("--verbose", 'v', "打印卡牌/死亡事件日志")
        .boolean();

    app.action([](ParseContext &ctx) -> CliResult<void>
               { return run_game(options_from(ctx)); });

    // audit：审计牌堆
    auto &audit = app.add_leaf("audit", "审计牌堆，列出引擎未实现的卡");
    audit.option<fixed_string("deck")>(
        "--deck", 'd', "资源目录", std::filesystem::path("resources"));
    audit.action([](ParseContext &ctx) -> CliResult<void>
                 { return audit_deck(options_from(ctx)); });

    // deal：位置参数跑局（REPL/批量通用，避免父选项位置限制）
    auto &deal = app.add_leaf("deal", "跑一局：deal <玩家数> <种子>");
    deal.arg<int, 0>("players", "玩家数").required();
    deal.arg<int, 1>("seed", "随机种子").required();
    deal.action(
        [](ParseContext &ctx) -> CliResult<void>
        {
            Options opt = options_from(ctx);
            opt.players = ctx.get<int, 0>();
            opt.seed = static_cast<std::uint32_t>(ctx.get<int, 1>());
            if (opt.players < 2 || opt.players > 8)
                return CliFailure{CliError("玩家数须在 2..8")};
            return run_game(opt);
        });

    // repl：交互模式（对局即 MUD 方向）
    auto &repl = app.add_leaf("repl", "进入交互模式（? 查看命令，quit 退出）");
    repl.set_visibility(Visibility::Cli);
    repl.action(
        [&app](ParseContext &) -> CliResult<void>
        {
            InteractiveConsole console(app, "tkw> ");
            console.run();
            return CliResult<void>::Ok();
        });

    auto parsed = app.parse(argc, argv);
    if (parsed.is_err())
    {
        std::cerr << parsed.unwrap_err().what() << "\n";
        return 2;
    }

    auto &ctx = parsed.unwrap();
    if (ctx.help_requested())
    {
        std::cout << ctx.help_text();
        return 0;
    }
    if (ctx.version_requested())
    {
        std::cout << ctx.version_text();
        return 0;
    }

    auto executed = ctx.matched_command()->execute(ctx);
    if (executed.is_err())
    {
        std::cerr << executed.unwrap_err().what() << "\n";
        return 1;
    }
    return 0;
}
