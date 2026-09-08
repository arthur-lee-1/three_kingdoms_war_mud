/**
 * @file table.hpp
 * @brief 对局运行时（应用层）：拥有事件总线/实体/牌/目录/随机源，产出各域
 *        指针保证非空的 GameContext。
 * @note 构造顺序 bus → entities(绑定 bus) → cards → catalog → rng；
 *       Game 不可拷贝/移动（EntityManager 须原地锚定）。
 */

#ifndef INCLUDE_TKW_GAME_TABLE_HPP
#define INCLUDE_TKW_GAME_TABLE_HPP

#include <memory>
#include <string>
#include <utility>

#include "card/catalog.hpp"
#include "card/manager.hpp"
#include "entity/hp.hpp"
#include "entity/manager.hpp"
#include "event/event_bus.hpp"
#include "game/context.hpp"
#include "util/rng.hpp"

namespace tkw
{
    namespace game
    {
        /**
         * @class Game
         * @brief 一局对战的运行时容器（各域成员公开，供应用/测试直接取用）。
         */
        class Game
        {
        public:
            Game(card::CardDefCatalog in_catalog, std::unique_ptr<Rng> in_rng) :
                catalog(std::move(in_catalog)), rng(std::move(in_rng))
            {
            }

            Game(const Game &) = delete;
            Game &operator=(const Game &) = delete;
            Game(Game &&) = delete;
            Game &operator=(Game &&) = delete;

            /** @brief 注册玩家实体（绑定本局总线）。 */
            entity::EntityResult<entity::Entity *> add_player(
                std::string id, int seat, entity::Hp hp)
            {
                return entities.create(std::move(id), seat, std::move(hp));
            }

            /**
             * @brief 构造 GameContext：bus/entities/cards/catalog 保证非空，
             *        rng 可空（空 = 不洗牌，供确定性测试）。
             */
            GameContext context()
            {
                GameContext ctx;
                ctx.bus = &bus;
                ctx.entities = &entities;
                ctx.cards = &cards;
                ctx.catalog = &catalog;
                ctx.rng = rng.get();
                return ctx;
            }

            EventBus bus;                          /**< 本局事件总线 */
            EntityManager entities{bus};           /**< 实体容器（绑定本局总线） */
            card::CardManager cards;               /**< 卡牌容器 */
            card::CardDefCatalog catalog;          /**< 本局卡牌目录 */
            std::unique_ptr<Rng> rng;              /**< 本局随机源 */
        };
    }
}

#endif  // INCLUDE_TKW_GAME_TABLE_HPP
