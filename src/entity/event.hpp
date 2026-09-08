#ifndef INCLUDE_TKW_ENTITY_EVENT_HPP
#define INCLUDE_TKW_ENTITY_EVENT_HPP

#include <string>

#include "event/event.hpp"
#include "event/marco.hpp"

namespace tkw
{
    /**
     * @class EntityEvent
     * @brief 实体域事件基类：描述实体自身状态/身份的变化，与具体战斗流程解耦。
     * @note 事件只携带实体 id；敌我关系等会话角色由 combat 域按 id 自行维护。
     */
    DEFINE_EVENT_START(Entity, Event)
    DEFINE_EVENT_END(Entity)

    /**
     * @class EntityHpChangedEvent
     * @brief 实体体力 cur 值发生变化，由状态监听器自动发布。
     */
    DEFINE_EVENT_START(EntityHpChanged, EntityEvent)
public:
    std::string entity_id;
    int old_cur = 0;
    int new_cur = 0;
    int max = 0;
    DEFINE_EVENT_END(EntityHpChanged)

    /**
     * @class EntityDamagedEvent
     * @brief 实体受到伤害（原因层：来源、伤害量、是否间接伤害）。
     * @note indirect = 连环传导等间接伤害；铁索传播判定与日志都看这个标志。
     */
    DEFINE_EVENT_START(EntityDamaged, EntityEvent)
public:
    std::string source;
    std::string target;
    int amount = 0;
    bool indirect = false;
    DEFINE_EVENT_END(EntityDamaged)

    /**
     * @class EntityHealedEvent
     * @brief 实体恢复 hp（原因层，amount 为实际恢复量；桃自濒死拉回同样走这里）。
     */
    DEFINE_EVENT_START(EntityHealed, EntityEvent)
public:
    std::string target;
    int amount = 0;
    DEFINE_EVENT_END(EntityHealed)

    /**
     * @class EntityDyingEvent
     * @brief 濒死判定轮：target 当前 hp 非正，进入一轮救场判定。
     * @note 由 **combat** 在救场窗口每轮发布（桃一次仍非正 → 再发一轮）；
     *       Entity 自身不发布本事件。
     */
    DEFINE_EVENT_START(EntityDying, EntityEvent)
public:
    std::string target;
    int current_hp = 0;
    DEFINE_EVENT_END(EntityDying)

    /**
     * @class EntityDiedEvent
     * @brief 实体死亡（救场窗口关闭、无人救回）。
     * @note 由 **combat** 发布；Entity 自身**不**发布本事件——
     *       hp 非正是濒死值状态，死亡声明属于流程而非状态。
     */
    DEFINE_EVENT_START(EntityDied, EntityEvent)
public:
    std::string entity_id;
    DEFINE_EVENT_END(EntityDied)
}

#endif  // INCLUDE_TKW_ENTITY_EVENT_HPP
