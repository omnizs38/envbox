#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <vector>

#include "envbox/core/container.hpp"

namespace envbox {
namespace {

namespace fs = std::filesystem;

std::vector<std::byte> to_bytes(std::string_view text) {
    return {reinterpret_cast<const std::byte*>(text.data()),
            reinterpret_cast<const std::byte*>(text.data() + text.size())};
}

TarEntry make_file(std::string path, std::string content) {
    TarEntry entry;
    entry.path = std::move(path);
    entry.data = to_bytes(content);
    return entry;
}

fs::path temp_file(const std::string& name) {
    static int counter = 0;
    const auto unique = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "-" +
                        std::to_string(counter++);
    return fs::temp_directory_path() / ("envbox-test-" + unique + "-" + name);
}

TEST(Container, EncodeDecodeRoundtrip) {
    const std::vector<TarEntry> entries{make_file("manifest.json", "{\"format\":\"envbox\"}"),
                                        make_file("applications/git/config", "[core]\n")};
    auto bytes = encode_container(entries);
    ASSERT_TRUE(bytes);
    auto decoded = decode_container(std::span<const std::byte>{*bytes});
    ASSERT_TRUE(decoded);
    ASSERT_EQ(decoded->size(), 2u);
    EXPECT_EQ((*decoded)[0].path, "manifest.json");
    EXPECT_EQ((*decoded)[1].path, "applications/git/config");
}

TEST(Container, FileRoundtrip) {
    const fs::path path = temp_file("roundtrip.envbox");
    const std::vector<TarEntry> entries{make_file("profile.json", "{\"name\":\"test\"}")};
    ASSERT_TRUE(write_container(path, entries));
    auto decoded = read_container(path);
    ASSERT_TRUE(decoded);
    ASSERT_EQ(decoded->size(), 1u);
    EXPECT_EQ((*decoded)[0].path, "profile.json");
    std::error_code ec;
    fs::remove(path, ec);
}

TEST(Container, RejectsBadMagic) {
    std::vector<std::byte> garbage(64, std::byte{0xAB});
    auto decoded = decode_container(std::span<const std::byte>{garbage});
    ASSERT_FALSE(decoded);
    EXPECT_EQ(decoded.error().code, ErrorCode::format);
}

TEST(Container, RejectsTruncatedMagic) {
    const std::vector<std::byte> tiny{std::byte{'E'}, std::byte{'N'}, std::byte{'V'}};
    auto decoded = decode_container(std::span<const std::byte>{tiny});
    ASSERT_FALSE(decoded);
    EXPECT_EQ(decoded.error().code, ErrorCode::format);
}

TEST(Container, RejectsCorruptZstdFrame) {
    std::vector<std::byte> bytes{std::byte{'E'}, std::byte{'N'}, std::byte{'V'}, std::byte{'B'},
                                 std::byte{'O'}, std::byte{'X'}, std::byte{0x1A}, std::byte{0x01}};
    bytes.insert(bytes.end(), {std::byte{0x00}, std::byte{0x01}, std::byte{0x02}, std::byte{0x03},
                               std::byte{0x04}, std::byte{0x05}, std::byte{0x06}, std::byte{0x07}});
    auto decoded = decode_container(std::span<const std::byte>{bytes});
    ASSERT_FALSE(decoded);
    EXPECT_EQ(decoded.error().code, ErrorCode::format);
}

TEST(Container, ReadMissingFileReportsIoError) {
    const fs::path path = temp_file("does-not-exist.envbox");
    auto decoded = read_container(path);
    ASSERT_FALSE(decoded);
    EXPECT_EQ(decoded.error().code, ErrorCode::io);
}

}
}
