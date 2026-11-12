#include <gtest/gtest.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "envbox/core/tar.hpp"

namespace envbox {
namespace {

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

std::string to_string(const std::vector<std::byte>& bytes) {
    return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}

TEST(Tar, NormalizeCollapsesRedundantSlashesAndDots) {
    auto result = normalize_member_path("applications\\git//./config");
    ASSERT_TRUE(result);
    EXPECT_EQ(*result, "applications/git/config");
}

TEST(Tar, NormalizeRejectsTraversal) {
    EXPECT_FALSE(normalize_member_path("../escape"));
    EXPECT_FALSE(normalize_member_path("a/../../b"));
    EXPECT_FALSE(normalize_member_path("/absolute/path"));
    EXPECT_FALSE(normalize_member_path("C:/windows/system32"));
    EXPECT_FALSE(normalize_member_path(""));
}

TEST(Tar, RoundtripSingleFile) {
    const std::vector<TarEntry> entries{make_file("applications/git/config", "[core]\n\tbare = false\n")};
    auto bytes = tar_write(entries);
    ASSERT_TRUE(bytes);
    auto parsed = tar_read(std::span<const std::byte>{*bytes});
    ASSERT_TRUE(parsed);
    ASSERT_EQ(parsed->size(), 1u);
    EXPECT_EQ((*parsed)[0].path, "applications/git/config");
    EXPECT_EQ(to_string((*parsed)[0].data), "[core]\n\tbare = false\n");
}

TEST(Tar, RoundtripEmptyAndBinaryEntries) {
    TarEntry binary;
    binary.path = "assets/bin/font.ttf";
    binary.data = {std::byte{0x00}, std::byte{0xFF}, std::byte{0x01}, std::byte{0x00}};
    const std::vector<TarEntry> entries{binary, make_file("applications/empty/file", "")};
    auto bytes = tar_write(entries);
    ASSERT_TRUE(bytes);
    auto parsed = tar_read(std::span<const std::byte>{*bytes});
    ASSERT_TRUE(parsed);
    ASSERT_EQ(parsed->size(), 2u);
    EXPECT_EQ((*parsed)[0].data.size(), 4u);
    EXPECT_EQ((*parsed)[1].data.size(), 0u);
}

TEST(Tar, RoundtripLongPathViaUstarPrefix) {
    const std::string path =
        "applications/some-very-long-application-name/that-keeps-going/with-many/nested/directories/and-a-reasonably-long-file-name.json";
    ASSERT_GT(path.size(), 100u);
    auto bytes = tar_write(std::vector<TarEntry>{make_file(path, "payload")});
    ASSERT_TRUE(bytes);
    auto parsed = tar_read(std::span<const std::byte>{*bytes});
    ASSERT_TRUE(parsed);
    ASSERT_EQ(parsed->size(), 1u);
    EXPECT_EQ((*parsed)[0].path, path);
}

TEST(Tar, WriteRejectsDuplicates) {
    const std::vector<TarEntry> entries{make_file("system/environment.json", "{}"),
                                        make_file("system/./environment.json", "{}")};
    auto bytes = tar_write(entries);
    ASSERT_FALSE(bytes);
    EXPECT_EQ(bytes.error().code, ErrorCode::validation);
}

TEST(Tar, ReadRejectsCorruptedChecksum) {
    auto bytes = tar_write(std::vector<TarEntry>{make_file("system/environment.json", "{}")});
    ASSERT_TRUE(bytes);
    ASSERT_GT(bytes->size(), 200u);
    (*bytes)[10] ^= std::byte{0xFF};
    auto parsed = tar_read(std::span<const std::byte>{*bytes});
    ASSERT_FALSE(parsed);
    EXPECT_EQ(parsed.error().code, ErrorCode::format);
}

TEST(Tar, ReadRejectsUnsupportedEntryType) {
    auto bytes = tar_write(std::vector<TarEntry>{make_file("system/environment.json", "{}")});
    ASSERT_TRUE(bytes);
    (*bytes)[156] = std::byte{'2'};
    std::memset(bytes->data() + 148, ' ', 8);
    unsigned int sum = 0;
    for (std::size_t i = 0; i < 512; ++i) {
        sum += static_cast<unsigned char>((*bytes)[i]);
    }
    char checksum[8]{};
    std::snprintf(checksum, sizeof checksum, "%06o", sum);
    std::memcpy(bytes->data() + 148, checksum, 6);
    (*bytes)[154] = std::byte{0};
    (*bytes)[155] = std::byte{' '};

    auto parsed = tar_read(std::span<const std::byte>{*bytes});
    ASSERT_FALSE(parsed);
    EXPECT_EQ(parsed.error().code, ErrorCode::format);
}

TEST(Tar, ReadRejectsTruncatedImage) {
    auto bytes = tar_write(std::vector<TarEntry>{make_file("system/environment.json", "{}")});
    ASSERT_TRUE(bytes);
    bytes->resize(bytes->size() - 700);
    auto parsed = tar_read(std::span<const std::byte>{*bytes});
    ASSERT_FALSE(parsed);
}

}
}
