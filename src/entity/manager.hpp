#ifndef INCLUDE_TKW_ENTITY_MANAGER_HPP
#define INCLUDE_TKW_ENTITY_MANAGER_HPP

#include <algorithm>
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
     * 创建顺序 = 容器迭代序；**座位序/回合序**请用 ordered_ids()/next()/
     * order_from()（按 seat 排序，与创建序解耦）；find 为 O(1)。
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

        /**
         * @brief 按座位序返回全部实体 id（同座位按创建序稳定）。
         * @note 座位是回合序与距离的唯一事实源：创建顺序与座位号不一致时，
         *       回合序仍以座位为准。
         */
        std::vector<std::string> ordered_ids() const
        {
            std::vector<std::pair<int, std::string>> tmp;
            tmp.reserve(entities.size());
            for (const auto &e : entities)
                tmp.emplace_back(e->get_seat(), e->get_id());
            std::stable_sort(
                tmp.begin(), tmp.end(),
                [](const auto &a, const auto &b) { return a.first < b.first; });
            std::vector<std::string> ids;
            ids.reserve(tmp.size());
            for (auto &p : tmp)
                ids.push_back(std::move(p.second));
            return ids;
        }

        /** @brief 座位序下家（环绕）；id 不在集合内时返回首个。 */
        std::string next(const std::string &id) const
        {
            const auto ids = ordered_ids();
            if (ids.empty())
                return id;
            auto it = std::find(ids.begin(), ids.end(), id);
            if (it == ids.end())
                return ids.front();
            ++it;
            return it == ids.end() ? ids.front() : *it;
        }

        /** @brief 从 start 起按座位序环绕的 id 列表（start 不在集合内则原序）。 */
        std::vector<std::string> order_from(const std::string &start) const
        {
            auto ids = ordered_ids();
            const auto it = std::find(ids.begin(), ids.end(), start);
            if (it != ids.end())
                std::rotate(ids.begin(), it, ids.end());
            return ids;
        }

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
