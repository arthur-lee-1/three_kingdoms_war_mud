#include <doctest/doctest.h>

#include <filesystem>
#include <iterator>
#include <string>

#include <pjh_json/document.hpp>
#include <pjh_platform/fs.hpp>

#include "card/catalog.hpp"
#include "card/def.hpp"
#include "config/error.hpp"
#include "config/resource.hpp"
#include "io/file.hpp"

namespace
{
    using namespace tkw::card;
    using tkw::config::ConfigError;
    using tkw::config::ConfigErrorKind;

    std::filesystem::path temp_dir(const char *name)
    {
        const auto dir = pjh::platform::Fs::temp_directory() / name;
        std::filesystem::remove_all(dir);
        REQUIRE(pjh::platform::Fs::create_directories(dir).is_ok());
        return dir;
    }

    /** 解析内联 JSON 为 Document（move-only，按值返回）。 */
    pjh::json::Document doc(const std::string &text)
    {
        return pjh::json::parse_copy(text);
    }
}

TEST_CASE("card: parse_card_def basic card")
{
    const auto d = doc(R"({
        "id": "sha", "name": "杀", "type": "basic", "subtype": "attack",
        "set": "standard",
        "copies": [ {"suit": "spade", "number": 7}, {"suit": "heart", "number": 10} ],
        "text": "出牌阶段……",
        "effect": {"kind": "damage", "amount": 1, "scope": "one_other", "response": "jink"}
    })");
    auto r = parse_card_def(d.root(), "sha");
    REQUIRE(r.is_ok());
    const auto &def = r.unwrap();
    CHECK(def.id == "sha");
    CHECK(def.name == "杀");
    CHECK(def.type == CardType::Basic);
    CHECK(def.subtype == "attack");
    CHECK(def.set == "standard");
    CHECK(def.copies.size() == 2);
    CHECK(def.copies[0] == CardCopy{Suit::Spade, 7});
    REQUIRE(def.effect.is_some());
    CHECK(def.effect.unwrap().kind == CardEffectKind::Damage);
    CHECK(def.effect.unwrap().amount == 1);
    CHECK(def.effect.unwrap().scope.contains(Scope::OneOther));
    CHECK(def.effect.unwrap().response.contains(ResponseKind::Jink));
    CHECK(def.equip.is_none());
}

TEST_CASE("card: parse_card_def equipment with horse direction")
{
    const auto d = doc(R"({
        "id": "chitu", "name": "赤兔", "type": "equipment", "subtype": "horse",
        "set": "standard",
        "copies": [ {"suit": "heart", "number": 5} ],
        "text": "坐骑·-1。锁定技，你与其他角色的距离-1。",
        "equip": {"slot": "horse", "direction": "offensive"}
    })");
    auto r = parse_card_def(d.root(), "chitu");
    REQUIRE(r.is_ok());
    CHECK(r.unwrap().effect.is_none());
    REQUIRE(r.unwrap().equip.is_some());
    const auto &eq = r.unwrap().equip.unwrap();
    CHECK(eq.slot == EquipSlot::Horse);
    CHECK(eq.range == 0);
    CHECK(eq.direction.contains(HorseDirection::Offensive));
}

TEST_CASE("card: parse_card_def weapon carries range")
{
    const auto d = doc(R"({
        "id": "liangnu", "name": "诸葛连弩", "type": "equipment", "subtype": "weapon",
        "copies": [ {"suit": "club", "number": 1} ],
        "equip": {"slot": "weapon", "range": 1},
        "effect": {"kind": "no_sha_limit"}
    })");
    auto r = parse_card_def(d.root(), "liangnu");
    REQUIRE(r.is_ok());
    REQUIRE(r.unwrap().equip.is_some());
    CHECK(r.unwrap().equip.unwrap().slot == EquipSlot::Weapon);
    CHECK(r.unwrap().equip.unwrap().range == 1);
    REQUIRE(r.unwrap().effect.is_some());
    CHECK(r.unwrap().effect.unwrap().kind == CardEffectKind::NoShaLimit);
}

TEST_CASE("card: parse_card_def default set / missing optionals")
{
    const auto d = doc(R"({
        "id": "a", "name": "a", "type": "basic",
        "copies": [ {"suit": "diamond", "number": 13} ]
    })");
    auto r = parse_card_def(d.root(), "a");
    REQUIRE(r.is_ok());
    CHECK(r.unwrap().set == "standard");
    CHECK(r.unwrap().subtype.empty());
    CHECK(r.unwrap().effect.is_none());
    CHECK(r.unwrap().equip.is_none());
}

TEST_CASE("card: parse errors carry field paths")
{
    auto miss = parse_card_def(doc(R"({"name": "x"})").root(), "x");
    REQUIRE(miss.is_err());
    CHECK(miss.unwrap_err() == ConfigError{ConfigErrorKind::MissingField, "x.id"});

    auto bad_kind = parse_card_def(
        doc(R"({"id": "a", "name": "a", "type": "basic", "copies": [],
                 "effect": {"kind": "explode"}})").root(), "a");
    REQUIRE(bad_kind.is_err());
    CHECK(bad_kind.unwrap_err() == ConfigError{ConfigErrorKind::InvalidValue, "a.effect.kind"});

    auto bad_num = parse_card_def(
        doc(R"({"id": "a", "name": "a", "type": "basic",
                 "copies": [ {"suit": "spade", "number": 14} ]})").root(), "a");
    REQUIRE(bad_num.is_err());
    CHECK(bad_num.unwrap_err() == ConfigError{ConfigErrorKind::InvalidValue, "a.copies[0].number"});

    auto bad_suit = parse_card_def(
        doc(R"({"id": "a", "name": "a", "type": "basic",
                 "copies": [ {"suit": "star", "number": 1} ]})").root(), "a");
    REQUIRE(bad_suit.is_err());
    CHECK(bad_suit.unwrap_err() == ConfigError{ConfigErrorKind::InvalidValue, "a.copies[0].suit"});
}

TEST_CASE("card: opt default must not swallow type mismatch")
{
    auto r = parse_card_def(
        doc(R"({"id": "a", "name": "a", "type": "basic", "copies": [],
                 "effect": {"kind": "damage", "amount": "x"}})").root(), "a");
    REQUIRE(r.is_err());
    CHECK(r.unwrap_err() == ConfigError{ConfigErrorKind::TypeMismatch, "a.effect.amount"});
}

TEST_CASE("card: effect rescue/counter flags parse with false default")
{
    auto r = parse_card_def(
        doc(R"({"id": "tao", "name": "桃", "type": "basic", "subtype": "heal",
                 "copies": [ {"suit": "heart", "number": 3} ],
                 "effect": {"kind": "heal", "amount": 1, "scope": "self", "rescue": true}})").root(),
        "tao");
    REQUIRE(r.is_ok());
    CHECK(r.unwrap().effect.unwrap().rescue);
    CHECK_FALSE(r.unwrap().effect.unwrap().counter);

    auto c = parse_card_def(
        doc(R"({"id": "wuxie", "name": "无懈可击", "type": "trick", "subtype": "instant",
                 "copies": [ {"suit": "spade", "number": 11} ],
                 "effect": {"kind": "counter_trick", "counter": true}})").root(),
        "wuxie");
    REQUIRE(c.is_ok());
    CHECK(c.unwrap().effect.unwrap().counter);
    CHECK_FALSE(c.unwrap().effect.unwrap().rescue);
}

TEST_CASE("card: effect field invariants and subtype are enforced")
{
    auto dmg = parse_card_def(
        doc(R"({"id": "a", "name": "a", "type": "basic", "copies": [],
                 "effect": {"kind": "damage"}})").root(), "a");
    REQUIRE(dmg.is_err());
    CHECK(dmg.unwrap_err() ==
          ConfigError{ConfigErrorKind::InvalidValue, "a.effect.amount"});

    auto draw = parse_card_def(
        doc(R"({"id": "a", "name": "a", "type": "trick", "copies": [],
                 "effect": {"kind": "draw"}})").root(), "a");
    REQUIRE(draw.is_err());
    CHECK(draw.unwrap_err() ==
          ConfigError{ConfigErrorKind::InvalidValue, "a.effect.count"});

    auto steal = parse_card_def(
        doc(R"({"id": "a", "name": "a", "type": "trick", "copies": [],
                 "effect": {"kind": "steal", "count": 1}})").root(), "a");
    REQUIRE(steal.is_err());
    CHECK(steal.unwrap_err() ==
          ConfigError{ConfigErrorKind::InvalidValue, "a.effect.range"});

    auto sub = parse_card_def(
        doc(R"({"id": "a", "name": "a", "type": "basic", "subtype": "nope",
                 "copies": []})").root(), "a");
    REQUIRE(sub.is_err());
    CHECK(sub.unwrap_err() == ConfigError{ConfigErrorKind::InvalidValue, "a.subtype"});
}

TEST_CASE("card: catalog loads deck + card files")
{
    const auto dir = temp_dir("tkw_card_catalog");
    REQUIRE(pjh::platform::Fs::create_directories(dir / "cards").is_ok());

    CHECK(tkw::io::write_text(dir / "deck.json",
                              R"({"name": "mini", "cards": ["sha", "chitu"]})")
              .is_ok());
    CHECK(tkw::io::write_text(dir / "cards" / "sha.json", R"({
        "id": "sha", "name": "杀", "type": "basic", "subtype": "attack",
        "copies": [ {"suit": "spade", "number": 7} ],
        "effect": {"kind": "damage", "amount": 1, "scope": "one_other"}
    })").is_ok());
    CHECK(tkw::io::write_text(dir / "cards" / "chitu.json", R"({
        "id": "chitu", "name": "赤兔", "type": "equipment", "subtype": "horse",
        "copies": [ {"suit": "heart", "number": 5} ],
        "equip": {"slot": "horse", "direction": "offensive"}
    })").is_ok());

    tkw::config::ResourceStore store(dir);
    auto r = CardDefCatalog::load(store, "deck");
    REQUIRE(r.is_ok());
    CHECK(r.unwrap().size() == 2);
    CHECK(r.unwrap().total_copies() == 2);

    auto sha = r.unwrap().find("sha");
    REQUIRE(sha.is_some());
    CHECK(sha.unwrap()->name == "杀");
    CHECK(sha.unwrap()->copies.size() == 1);
    CHECK(r.unwrap().find("nope").is_none());

    // 迭代顺序 = deck.json 引用顺序（["sha", "chitu"]），不得退化为无序
    const auto &cat = r.unwrap();
    auto it = cat.begin();
    CHECK(it->id == "sha");
    ++it;
    CHECK(it->id == "chitu");
    CHECK(std::next(it) == cat.end());
}

TEST_CASE("card: catalog load error paths")
{
    const auto dir = temp_dir("tkw_card_catalog_err");
    REQUIRE(pjh::platform::Fs::create_directories(dir / "cards").is_ok());

    CHECK(tkw::io::write_text(dir / "deck.json",
                              R"({"name": "mini", "cards": ["ghost"]})")
              .is_ok());

    // 引用的卡牌文件缺失 → FileNotFound（完整路径）
    {
        tkw::config::ResourceStore store(dir);
        auto r = CardDefCatalog::load(store, "deck");
        REQUIRE(r.is_err());
        CHECK(r.unwrap_err().kind == ConfigErrorKind::FileNotFound);
        CHECK(r.unwrap_err().detail == (dir / "cards" / "ghost.json").string());
    }

    // 文件内 id 与文件名不一致 → InvalidValue
    CHECK(tkw::io::write_text(dir / "cards" / "ghost.json", R"({
        "id": "tao", "name": "桃", "type": "basic",
        "copies": [ {"suit": "heart", "number": 3} ]
    })").is_ok());
    {
        tkw::config::ResourceStore store(dir);
        auto r = CardDefCatalog::load(store, "deck");
        REQUIRE(r.is_err());
        CHECK(r.unwrap_err().kind == ConfigErrorKind::InvalidValue);
        CHECK(r.unwrap_err().detail == "cards/ghost.json.id");
    }

    // deck 重复引用同一卡牌 → InvalidValue
    CHECK(tkw::io::write_text(dir / "deck.json",
                              R"({"name": "mini", "cards": ["tao", "tao"]})")
              .is_ok());
    CHECK(tkw::io::write_text(dir / "cards" / "tao.json", R"({
        "id": "tao", "name": "桃", "type": "basic",
        "copies": [ {"suit": "heart", "number": 3} ]
    })").is_ok());
    {
        tkw::config::ResourceStore store(dir);
        auto r = CardDefCatalog::load(store, "deck");
        REQUIRE(r.is_err());
        CHECK(r.unwrap_err().kind == ConfigErrorKind::InvalidValue);
        CHECK(r.unwrap_err().detail.find("重复引用") != std::string::npos);
    }
}

#ifdef TKW_TEST_RESOURCE_DIR
TEST_CASE("card: real standard deck loads to 108 copies")
{
    tkw::config::ResourceStore store(TKW_TEST_RESOURCE_DIR);
    auto r = CardDefCatalog::load(store, "deck");
    REQUIRE(r.is_ok());
    const auto &cat = r.unwrap();
    CHECK(cat.size() == 32);
    CHECK(cat.total_copies() == 108);

    // 迭代顺序 = deck.json 引用顺序（首张 sha，末张 zhuahuang）
    CHECK(cat.begin()->id == "sha");
    std::string last_id;
    for (const auto &def : cat)
        last_id = def.id;
    CHECK(last_id == "zhuahuang");

    auto sha = cat.find("sha");
    REQUIRE(sha.is_some());
    CHECK(sha.unwrap()->copies.size() == 30);
    CHECK(sha.unwrap()->type == CardType::Basic);
    REQUIRE(sha.unwrap()->effect.is_some());
    CHECK(sha.unwrap()->effect.unwrap().kind == CardEffectKind::Damage);
    CHECK(sha.unwrap()->effect.unwrap().scope.contains(Scope::OneOther));
    CHECK(sha.unwrap()->effect.unwrap().response.contains(ResponseKind::Jink));

    auto liangnu = cat.find("liangnu");
    REQUIRE(liangnu.is_some());
    REQUIRE(liangnu.unwrap()->equip.is_some());
    CHECK(liangnu.unwrap()->equip.unwrap().slot == EquipSlot::Weapon);
    CHECK(liangnu.unwrap()->equip.unwrap().range == 1);

    auto taoyuan = cat.find("taoyuan");
    REQUIRE(taoyuan.is_some());
    REQUIRE(taoyuan.unwrap()->effect.is_some());
    CHECK(taoyuan.unwrap()->effect.unwrap().kind == CardEffectKind::Heal);
    CHECK(taoyuan.unwrap()->effect.unwrap().scope.contains(Scope::All));
}
#endif  // TKW_TEST_RESOURCE_DIR