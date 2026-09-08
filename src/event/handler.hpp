#ifndef INCLUDE_TKW_EVENT_HANDLER_HPP
#define INCLUDE_TKW_EVENT_HANDLER_HPP

#include <functional>
#include <memory>
#include <utility>
#include <vector>

namespace tkw
{
    /**
     * @brief 处理优先级：数值小者先执行；同优先级按注册顺序。
     */
    enum class HandlerPriority : int
    {
        First  = 0,
        Normal = 100,
        Last   = 200,
    };

    /**
     * @struct HandlerContext
     * @brief 交给 handler action 的上下文：事件本体 + 传播开关。
     * @note action 置 stop 后，同一次 publish 内排在后面的 handler 不再执行。
     */
    template <typename EventT>
    struct HandlerContext
    {
        EventT &event;
        bool stop = false;
    };

    /**
     * @class Handler
     * @brief 针对某事件类型的最小行为单元：filter 回答「是否关心」，
     *        action 只做一件事，priority 决定执行先后。
     * @note 不依赖总线即可直接构造/调用/断言，便于独立单测；
     *        用 pipe / fanout / when 组合成更复杂的行为。
     */
    template <typename EventT>
    class Handler
    {
    public:
        using Event  = EventT;
        using Filter = std::function<bool(const EventT &)>;
        using Action = std::function<void(HandlerContext<EventT> &)>;

        Handler() = default;
        explicit Handler(Action action, HandlerPriority priority = HandlerPriority::Normal) :
            m_action(std::move(action)), m_priority(priority) {}
        Handler(Filter filter, Action action, HandlerPriority priority = HandlerPriority::Normal) :
            m_filter(std::move(filter)), m_action(std::move(action)), m_priority(priority) {}

        bool has_filter() const noexcept { return static_cast<bool>(m_filter); }
        bool matches(const EventT &event) const { return !m_filter || m_filter(event); }
        void invoke(HandlerContext<EventT> &ctx) const { m_action(ctx); }
        HandlerPriority priority() const noexcept { return m_priority; }

    private:
        Filter m_filter;  // 空 = 关心所有该类型事件
        Action m_action;
        HandlerPriority m_priority = HandlerPriority::Normal;
    };

    /**
     * @brief 管线组合：按序执行各阶段，任一阶段置 stop 即短路。
     * @note 用于「计算 → 校验 → 结算」这类后段依赖前段的阶段链。
     */
    template <typename EventT, typename... Rest>
    Handler<EventT> pipe(Handler<EventT> first, Rest &&...rest)
    {
        std::vector<Handler<EventT>> stages{std::move(first), std::forward<Rest>(rest)...};
        return Handler<EventT>(
            [stages = std::move(stages)](HandlerContext<EventT> &ctx) mutable
            {
                for (auto &stage : stages)
                {
                    if (!stage.matches(ctx.event))
                        continue;
                    stage.invoke(ctx);
                    if (ctx.stop)
                        break;
                }
            });
    }

    /**
     * @brief 分发组合：所有 handler 各自独立执行，互不影响，不看 stop。
     * @note 用于「受伤 → (记日志, 荆棘反弹, 连击计数)」这类独立关注点。
     */
    template <typename EventT, typename... Rest>
    Handler<EventT> fanout(Handler<EventT> first, Rest &&...rest)
    {
        std::vector<Handler<EventT>> handlers{std::move(first), std::forward<Rest>(rest)...};
        return Handler<EventT>(
            [handlers = std::move(handlers)](HandlerContext<EventT> &ctx) mutable
            {
                for (auto &handler : handlers)
                    if (handler.matches(ctx.event))
                        handler.invoke(ctx);
            });
    }

    /**
     * @brief 条件组合：仅当 pred(event) 为真时才委托给 h（与 h 自带 filter 叠加）。
     */
    template <typename EventT, typename Pred>
    Handler<EventT> when(Pred &&pred, Handler<EventT> h)
    {
        auto outer = typename Handler<EventT>::Filter(std::forward<Pred>(pred));
        auto inner = std::make_shared<Handler<EventT>>(std::move(h));
        return Handler<EventT>(
            [outer, inner](const EventT &event) { return outer(event) && inner->matches(event); },
            [inner](HandlerContext<EventT> &ctx) { inner->invoke(ctx); },
            inner->priority());
    }
}

#endif  // INCLUDE_TKW_EVENT_HANDLER_HPP
