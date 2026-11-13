#include <gtest/gtest.h>

#include "envbox/compat/paths.hpp"

namespace envbox {
namespace {

TEST(Compat, ClassifyPath) {
    EXPECT_EQ(compat::classify_path(""), compat::PathKind::empty);
    EXPECT_EQ(compat::classify_path("relative/dir/file"), compat::PathKind::relative);
    EXPECT_EQ(compat::classify_path("C:\\Users\\alice"), compat::PathKind::windows_absolute);
    EXPECT_EQ(compat::classify_path("c:/users/alice"), compat::PathKind::windows_absolute);
    EXPECT_EQ(compat::classify_path("\\\\server\\share"), compat::PathKind::windows_absolute);
    EXPECT_EQ(compat::classify_path("/home/alice"), compat::PathKind::unix_absolute);
}

TEST(Compat, ContainsWindowsPathDoesNotMatchUrls) {
    EXPECT_TRUE(compat::contains_windows_path("C:\\tools\\git.exe"));
    EXPECT_TRUE(compat::contains_windows_path("c:/tools/git.exe"));
    EXPECT_TRUE(compat::contains_windows_path("editor = C:\\vim\\vim.exe"));
    EXPECT_FALSE(compat::contains_windows_path("https://example.com/path"));
    EXPECT_FALSE(compat::contains_windows_path("vscode://settings"));
    EXPECT_FALSE(compat::contains_windows_path("about:blank"));
    EXPECT_FALSE(compat::contains_windows_path("editor.fontSize"));
}

TEST(Compat, ConvertsWindowsUserPathsToLinuxAndMacos) {
    auto linux = compat::convert_path("C:\\Users\\alice\\dev\\project", OsKind::windows, OsKind::linux);
    ASSERT_TRUE(linux);
    EXPECT_EQ(linux->path, "/home/alice/dev/project");
    EXPECT_TRUE(linux->exact);

    auto macos = compat::convert_path("C:\\Users\\alice\\dev\\project", OsKind::windows, OsKind::macos);
    ASSERT_TRUE(macos);
    EXPECT_EQ(macos->path, "/Users/alice/dev/project");
    EXPECT_TRUE(macos->exact);
}

TEST(Compat, ConvertsOtherDrivePathsInexactly) {
    auto linux = compat::convert_path("C:\\Program Files\\Git", OsKind::windows, OsKind::linux);
    ASSERT_TRUE(linux);
    EXPECT_EQ(linux->path, "/mnt/c/Program Files/Git");
    EXPECT_FALSE(linux->exact);
    EXPECT_FALSE(linux->note.empty());
}

TEST(Compat, ConvertsUnixUserPathsToWindows) {
    auto windows = compat::convert_path("/home/alice/.config/git/config", OsKind::linux, OsKind::windows);
    ASSERT_TRUE(windows);
    EXPECT_EQ(windows->path, "C:\\Users\\alice\\.config\\git\\config");
    EXPECT_TRUE(windows->exact);

    auto mac = compat::convert_path("/Users/alice/Library", OsKind::macos, OsKind::windows);
    ASSERT_TRUE(mac);
    EXPECT_EQ(mac->path, "C:\\Users\\alice\\Library");
    EXPECT_TRUE(mac->exact);
}

TEST(Compat, ConvertsOtherUnixPathsInexactly) {
    auto windows = compat::convert_path("/opt/tool/bin", OsKind::linux, OsKind::windows);
    ASSERT_TRUE(windows);
    EXPECT_FALSE(windows->exact);
    EXPECT_FALSE(windows->note.empty());
}

TEST(Compat, RelativeAndSameOsPathsAreUnchanged) {
    auto relative = compat::convert_path("cache/data", OsKind::windows, OsKind::linux);
    ASSERT_TRUE(relative);
    EXPECT_EQ(relative->path, "cache/data");
    EXPECT_TRUE(relative->exact);

    auto same = compat::convert_path("C:\\Users\\alice", OsKind::windows, OsKind::windows);
    ASSERT_TRUE(same);
    EXPECT_EQ(same->path, "C:\\Users\\alice");
    EXPECT_TRUE(same->exact);

    auto unix_same = compat::convert_path("/home/alice", OsKind::linux, OsKind::linux);
    ASSERT_TRUE(unix_same);
    EXPECT_EQ(unix_same->path, "/home/alice");
}

TEST(Compat, RejectsUnknownOperatingSystem) {
    auto result = compat::convert_path("C:\\Users\\alice", OsKind::unknown, OsKind::linux);
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code, ErrorCode::validation);
}

}
}
