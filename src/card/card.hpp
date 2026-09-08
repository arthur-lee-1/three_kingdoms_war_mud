/**
 * @file card.hpp
 * @brief 实体牌（运行时）与牌堆（摸牌堆/弃牌堆）。
 * @note 与 CardDef 的区别：CardDef 是「卡牌定义」（目录里的一张），Card 是
 *       牌堆里的一张实体牌（携带唯一 instance_id + 花色点数）。本文件不含
 *       任何规则/事件：堆满不自动洗回、不发布事件，规则归 gameplay。
 */

#ifndef INCLUDE_TKW_CARD_CARD_HPP
#define INCLUDE_TKW_CARD_CARD_HPP

#include <cstddef>
#include <random>
#include <string>
#include <utility>
#include <vector>

#include "card/def.hpp"
#include "util/types.hpp"

namespace tkw
{
    namespace card
    {
        /**
         * @class Card
         * @brief 实体牌：由牌堆构建按 CardDef.copies 逐张生成。
         * @note 只携带 def_id（引用 CardDefCatalog 中的定义）+ 花色点数；
         *       需要 type/effect/equip 时经 catalog 解析，本类不持有定义。
         */
        struct Card
        {
            std::string instance_id; /**< 对局内唯一（构建时分配，形如 "sha#12"） */
            std::string def_id;      /**< 卡牌定义 id（= 卡牌文件名） */
            Suit suit = Suit::Spade;
            int number = 1;

            bool operator==(const Card &) const = default;
        };

        /**
         * @class CardStack
         * @brief 牌堆容器：vector 尾 = 堆顶。
         * @note 不自动洗回弃牌堆：摸空返回 None，由 gameplay 决定补牌规则。
         */
        class CardStack
        {
        public:
            CardStack() = default;
            CardStack(const CardStack &) = delete;
            CardStack &operator=(const CardStack &) = delete;
            CardStack(CardStack &&) noexcept = default;
            CardStack &operator=(CardStack &&) noexcept = default;

            std::size_t size() const noexcept { return cards.size(); }
            bool empty() const noexcept { return cards.empty(); }

            /** @brief 置顶（弃牌/置入牌堆底语义由调用方决定）。 */
            void push(Card card) { cards.push_back(std::move(card)); }

            /** @brief 取顶；空时 None。 */
            Option<Card> pop()
            {
                if (cards.empty())
                    return Option<Card>::None();
                Card c = std::move(cards.back());
                cards.pop_back();
                return Option<Card>::Some(std::move(c));
            }

            /** @brief 看顶（指针在未 pop 前稳定）；空时 None。 */
            Option<const Card *> top() const
            {
                if (cards.empty())
                    return Option<const Card *>::None();
                return Option<const Card *>::Some(&cards.back());
            }

            /** @brief Fisher–Yates 原地洗牌。 */
            void shuffle(std::mt19937 &rng)
            {
                for (std::size_t i = cards.size(); i > 1; --i)
                {
                    std::uniform_int_distribution<std::size_t> d(0, i - 1);
                    const std::size_t j = d(rng);
                    std::swap(cards[i - 1], cards[j]);
                }
            }

        private:
            std::vector<Card> cards;
        };
    }
}

#endif  // INCLUDE_TKW_CARD_CARD_HPP