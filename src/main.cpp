/**
 * @file main.cpp
 * @brief CLI 入口：加载牌堆 → 建 Game → 简单贪心 AI 跑完整对局 → 打印胜负。
 */

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>

#include "card/catalog.hpp"
#include "config/error.hpp"
#include "config/resource.hpp"
#include "entity/hp.hpp"
#include "game/ai/simple.hpp"
#include "game/loop.hpp"
#include "game/table.hpp"
#include "util/rng.hpp"

namespace
{
    struct Options
    {
        std::string deck_dir = "resources";
        int players = 4;
        int hand = 4;
        std::uint32_t seed = 42;
    };

    void print_usage()
    {
        std::cout << "用法: tkw [选项]\n"
                  << "  --deck <目录>   资源目录（含 deck.json 与 cards/，默认 resources）\n"
                  << "  --players <n>   玩家数（>= 2，默认 4）\n"
                  << "  --hand <n>      初始手牌数（默认 4）\n"
                  << "  --seed <n>      随机种子（默认 42）\n"
                  << "  -h, --help      显示本帮助\n";
    }

    /** 解析参数；--help 直接退出 0，非法输入直接退出 2。 */
    void parse_args(int argc, char **argv, Options &opt)
    {
        for (int i = 1; i < argc; ++i)
        {
            const std::string arg = argv[i];
            auto value = [&](const char *name) -> const char *
            {
                if (i + 1 >= argc)
                {
                    std::cerr << "选项 " << name << " 缺少参数\n";
                    std::exit(2);
                }
                return argv[++i];
            };

            if (arg == "--deck")
                opt.deck_dir = value("--deck");
            else if (arg == "--players")
                opt.players = std::stoi(value("--players"));
            else if (arg == "--hand")
                opt.hand = std::stoi(value("--hand"));
            else if (arg == "--seed")
                opt.seed = static_cast<std::uint32_t>(std::stoul(value("--seed")));
            else if (arg == "-h" || arg == "--help")
            {
                print_usage();
                std::exit(0);
            }
            else
            {
                std::cerr << "未知选项: " << arg << "\n";
                print_usage();
                std::exit(2);
            }
        }

        if (opt.players < 2 || opt.hand < 0)
        {
            std::cerr << "参数非法：--players 须 >= 2，--hand 须 >= 0\n";
            std::exit(2);
        }
    }
}

int main(int argc, char **argv)
{
    try
    {
        Options opt;
        parse_args(argc, argv, opt);

        tkw::config::ResourceStore store(opt.deck_dir);
        auto catalog = tkw::card::CardDefCatalog::load(store, "deck");
        if (catalog.is_err())
        {
            const auto &e = catalog.unwrap_err();
            std::cerr << "加载牌堆失败 (kind=" << static_cast<int>(e.kind)
                      << "): " << e.detail << "\n";
            return 1;
        }

        tkw::game::Game game(
            std::move(catalog).unwrap(),
            std::make_unique<tkw::SeededRng>(opt.seed));

        for (int i = 0; i < opt.players; ++i)
        {
            auto r =
                game.add_player("P" + std::to_string(i), i, tkw::entity::Hp::make(4));
            if (r.is_err())
            {
                std::cerr << "创建玩家失败: P" << i << "\n";
                return 1;
            }
        }

        auto ctx = game.context();
        tkw::game::SimpleAI ai;
        auto outcome = tkw::game::play_game(ctx, ai, "P0", opt.hand);
        if (outcome.is_err())
        {
            const auto err = outcome.unwrap_err();
            if (err == tkw::game::LoopError::MaxRounds)
            {
                std::cout << "平局（达到最大回合数）\n";
                return 0;
            }
            std::cerr << "对局失败 (LoopError=" << static_cast<int>(err) << ")\n";
            return 1;
        }

        const auto &out = outcome.unwrap();
        std::cout << "胜者: " << out.winner << "，回合数: " << out.rounds << "\n";
        return 0;
    }
    catch (const std::exception &e)
    {
        std::cerr << "错误: " << e.what() << "\n";
        return 2;
    }
}
