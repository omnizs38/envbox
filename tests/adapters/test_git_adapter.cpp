#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "common/test_support.hpp"
#include "envbox/adapters/git.hpp"
#include "envbox/core/os.hpp"
#include "envbox/core/profile.hpp"
#include "envbox/engine/capture.hpp"
#include "envbox/engine/restore.hpp"

namespace envbox::engine {
namespace {

namespace fs = std::filesystem;
using test::EnvGuard;
using test::FakeHome;
using test::read_text_file;
using test::write_text_file;

constexpr const char* kGitConfig = "[user]\n\tname = Engine Test\n";

TEST(GitAdapter, CaptureDotGitconfig) {
    FakeHome home;
    EnvGuard env;
    home.wire(env);
    write_text_file(home.path() / ".gitconfig", kGitConfig);

    auto adapter = adapters::make_git_adapter();
    auto captured = adapter->capture();
    ASSERT_TRUE(captured);
    EXPECT_EQ(captured->spec.id, "git");
    EXPECT_EQ(captured->spec.adapter, "git");
    EXPECT_EQ(captured->spec.adapter_version, 1u);
    ASSERT_EQ(captured->files.size(), 1u);
    EXPECT_EQ(captured->files[0].path, "applications/git/config");
    EXPECT_EQ(read_text_file(home.path() / ".gitconfig"), kGitConfig);
}

TEST(GitAdapter, CapturePrefersXdgOverDotGitconfig) {
    FakeHome home;
    EnvGuard env;
    home.wire(env);
    write_text_file(home.path() / ".config" / "git" / "config", "[user]\n\tname = Xdg\n");
    write_text_file(home.path() / ".gitconfig", "[user]\n\tname = Dot\n");

    auto adapter = adapters::make_git_adapter();
    auto captured = adapter->capture();
    ASSERT_TRUE(captured);
    const std::string content(reinterpret_cast<const char*>(captured->files[0].data.data()),
                              captured->files[0].data.size());
    EXPECT_NE(content.find("Xdg"), std::string::npos);
}

TEST(GitAdapter, CaptureMissingConfigReportsNotFound) {
    FakeHome home;
    EnvGuard env;
    home.wire(env);

    auto adapter = adapters::make_git_adapter();
    auto captured = adapter->capture();
    ASSERT_FALSE(captured);
    EXPECT_EQ(captured.error().code, ErrorCode::not_found);
}

TEST(GitAdapter, CaptureHonorsGitConfigGlobal) {
    FakeHome home;
    EnvGuard env;
    home.wire(env);
    write_text_file(home.path() / ".gitconfig", "[user]\n\tname = Dot\n");
    write_text_file(home.path() / "custom.ini", "[user]\n\tname = Custom\n");
    env.set("GIT_CONFIG_GLOBAL", (home.path() / "custom.ini").string());

    auto adapter = adapters::make_git_adapter();
    auto captured = adapter->capture();
    ASSERT_TRUE(captured);
    const std::string content(reinterpret_cast<const char*>(captured->files[0].data.data()),
                              captured->files[0].data.size());
    EXPECT_NE(content.find("Custom"), std::string::npos);
}

TEST(GitAdapter, CaptureRejectsMissingGitConfigGlobal) {
    FakeHome home;
    EnvGuard env;
    home.wire(env);
    env.set("GIT_CONFIG_GLOBAL", (home.path() / "does-not-exist.ini").string());

    auto adapter = adapters::make_git_adapter();
    auto captured = adapter->capture();
    ASSERT_FALSE(captured);
    EXPECT_EQ(captured.error().code, ErrorCode::not_found);
}

TEST(GitAdapter, ResolveFreshInstallTargetDependsOnHostOs) {
    FakeHome home;
    EnvGuard env;
    home.wire(env);

    auto adapter = adapters::make_git_adapter();
    auto resolutions = adapter->resolve_targets({"applications/git/config"});
    ASSERT_TRUE(resolutions);
    ASSERT_EQ(resolutions->size(), 1u);
    const fs::path expected = current_os().kind == OsKind::linux ? home.path() / ".config" / "git" / "config"
                                                                  : home.path() / ".gitconfig";
    EXPECT_EQ((*resolutions)[0].target, expected);
}

TEST(GitAdapter, ResolvePrefersExistingDotGitconfig) {
    FakeHome home;
    EnvGuard env;
    home.wire(env);
    write_text_file(home.path() / ".gitconfig", kGitConfig);

    auto adapter = adapters::make_git_adapter();
    auto resolutions = adapter->resolve_targets({"applications/git/config"});
    ASSERT_TRUE(resolutions);
    ASSERT_EQ(resolutions->size(), 1u);
    EXPECT_EQ((*resolutions)[0].target, home.path() / ".gitconfig");
}

TEST(GitAdapter, ResolveRejectsUnknownPayloadMember) {
    FakeHome home;
    EnvGuard env;
    home.wire(env);

    auto adapter = adapters::make_git_adapter();
    auto resolutions = adapter->resolve_targets({"applications/git/config", "applications/git/extra.txt"});
    ASSERT_FALSE(resolutions);
    EXPECT_EQ(resolutions.error().code, ErrorCode::validation);
}

}
}
