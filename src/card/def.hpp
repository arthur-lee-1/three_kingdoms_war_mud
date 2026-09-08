/**
 * @file def.hpp
 * @brief 卡牌域值类型：枚举 + 不可变 CardDef（卡牌定义）。
 * @note 本文件不含任何 JSON 解析（归 catalog.hpp）。CardDef 是纯值类型：
 *       解析器产出后即独立于 Document 生命周期，可直接值拷贝/比较。
 * @note effect.kind / judge / abilities 是「配置数据 ↔ 代码语义」的三个接缝：
 *       主动效果、判定条件、装备被动分别承载，未知值在加载时直接
 *       InvalidValue 失败，保证跑起来的数据永远是代码认识的。
 */

#ifndef INCLUDE_TKW_CARD_DEF_HPP
#define INCLUDE_TKW_CARD_DEF_HPP

#include <cstdint>
#include <string>
#include <vector>

#include "util/types.hpp"

namespace tkw
{
    namespace card
    {
        /** @brief 花色（判定/拼点依赖具体花色点数）。 */
        enum class Suit : std::uint8_t
        {
            Spade,
            Club,
            Heart,
            Diamond,
        };

        /** @brief 卡牌大类。 */
        enum class CardType : std::uint8_t
        {
            Basic,     /**< 基本牌：杀/闪/桃 */
            Trick,     /**< 锦囊牌（含延时） */
            Equipment, /**< 装备牌 */
        };

        /**
         * @brief 主动效果类别（封闭枚举，与 JSON 的 effect.kind 一一对应）。
         * @note 只承载「打出的牌怎么结算」；装备被动见 Ability，延时判定见
         *       JudgeEffect（条件进数据）。
         */
        enum class CardEffectKind : std::uint8_t
        {
            Damage,        /**< 造成 amount 点伤害（杀） */
            Jink,          /**< 响应牌：抵消杀/万箭（闪） */
            Heal,          /**< 回复 amount 点体力（桃/桃园） */
            Draw,          /**< 摸 count 张牌（无中生有） */
            DiscardTarget, /**< 弃置目标 count 张牌（过河拆桥） */
            Steal,         /**< 获得目标 count 张牌（顺手牵羊） */
            AoeDamage,     /**< 全场响应 response，否则受 amount 伤害 */
            Duel,          /**< 决斗：轮流出杀 */
            RevealPick,    /**< 五谷丰登（未实现） */
            BorrowedSword, /**< 借刀杀人（未实现） */
        };

        /** @brief 装备被动能力（一件装备可带多个，经 equip.hpp 查询）。 */
        enum class Ability : std::uint8_t
        {
            NoShaLimit,            /**< 诸葛连弩：杀无次数限制 */
            IgnoreArmor,           /**< 青釭剑：无视防具 */
            Cixiong,               /**< 雌雄双股剑（未实现） */
            ExtraShaAfterJink,     /**< 青龙偃月刀：被闪可再出杀 */
            TwoCardsAsSha,         /**< 丈八蛇矛（未实现） */
            DiscardTwoForceDamage, /**< 贯石斧：弃两牌令杀仍造成伤害 */
            MultiTargetSha,        /**< 方天画戟（未实现） */
            DiscardHorseOnDamage,  /**< 麒麟弓：伤害后弃目标坐骑 */
            DamageAsDiscard,       /**< 寒冰剑：防止伤害改弃两张牌 */
            JudgementJink,         /**< 八卦阵：判定成功视为闪 */
            BlackShaImmune,        /**< 仁王盾：黑杀无效 */
        };

        /** @brief 判定触发条件（数据描述，不再写死在代码里）。 */
        enum class JudgeTrigger : std::uint8_t
        {
            Red,       /**< 红色（♥/♦） */
            Black,     /**< 黑色（♠/♣） */
            Heart,     /**< 红桃 */
            NotHeart,  /**< 非红桃 */
            Spade2to9, /**< 黑桃 2~9 */
        };

        /** @brief 判定结果动作（触发/未触发各一个）。 */
        enum class JudgeAction : std::uint8_t
        {
            Nothing,    /**< 无额外效果 */
            SkipPlay,   /**< 跳过出牌阶段（乐不思蜀） */
            Damage,     /**< 造成 amount 点伤害（闪电） */
            Jink,       /**< 视为打出闪（八卦阵） */
            PassToNext, /**< 移入下家判定区（闪电未劈中） */
        };

        /** @brief 延时锦囊/防具的判定描述：条件、成功动作、失败动作。 */
        struct JudgeEffect
        {
            JudgeTrigger trigger = JudgeTrigger::Red;
            JudgeAction success = JudgeAction::Nothing;
            JudgeAction failure = JudgeAction::Nothing;
            int amount = 0;

            bool operator==(const JudgeEffect &) const = default;
        };

        /** @brief 效果作用范围（决定结算时如何选目标）。 */
        enum class Scope : std::uint8_t
        {
            Self,      /**< 仅自己 */
            OneOther,  /**< 一名其他角色 */
            AllOthers, /**< 所有其他角色 */
            All,       /**< 所有角色（含自己） */
        };

        /** @brief 需要目标打出的响应牌类别。 */
        enum class ResponseKind : std::uint8_t
        {
            Sha,
            Jink,
        };

        /** @brief 装备槽位。 */
        enum class EquipSlot : std::uint8_t
        {
            Weapon,
            Armor,
            Horse,
        };

        /**
         * @brief 坐骑方向。
         * @note Offensive = -1马（你计算与其他角色的距离 -1）；
         *       Defensive = +1马（其他角色计算与你的距离 +1）。
         */
        enum class HorseDirection : std::uint8_t
        {
            Offensive,
            Defensive,
        };

        /** @brief 一张实体牌副本的花色点数（判定/拼点用）。 */
        struct CardCopy
        {
            Suit suit = Suit::Spade;
            int number = 1;

            bool operator==(const CardCopy &) const = default;
        };

        /** @brief 装备参数：槽位；武器带攻击范围；坐骑带方向。 */
        struct CardEquip
        {
            EquipSlot slot = EquipSlot::Weapon;
            int range = 0;
            Option<HorseDirection> direction = Option<HorseDirection>::None();

            bool operator==(const CardEquip &) const = default;
        };

        /**
         * @brief 卡牌效果参数：kind 是判别器，其余字段按 kind 取用
         *        （amount/count/scope/response/range 均可不填）。
         */
        struct CardEffect
        {
            CardEffectKind kind = CardEffectKind::Damage;
            int amount = 0;
            int count = 0;
            Option<Scope> scope = Option<Scope>::None();
            Option<ResponseKind> response = Option<ResponseKind>::None();
            int range = 0;

            bool operator==(const CardEffect &) const = default;
        };

        /**
         * @brief 不可变卡牌定义：由 CardDefCatalog 从 JSON 解析产出。
         * @note 含 id（= 文件名）+ 精确副本列表（copies）。牌堆构建时
         *       按 copies 逐一生成实体牌。
         */
        struct CardDef
        {
            std::string id;
            std::string name;
            CardType type = CardType::Basic;
            std::string subtype;
            std::string set;
            std::vector<CardCopy> copies;
            std::string text;
            Option<CardEffect> effect = Option<CardEffect>::None();
            Option<CardEquip> equip = Option<CardEquip>::None();
            Option<JudgeEffect> judge = Option<JudgeEffect>::None();
            std::vector<Ability> abilities;
            bool rescue = false;  /**< 可作濒死救场牌（桃） */
            bool counter = false; /**< 可作无懈响应牌（无懈可击） */

            bool operator==(const CardDef &) const = default;
        };
    }
}

#endif  // INCLUDE_TKW_CARD_DEF_HPP