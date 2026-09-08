#include <doctest/doctest.h>

#include <filesystem>
#include <string>

#include <pjh_json/document.hpp>
#include <pjh_platform/fs.hpp>

#include "config/error.hpp"
#include "config/fields.hpp"
#include "config/resource.hpp"
#include "io/file.hpp"

namespace
{
    using namespace tkw::config;

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

TEST_CASE("config: require_int ok / missing / type-mismatch")
{
    const auto d = doc(R"({"damage": 3})");
    const auto &root = d.root();

    auto ok = require_int(root, "damage");
    REQUIRE(ok.is_ok());
    CHECK(ok.unwrap() == 3);

    auto miss = require_int(root, "heal");
    REQUIRE(miss.is_err());
    CHECK(miss.unwrap_err()
          == ConfigError{ConfigErrorKind::MissingField, "heal"});

    const auto ds = doc(R"({"damage": "three"})");
    auto bad = require_int(ds.root(), "damage");
    REQUIRE(bad.is_err());
    CHECK(bad.unwrap_err() == ConfigError{ConfigErrorKind::TypeMismatch, "damage"});
}

TEST_CASE("config: field on a non-object container is TypeMismatch at container path")
{
    const auto d = doc(R"([1, 2])");  // 顶层是数组
    auto r = require_int(d.root(), "damage");
    REQUIRE(r.is_err());
    CHECK(r.unwrap_err() == ConfigError{ConfigErrorKind::TypeMismatch, "root"});

    const auto dn = doc(R"({"a": [1]})");
    auto nested = require_int(dn.root()["a"], "damage", "a");  // 容器是数组，path 由调用方提供
    REQUIRE(nested.is_err());
    CHECK(nested.unwrap_err()
          == ConfigError{ConfigErrorKind::TypeMismatch, "a"});
}

TEST_CASE("config: require_string copies content (incl. non-ASCII)")
{
    const auto d = doc(R"({"name": "火杀"})");
    auto r = require_string(d.root(), "name");
    REQUIRE(r.is_ok());
    CHECK(r.unwrap() == std::string("火杀"));
}

TEST_CASE("config: require_bool / require_object / require_array")
{
    const auto d = doc(
        R"({"flag": true, "boss": {"hp": 5000}, "cards": [1, 2]})");
    const auto &root = d.root();

    auto flag = require_bool(root, "flag");
    REQUIRE(flag.is_ok());
    CHECK(flag.unwrap() == true);

    auto boss = require_object(root, "boss");
    REQUIRE(boss.is_ok());
    auto hp = require_int(*boss.unwrap(), "hp");
    REQUIRE(hp.is_ok());
    CHECK(hp.unwrap() == 5000);

    auto cards = require_array(root, "cards");
    REQUIRE(cards.is_ok());
    CHECK(cards.unwrap()->size() == 2);

    auto wrong = require_array(root, "boss");
    REQUIRE(wrong.is_err());
    CHECK(wrong.unwrap_err()
          == ConfigError{ConfigErrorKind::TypeMismatch, "boss"});
}

TEST_CASE("config: opt_* falls back only on missing, never on type mismatch")
{
    const auto d = doc(R"({"a": 1})");
    const auto &root = d.root();

    auto missing = opt_int(root, "b", 7);
    REQUIRE(missing.is_ok());
    CHECK(missing.unwrap() == 7);

    auto present = opt_int(root, "a", 7);
    REQUIRE(present.is_ok());
    CHECK(present.unwrap() == 1);

    const auto ds = doc(R"({"a": "x"})");
    auto bad = opt_int(ds.root(), "a", 7);
    REQUIRE(bad.is_err());
    CHECK(bad.unwrap_err() == ConfigError{ConfigErrorKind::TypeMismatch, "a"});

    auto miss_str = opt_string(root, "name", "默认");
    REQUIRE(miss_str.is_ok());
    CHECK(miss_str.unwrap() == std::string("默认"));
}

TEST_CASE("config: each builds element paths like cards[3].damage")
{
    const auto d = doc(
        R"({"cards": [{"damage": 1}, {"damage": 2}, {"damage": 3}, {"name": "x"}]})");
    const auto &root = d.root();

    std::string seen_path;
    auto r = each(root, "cards", {}, [&](const pjh::json::Json &item, std::string_view ip) -> ConfigResult<void>
    {
        seen_path = std::string(ip);
        auto dmg = require_int(item, "damage", ip);
        if (dmg.is_err())
            return ConfigResult<void>::Err(dmg.unwrap_err());
        return ConfigResult<void>::Ok();
    });
    REQUIRE(r.is_err());
    CHECK(r.unwrap_err()
          == ConfigError{ConfigErrorKind::MissingField, "cards[3].damage"});
    CHECK(seen_path == "cards[3]");
}

TEST_CASE("config: each on missing / non-array field")
{
    const auto d = doc(R"({"cards": 5})");
    const auto &root = d.root();

    auto not_arr = each(
        root, "cards", {},
        [](const pjh::json::Json &, std::string_view) { return ConfigResult<void>::Ok(); });
    REQUIRE(not_arr.is_err());
    CHECK(not_arr.unwrap_err()
          == ConfigError{ConfigErrorKind::TypeMismatch, "cards"});

    auto missing = each(
        root, "nope", {},
        [](const pjh::json::Json &, std::string_view) { return ConfigResult<void>::Ok(); });
    REQUIRE(missing.is_err());
    CHECK(missing.unwrap_err() == ConfigError{ConfigErrorKind::MissingField, "nope"});
}

TEST_CASE("config: each stops at the first element error")
{
    const auto d = doc(R"({"cards": [{"damage": "x"}, {"damage": 2}]})");
    const auto &root = d.root();

    int calls = 0;
    auto r = each(root, "cards", {}, [&](const pjh::json::Json &item, std::string_view ip)
    {
        ++calls;
        return require_int(item, "damage", ip);
    });
    REQUIRE(r.is_err());
    CHECK(r.unwrap_err() == ConfigError{ConfigErrorKind::TypeMismatch, "cards[0].damage"});
    CHECK(calls == 1);  // 第二个元素未再进入回调
}

TEST_CASE("config: resource load missing file is FileNotFound with full path")
{
    const auto dir = temp_dir("tkw_cfg_missing");
    ResourceStore store(dir);

    auto r = store.load("nope");
    REQUIRE(r.is_err());
    const auto expect = (dir / "nope.json").string();
    CHECK(r.unwrap_err() == ConfigError{ConfigErrorKind::FileNotFound, expect});
}

TEST_CASE("config: resource load invalid json is ParseError with offset")
{
    const auto dir = temp_dir("tkw_cfg_badjson");
    CHECK(tkw::io::write_text(dir / "bad.json", R"({"hp": 4,)").is_ok());
    ResourceStore store(dir);

    auto r = store.load("bad");
    REQUIRE(r.is_err());
    const auto &err = r.unwrap_err();
    CHECK(err.kind == ConfigErrorKind::ParseError);
    CHECK(err.detail.find("@") != std::string::npos);  // "路径 @ offset"
}

TEST_CASE("config: load + each + require_int full pipeline")
{
    const auto dir = temp_dir("tkw_cfg_pipeline");
    CHECK(
        tkw::io::write_text(dir / "cards.json", R"({"cards": [{"damage": 1}, {"damage": 3}]})")
            .is_ok());
    ResourceStore store(dir);

    auto r = store.load("cards");
    REQUIRE(r.is_ok());

    int total = 0;
    auto er = each(r.unwrap().root(), "cards", {},
                   [&](const pjh::json::Json &item, std::string_view ip) -> ConfigResult<void>
    {
        auto dmg = require_int(item, "damage", ip);
        if (dmg.is_err())
            return ConfigResult<void>::Err(dmg.unwrap_err());
        total += static_cast<int>(dmg.unwrap());
        return ConfigResult<void>::Ok();
    });
    CHECK(er.is_ok());
    CHECK(total == 4);
}
