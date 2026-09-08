#include <doctest/doctest.h>

#include <filesystem>
#include <string>

#if defined(_WIN32)
#include <windows.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

#include <pjh_platform/fs.hpp>

#include "io/file.hpp"

namespace
{
    /** 每个测试独占的临时目录；清掉上次运行残留，保证断言起点干净。 */
    std::filesystem::path temp_dir(const char *name)
    {
        const auto dir = pjh::platform::Fs::temp_directory() / name;
        std::filesystem::remove_all(dir);
        REQUIRE(pjh::platform::Fs::create_directories(dir).is_ok());
        return dir;
    }
}

TEST_CASE("io: exists is true for a file, false when missing")
{
    const auto dir = temp_dir("tkw_io_exists");
    const auto f = dir / "a.txt";
    CHECK_FALSE(tkw::io::exists(f));
    CHECK(tkw::io::write_text(f, "x").is_ok());
    CHECK(tkw::io::exists(f));
    CHECK(tkw::io::exists(dir));  // 目录也算存在
}

TEST_CASE("io: write + read round-trip preserves content (incl. non-ASCII)")
{
    const auto dir = temp_dir("tkw_io_roundtrip");
    const auto f = dir / "content.txt";
    const std::string payload = "火杀\u2764 三国杀 {\"hp\":4} 尾字节\\";

    CHECK(tkw::io::write_text(f, payload).is_ok());
    auto r = tkw::io::read_text(f);
    REQUIRE(r.is_ok());
    CHECK(r.unwrap() == payload);
}

TEST_CASE("io: empty file yields empty string, not an error")
{
    const auto dir = temp_dir("tkw_io_empty");
    const auto f = dir / "empty.txt";
    CHECK(tkw::io::write_text(f, "").is_ok());
    auto r = tkw::io::read_text(f);
    REQUIRE(r.is_ok());
    CHECK(r.unwrap().empty());
}

TEST_CASE("io: read missing file is NotExist")
{
    const auto dir = temp_dir("tkw_io_missing");
    auto r = tkw::io::read_text(dir / "nope.txt");
    REQUIRE(r.is_err());
    CHECK(r.unwrap_err() == tkw::io::IoError::NotExist);
}

TEST_CASE("io: reading a directory is NotAFile")
{
    const auto dir = temp_dir("tkw_io_dir");
    auto r = tkw::io::read_text(dir);
    REQUIRE(r.is_err());
    CHECK(r.unwrap_err() == tkw::io::IoError::NotAFile);
}

TEST_CASE("io: writing into a missing parent directory is NotExist")
{
    const auto dir = temp_dir("tkw_io_parent");
    auto r = tkw::io::write_text(dir / "no_such_dir" / "f.txt", "x");
    REQUIRE(r.is_err());
    CHECK(r.unwrap_err() == tkw::io::IoError::NotExist);
}

TEST_CASE("io: write_text overwrites existing content")
{
    const auto dir = temp_dir("tkw_io_overwrite");
    const auto f = dir / "ovr.txt";
    CHECK(tkw::io::write_text(f, "first-longer-content").is_ok());
    CHECK(tkw::io::write_text(f, "abc").is_ok());
    auto r = tkw::io::read_text(f);
    REQUIRE(r.is_ok());
    CHECK(r.unwrap() == "abc");
}

#if defined(_WIN32)
TEST_CASE("io: write to read-only file is Permission")
{
    const auto dir = temp_dir("tkw_io_permission");
    const auto f = dir / "locked.txt";
    CHECK(tkw::io::write_text(f, "seed").is_ok());

    const auto attrs = GetFileAttributesW(f.c_str());
    REQUIRE(attrs != INVALID_FILE_ATTRIBUTES);
    REQUIRE(SetFileAttributesW(f.c_str(), attrs | FILE_ATTRIBUTE_READONLY));

    // 无论断言成败都恢复，避免污染临时目录
    struct Restore
    {
        std::filesystem::path file;
        DWORD original;
        ~Restore()
        {
            SetFileAttributesW(file.c_str(), original);
        }
    } guard{f, attrs};

    auto r = tkw::io::write_text(f, "x");

    REQUIRE(r.is_err());
    CHECK(r.unwrap_err() == tkw::io::IoError::Permission);
}
#else
TEST_CASE("io: unreadable file is Permission")
{
    if (geteuid() == 0)
        doctest::skip("running as root: permission checks are bypassed");

    const auto dir = temp_dir("tkw_io_permission");
    const auto f = dir / "locked.txt";
    CHECK(tkw::io::write_text(f, "secret").is_ok());

    auto st = std::filesystem::status(f);
    REQUIRE(st.type() == std::file_type::regular);
    const auto original = st.permissions();

    std::error_code ec;
    std::filesystem::permissions(f, std::filesystem::perms::none, ec);
    REQUIRE_FALSE(ec);

    // 无论断言成败都恢复，避免污染临时目录
    struct Restore
    {
        std::filesystem::path file;
        std::filesystem::perms original;
        ~Restore()
        {
            std::error_code restore_ec;
            std::filesystem::permissions(file, original, restore_ec);
        }
    } guard{f, original};

    auto r = tkw::io::read_text(f);

    REQUIRE(r.is_err());
    CHECK(r.unwrap_err() == tkw::io::IoError::Permission);
}
#endif

TEST_CASE("io: large content round-trip (mmap read path)")
{
    const auto dir = temp_dir("tkw_io_large");
    const auto f = dir / "large.bin";
    std::string payload(128 * 1024, '\0');
    for (std::size_t i = 0; i < payload.size(); ++i)
        payload[i] = static_cast<char>(i * 31 + 7);

    CHECK(tkw::io::write_text(f, payload).is_ok());
    auto r = tkw::io::read_text(f);
    REQUIRE(r.is_ok());
    CHECK(r.unwrap() == payload);
}
