/**
 * @file hp.hpp
 * @brief 血条模型：体力 cur / 上限 max。
 * @note 扣到非正是**濒死值状态**，合法且可继续被桃拉回；死亡不是
 *      状态值问题，是 combat 的流程判定。
 */

#ifndef INCLUDE_TKW_ENTITY_HP_HPP
#define INCLUDE_TKW_ENTITY_HP_HPP

#include <algorithm>
#include <functional>

#include "util/marco.hpp"

namespace tkw
{
    namespace entity
    {
        /**
         * @class Hp
         * @brief 血条（体力 / 体力上限）。所有修改路径收敛到 set_cur/set_max，
         *        值真正变化时触发 on_change 回调（无变化不触发）。
         *        add 受上限钳制；sub 无下限——扣到非正即濒死值状态。
         *        add/sub 返回实际变化量（受钳制，可能小于入参或为 0）。
         */
        class Hp
        {
        private:
            int cur_ = 0;
            int max_ = 0;
            std::function<void(int, int, int)> m_on_change;  // (old_cur, cur, max)

        public:
            DEFAULT_CONSTRUCTOR(Hp)

            /** @brief 初始满血：cur = max = initial。 */
            static Hp make(int initial)
            {
                Hp hp;
                hp.cur_ = initial;
                hp.max_ = initial;
                return hp;
            }

            int get_cur() const noexcept { return cur_; }
            int get_max() const noexcept { return max_; }

            /**
             * @brief 直接设置体力：v > max_ 拒绝；v < 0 合法（濒死值状态）；
             *       与当前值相同不触发回调。
             */
            bool set_cur(int val)
            {
                if (val > max_)
                    return false;
                if (val == cur_)
                    return true;
                const int old = cur_;
                cur_ = val;
                if (m_on_change)
                    m_on_change(old, cur_, max_);
                return true;
            }

            /**
             * @brief 变更体力上限：val < 0 拒绝；cur_ 超过新上限时夹紧 cur_
             *       并仅就 cur_ 的变化触发一次回调（上限自身变化不触发）。
             */
            bool set_max(int val)
            {
                if (val < 0)
                    return false;
                if (val == max_)
                    return true;
                max_ = val;
                const int old_cur = cur_;
                if (cur_ > max_)
                    cur_ = max_;
                if (cur_ != old_cur && m_on_change)
                    m_on_change(old_cur, cur_, max_);
                return true;
            }

            int add(int det)
            {
                const int old = cur_;
                set_cur(std::min(cur_ + det, max_));
                return cur_ - old;
            }

            int sub(int det)
            {
                const int old = cur_;
                set_cur(cur_ - det);
                return old - cur_;
            }

            void on_change(std::function<void(int, int, int)> cb)
            {
                m_on_change = std::move(cb);
            }
        };
    }
}

#endif  // INCLUDE_TKW_ENTITY_HP_HPP
