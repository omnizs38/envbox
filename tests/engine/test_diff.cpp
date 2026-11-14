#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "common/test_support.hpp"
#include "envbox/adapters/firefox.hpp"
#include "envbox/adapters/git.hpp"
#include "envbox/adapters/vscode.hpp"
#include "envbox/core/os.hpp"
#include "envbox/core/profile.hpp"
#include "envbox/engine/capture.hpp"
#include "envbox/engine/diff.hpp"
#include "envbox/engine/restore.hpp"

namespace envbox::engine {
namespace {

namespace fs = std::filesystem;
using test::EnvGuard;
using test::FakeHome;
using test::write_text_file;

constexpr const char* kCapturedConfig = "[user]\n\tname = Restore Test\n";

fs::path host_default_target(const fs::path& home) {
    return current_os().kind == OsKind::linux ? home / ".config" / "git" / "config" : home / ".gitconfig";
}

struct CapturedProfile {
    Profile profile;
};

CapturedProfile capture_git(const FakeHome& home, const fs::path& out) {
    write_text_file(home.path() / ".gitconfig", kCapturedConfig);
    AdapterRegistry registry;
    registry.add(adapters::make_git_adapter());
    ProfileInfo info;
    info.name = "Diff Test";
    auto report = capture_profile(out, info, registry);
    EXPECT_TRUE(report.has_value());
    auto profile = open_profile(out);
    EXPECT_TRUE(profile.has_value());
    return {std::move(*profile)};
}

std::vector<std::byte> bytes_of(const std::string& text) {
    return {reinterpret_cast<const std::byte*>(text.data()), reinterpret_cast<const std::byte*>(text.data() + text.size())};
}

TEST(Diff, NewFileIsReported) {
    FakeHome source;
    EnvGuard env;
    source.wire(env);
    auto captured = capture_git(source, source.path() / "profile.envbox");

    FakeHome target;
    target.wire(env);
    AdapterRegistry registry;
    registry.add(adapters::make_git_adapter());
    auto diff = diff_profile(captured.profile, registry);
    ASSERT_TRUE(diff);
    ASSERT_EQ(diff->changes.size(), 1u);
    EXPECT_EQ(diff->changes[0].kind, ChangeKind::new_file);
    EXPECT_EQ(diff->changes[0].target, host_default_target(target.path()));
    EXPECT_EQ(diff->count(ChangeKind::new_file), 1u);
}

TEST(Diff, IdenticalFileHasNoDiff) {
    FakeHome source;
    EnvGuard env;
    source.wire(env);
    auto captured = capture_git(source, source.path() / "profile.envbox");

    FakeHome target;
    target.wire(env);
    write_text_file(host_default_target(target.path()), kCapturedConfig);

    AdapterRegistry registry;
    registry.add(adapters::make_git_adapter());
    auto diff = diff_profile(captured.profile, registry);
    ASSERT_TRUE(diff);
    ASSERT_EQ(diff->changes.size(), 1u);
    EXPECT_EQ(diff->changes[0].kind, ChangeKind::identical);
    EXPECT_TRUE(diff->changes[0].diff_text.empty());
}

TEST(Diff, ModifiedTextFileProducesUnifiedDiff) {
    FakeHome source;
    EnvGuard env;
    source.wire(env);
    auto captured = capture_git(source, source.path() / "profile.envbox");

    FakeHome target;
    target.wire(env);
    write_text_file(host_default_target(target.path()), "[user]\n\tname = Old Name\n");

    AdapterRegistry registry;
    registry.add(adapters::make_git_adapter());
    auto diff = diff_profile(captured.profile, registry);
    ASSERT_TRUE(diff);
    ASSERT_EQ(diff->changes.size(), 1u);
    const auto& change = diff->changes[0];
    EXPECT_EQ(change.kind, ChangeKind::modified);
    EXPECT_TRUE(change.text);
    EXPECT_FALSE(change.diff_text.empty());
    EXPECT_NE(change.diff_text.find("@@"), std::string::npos);
    EXPECT_NE(change.diff_text.find("-	name = Old Name"), std::string::npos);
    EXPECT_NE(change.diff_text.find("+	name = Restore Test"), std::string::npos);
}

TEST(Diff, ModifiedBinaryFileHasNoTextDiff) {
    FakeHome target;
    EnvGuard env;
    target.wire(env);

    Profile profile;
    profile.info.name = "Binary Diff";
    profile.info.source = {"linux", "x86_64"};
    profile.info.applications = {{"git", "", "git", 1}};
    profile.payload = {{"applications/git/config", {std::byte{0x00}, std::byte{0x01}, std::byte{0x02}}, false}};

    write_text_file(host_default_target(target.path()), std::string("\x00\x05\x06", 3));

    AdapterRegistry registry;
    registry.add(adapters::make_git_adapter());
    auto diff = diff_profile(profile, registry);
    ASSERT_TRUE(diff);
    ASSERT_EQ(diff->changes.size(), 1u);
    EXPECT_EQ(diff->changes[0].kind, ChangeKind::modified);
    EXPECT_FALSE(diff->changes[0].text);
    EXPECT_TRUE(diff->changes[0].diff_text.empty());
}

TEST(Diff, UnresolvedPayloadIsReportedAsSkipped) {
    FakeHome source;
    EnvGuard env;
    source.wire(env);
    write_text_file(test::host_vscode_user_dir(source.path()) / "settings.json", "{}");
    write_text_file(source.path() / ".vscode" / "extensions" / "ms-python.python-2024.1.1" / "package.json", "{}");
    const fs::path out = source.path() / "vscode.envbox";

    AdapterRegistry registry;
    registry.add(adapters::make_vscode_adapter());
    ProfileInfo info;
    info.name = "Skipped Diff";
    ASSERT_TRUE(capture_profile(out, info, registry));
    auto profile = open_profile(out);
    ASSERT_TRUE(profile);

    FakeHome target;
    target.wire(env);
    auto diff = diff_profile(*profile, registry);
    ASSERT_TRUE(diff);
    ASSERT_EQ(diff->count(ChangeKind::skipped), 1u);
    bool found_extensions = false;
    for (const auto& change : diff->changes) {
        if (change.kind == ChangeKind::skipped && change.payload_path == "applications/vscode/extensions.json") {
            found_extensions = true;
        }
    }
    EXPECT_TRUE(found_extensions);
}

TEST(Diff, MissingAdapterIsReported) {
    Profile profile;
    profile.info.name = "Missing";
    profile.info.source = {"linux", "x86_64"};
    profile.info.applications = {{"bogus", "", "bogus", 1}};
    profile.payload = {{"applications/bogus/config", bytes_of("data"), false}};

    AdapterRegistry registry;
    auto diff = diff_profile(profile, registry);
    ASSERT_TRUE(diff);
    ASSERT_EQ(diff->missing.size(), 1u);
    EXPECT_EQ(diff->missing[0].first, "bogus");
    ASSERT_EQ(diff->changes.size(), 1u);
    EXPECT_EQ(diff->changes[0].kind, ChangeKind::skipped);
    EXPECT_EQ(diff->changes[0].payload_path, "applications/bogus/config");
}

TEST(Diff, IncludeIdenticalFalseFiltersIdenticalFiles) {
    FakeHome source;
    EnvGuard env;
    source.wire(env);
    auto captured = capture_git(source, source.path() / "profile.envbox");

    FakeHome target;
    target.wire(env);
    write_text_file(host_default_target(target.path()), kCapturedConfig);

    AdapterRegistry registry;
    registry.add(adapters::make_git_adapter());
    DiffOptions options;
    options.include_identical = false;
    auto diff = diff_profile(captured.profile, registry, options);
    ASSERT_TRUE(diff);
    EXPECT_TRUE(diff->changes.empty());
}

TEST(Diff, MaxDiffLinesTruncatesOutput) {
    FakeHome target;
    EnvGuard env;
    target.wire(env);

    std::string current;
    std::string incoming;
    for (int i = 0; i < 30; ++i) {
        current += "line " + std::to_string(i) + "\n";
        incoming += "changed " + std::to_string(i) + "\n";
    }

    Profile profile;
    profile.info.name = "Truncated";
    profile.info.source = {"linux", "x86_64"};
    profile.info.applications = {{"git", "", "git", 1}};
    profile.payload = {{"applications/git/config", bytes_of(incoming), false}};

    write_text_file(host_default_target(target.path()), current);

    AdapterRegistry registry;
    registry.add(adapters::make_git_adapter());
    DiffOptions options;
    options.max_diff_lines = 10;
    auto diff = diff_profile(profile, registry, options);
    ASSERT_TRUE(diff);
    ASSERT_EQ(diff->changes.size(), 1u);
    EXPECT_TRUE(diff->changes[0].truncated);
}

} // namespace
} // namespace envbox::engine
