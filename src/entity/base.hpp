#ifndef INCLUDE_TKW_ENTITY_BASE_HPP
#define INCLUDE_TKW_ENTITY_BASE_HPP

#include <memory>
#include <string>
#include <utility>

#include "entity/event.hpp"
#include "entity/hp.hpp"
#include "event/event_bus.hpp"

namespace tkw
{
    namespace entity
    {
        /**
         * @class Entity
         * @brief 玩家实体：id + 座位 + 血条（Hp：体力/上限）。构造时自动绑定
         *        体力监听：cur 变化经注入的 EventBus 发布 EntityHpChangedEvent。
         * @note 事件总线须比实体存活更久（实体析构不发布事件，
         *       但存活期间的状态变化都会发布到该总线）。
         * @note 本类是**哑状态持有者**：体力可扣到非正（濒死值状态），
         *       但 Dying / Died 事件都不由本类发布——救场窗口与死亡
         *       声明是 combat 的流程职责。
         */
        class Entity
        {
        private:
            std::string id;
            int seat = 0;
            Hp hp;
            EventBus *bus;

        public:
            Entity(
                std::string eid,
                int in_seat,
                Hp in_hp,
                EventBus &injected_bus) :
                id(std::move(eid)),
                seat(in_seat),
                hp(std::move(in_hp)),
                bus(&injected_bus)
            {
                bind_status_events();
            }

            const std::string &get_id() const noexcept { return id; }
            int get_seat() const noexcept { return seat; }

            /** @brief 当前体力（可为非正 = 濒死值状态）。 */
            int get_hp() const noexcept { return hp.get_cur(); }

            /** @brief 血条对象（含上限与 set_cur/set_max/add/sub 入口）。 */
            Hp &get_hp_bar() noexcept { return hp; }
            const Hp &get_hp_bar() const noexcept { return hp; }

            /**
             * @brief 承受伤害（战斗结算入口）：发布 EntityDamagedEvent（原因层），
             *        再扣体力（状态层自动发布 HpChanged）。
             * @param indirect 是否间接伤害（连环传导等）。
             * @return 实际血量损失（amount <= 0 时为 0）。
             */
            int take_damage(const std::string &source, int amount, bool indirect)
            {
                if (amount <= 0)
                    return 0;
                auto ev = std::make_shared<EntityDamagedEvent>();
                ev->source = source;
                ev->target = id;
                ev->amount = amount;
                ev->indirect = indirect;
                bus->publish(ev);

                return hp.sub(amount);
            }

            /**
             * @brief 恢复 hp（含桃自濒死拉回）：按上限钳制后发布
             *        EntityHealedEvent，amount 记实际恢复量。
             * @note 濒死者体力为负时，每次 +1 后仍可能非正 → combat 进入
             *       下一轮濒死判定；拉回正数即视为救回。
             * @return 实际恢复量（满血或 amount <= 0 时为 0）。
             */
            int heal(int amount)
            {
                if (amount <= 0)
                    return 0;
                const int real = hp.add(amount);
                if (real <= 0)
                    return 0;
                auto ev = std::make_shared<EntityHealedEvent>();
                ev->target = id;
                ev->amount = real;
                bus->publish(ev);
                return real;
            }

        private:
            void bind_status_events()
            {
                const std::string entity_id = id;
                EventBus *bus = this->bus;
                hp.on_change(
                    [entity_id, bus](int old_cur, int cur, int max)
                    {
                        auto ev = std::make_shared<EntityHpChangedEvent>();
                        ev->entity_id = entity_id;
                        ev->old_cur = old_cur;
                        ev->new_cur = cur;
                        ev->max = max;
                        bus->publish(ev);
                        // 不自动发 Died：体力变非正是濒死值状态，
                        // 救场窗口与死亡声明归 combat 流程
                    });
            }
        };
    }
}

#endif  // INCLUDE_TKW_ENTITY_BASE_HPP
