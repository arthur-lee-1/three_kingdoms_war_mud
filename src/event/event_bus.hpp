#ifndef INCLUDE_TKW_EVENT_BUS_HPP
#define INCLUDE_TKW_EVENT_BUS_HPP

#include <algorithm>
#include <cstdint>
#include <functional>
#include <memory>
#include <type_traits>
#include <typeindex>
#include <unordered_map>
#include <vector>

#include "event/event.hpp"
#include "event/handler.hpp"
#include "util/marco.hpp"

namespace tkw
{
    /**
     * @class CommonEventBus
     * @brief 按事件类型的精确分发总线：接收 Handler（filter + priority + action），
     *        publish 时按 (priority, 注册顺序) 依次执行，并支持停止传播。
     * @note subscribe 返回 RAII 句柄：句柄析构/reset 即退订。
     *       推荐「一场战斗一个总线」——会话结束时整组句柄随之销毁，
     *       handler 全部自动卸载（如战斗内 debuff 的监听）。
     */
    template <typename T>
    class CommonEventBus
    {
    public:
        using Key = std::type_index;

        /**
         * @class Handle
         * @brief 订阅句柄（RAII）：析构或 reset() 即从总线退订；可移动，不可拷贝。
         */
        class Handle
        {
            friend class CommonEventBus;

        public:
            Handle() = default;
            Handle(Handle &&o) noexcept { *this = std::move(o); }
            Handle &operator=(Handle &&o) noexcept
            {
                if (this != &o)
                {
                    reset();
                    m_bus = o.m_bus;
                    m_key = o.m_key;
                    m_seq = o.m_seq;
                    o.m_bus = nullptr;  // 移交所有权：源退化为未注册态，析构不再退订
                }
                return *this;
            }
            Handle(const Handle &) = delete;
            Handle &operator=(const Handle &) = delete;
            ~Handle() { reset(); }

            void reset() noexcept
            {
                if (m_bus == nullptr)
                    return;
                m_bus->drop(m_key, m_seq);
                m_bus = nullptr;
                m_seq = 0;
            }

            bool active() const noexcept { return m_bus != nullptr; }

        private:
            Handle(CommonEventBus *bus, Key key, std::uint64_t seq) :
                m_bus(bus), m_key(key), m_seq(seq) {}

            CommonEventBus *m_bus = nullptr;
            Key m_key{typeid(void)};
            std::uint64_t m_seq = 0;
        };

    private:
        /** @brief 已类型擦除的 handler 槽位。 */
        struct Slot
        {
            int priority = 100;
            std::uint64_t seq = 0;
            std::function<bool(const T &)> match;  // 空 = 关心全部
            std::function<bool(T &)> invoke;       // 返回 false = 停止传播
        };

        using ListenerTable = std::unordered_map<Key, std::vector<Slot>>;

        ListenerTable listeners;
        std::uint64_t m_sequence = 0;  /**< 已分配的最大事件序号 */
        std::uint64_t m_next_slot = 0; /**< 注册序号（同优先级的稳定次序） */

        void drop(Key key, std::uint64_t seq)
        {
            auto it = listeners.find(key);
            if (it == listeners.end())
                return;
            auto &slots = it->second;
            slots.erase(
                std::remove_if(slots.begin(), slots.end(),
                                [seq](const Slot &slot) { return slot.seq == seq; }),
                slots.end());
            if (slots.empty())
                listeners.erase(it);
        }

    public:
        DEFAULT_CONSTRUCTOR(CommonEventBus)

    public:
        /** @brief 当前已分配的最大事件序号。 */
        std::uint64_t sequence() const noexcept { return m_sequence; }

        /**
         * @brief 注册一个 handler。
         * @tparam U 事件类型（须派生自 T）。
         * @return 订阅句柄：持有期间生效，析构/reset 即退订。
         */
        template <typename U>
            requires(std::is_base_of_v<T, U>)
        Handle subscribe(Handler<U> handler)
        {
            const Key key{typeid(U)};
            const std::uint64_t seq = ++m_next_slot;
            Slot slot;
            slot.priority = static_cast<int>(handler.priority());
            slot.seq = seq;
            if (handler.has_filter())
                slot.match = [h = handler](const T &base)
                { return h.matches(static_cast<const U &>(base)); };
            slot.invoke = [h = std::move(handler)](T &base)
            {
                HandlerContext<U> ctx{static_cast<U &>(base)};
                h.invoke(ctx);
                return !ctx.stop;
            };
            listeners[key].push_back(std::move(slot));
            return Handle(this, key, seq);
        }

        /**
         * @brief 发布事件：分配全局序号后，按 (priority, 注册顺序) 执行全部命中 handler。
         * @note 分发基于当前监听表快照：分发期间的新注册不影响本轮，退订对本轮已排队的
         *       handler 不再生效（快照语义）。handler 内可再次 publish（如荆棘反弹）。
         */
        template <typename U>
            requires(
                std::is_base_of_v<T, U> &&
                requires(T &t) { t.set_sequence(std::uint64_t{}); })
        void publish(const std::shared_ptr<U> &event)
        {
            event->set_sequence(++m_sequence);
            auto it = listeners.find(typeid(U));
            if (it == listeners.end())
                return;
            auto slots = it->second;
            std::stable_sort(slots.begin(), slots.end(),
                             [](const Slot &a, const Slot &b) { return a.priority < b.priority; });
            for (auto &slot : slots)
            {
                if (slot.match && !slot.match(*event))
                    continue;
                if (!slot.invoke(*event))
                    break;
            }
        }
    };

    using EventBus = CommonEventBus<Event>;
}

#endif  // INCLUDE_TKW_EVENT_BUS_HPP
