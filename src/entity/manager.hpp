#ifndef INCLUDE_TKW_ENTITY_MANAGER_HPP
#define INCLUDE_TKW_ENTITY_MANAGER_HPP

#include <cstddef>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "entity/base.hpp"
#include "entity/error.hpp"
#include "entity/hp.hpp"
#include "event/event_bus.hpp"
#include "util/types.hpp"

namespace tkw
{
    /**
     * @class EntityManager
     * @brief 对局作用域的实体容器 + id 索引。总线由拥有它的上下文注入
     *        （须比本管理器存活更久），create 出的实体绑定该总线。
     *
     * 创建顺序 = 确定性迭代顺序（座位/回合序）；find 为 O(1)。
     * 不关心敌我关系与死亡规则：死实体由调用方在安静时刻（如回合结算后）
     * 显式 remove，避免事件分发中途改动容器。
     *
     * @note 不可拷贝、不可移动：须原地锚定在对局运行时内。
     */
    class EntityManager
    {
    public:
        explicit EntityManager(EventBus &injected_bus) : bus(&injected_bus) {}

        EntityManager(const EntityManager &) = delete;
        EntityManager &operator=(const EntityManager &) = delete;
        EntityManager(EntityManager &&) = delete;
        EntityManager &operator=(EntityManager &&) = delete;

        /**
         * @brief 注册新玩家实体（绑定到注入总线）。
         * @param id 实体 id，对局内唯一。
         * @param seat 座位号（距离计算与回合序的基础）。
         * @param hp 初始血条（体力/上限）。
         * @return Ok 时为实体指针（与 find 同稳定性：未 remove 前有效）；
         *         Err 时为 EntityError::DuplicateId。
         */
        entity::EntityResult<entity::Entity *> create(
            std::string id, int seat, entity::Hp hp)
        {
            if (contains(id))
                return entity::EntityResult<entity::Entity *>::Err(
                    entity::EntityError::DuplicateId);
            const std::size_t at = entities.size();
            entities.push_back(
                std::make_unique<entity::Entity>(
                    std::move(id), seat, std::move(hp), *bus));
            index.emplace(entities[at]->get_id(), at);
            return entity::EntityResult<entity::Entity *>::Ok(entities[at].get());
        }

        /** @brief O(1) 按 id 查询；不存在时为 None。 */
        Option<entity::Entity *> find(const std::string &id) const
        {
            auto it = index.find(id);
            if (it == index.end())
                return Option<entity::Entity *>::None();
            return Option<entity::Entity *>::Some(entities[it->second].get());
        }

        bool contains(const std::string &id) const
        {
            return index.find(id) != index.end();
        }

        /**
         * @brief 按 id 移除（不存在时幂等无操作）。
         * @note 其余实体的 Entity* 不受影响（堆对象不移动）。
         */
        void remove(const std::string &id)
        {
            auto it = index.find(id);
            if (it == index.end())
                return;
            entities.erase(entities.begin() + std::ptrdiff_t(it->second));
            rebuild_index();
        }

        std::size_t size() const noexcept { return entities.size(); }
        bool empty() const noexcept { return entities.empty(); }

        /** @brief 按创建序迭代（即座位回合序）。 */
        auto begin() noexcept { return entities.begin(); }
        auto end() noexcept { return entities.end(); }
        auto begin() const noexcept { return entities.begin(); }
        auto end() const noexcept { return entities.end(); }

    private:
        EventBus *bus;
        std::vector<std::unique_ptr<entity::Entity>> entities;
        std::unordered_map<std::string, std::size_t> index;

        void rebuild_index()
        {
            index.clear();
            for (std::size_t i = 0; i < entities.size(); ++i)
                index.emplace(entities[i]->get_id(), i);
        }
    };
}

#endif  // INCLUDE_TKW_ENTITY_MANAGER_HPP
