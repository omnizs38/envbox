#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "common/test_support.hpp"
#include "envbox/adapters/git.hpp"
#include "envbox/adapters/vscode.hpp"
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

constexpr const char* kCapturedConfig = "[user]\n\tname = Restore Test\n";

fs::path host_default_target(const fs::path& home) {
    return current_os().kind == OsKind::linux ? home / ".config" / "git" / "config" : home / ".gitconfig";
}

struct CapturedProfile {
    fs::path profile_path;
    Profile profile;
};

CapturedProfile capture_git_profile(const FakeHome& home, const fs::path& out) {
    write_text_file(home.path() / ".gitconfig", kCapturedConfig);
    AdapterRegistry registry;
    registry.add(adapters::make_git_adapter());
    ProfileInfo info;
    info.name = "Engine Test Profile";
    auto report = capture_profile(out, info, registry);
    EXPECT_TRUE(report.has_value());
    auto profile = open_profile(out);
    EXPECT_TRUE(profile.has_value());
    return {out, std::move(*profile)};
}

TEST(Engine, CaptureProducesValidProfile) {
    FakeHome home;
    EnvGuard env;
    home.wire(env);
    const fs::path out = home.path() / "captured.envbox";

    write_text_file(home.path() / ".gitconfig", kCapturedConfig);
    AdapterRegistry registry;
    registry.add(adapters::make_git_adapter());
    ProfileInfo info;
    info.name = "Engine Test Profile";
    auto report = capture_profile(out, info, registry);
    ASSERT_TRUE(report);
    EXPECT_EQ(report->captured, std::vector<std::string>{"git"});
    EXPECT_TRUE(report->skipped.empty());
    ASSERT_TRUE(fs::exists(out));

    auto profile = open_profile(out);
    ASSERT_TRUE(profile);
    ASSERT_EQ(profile->info.applications.size(), 1u);
    EXPECT_EQ(profile->info.applications[0].id, "git");
    EXPECT_EQ(profile->info.applications[0].adapter, "git");
    ASSERT_EQ(profile->payload.size(), 1u);
    EXPECT_EQ(profile->payload[0].path, "applications/git/config");
}

TEST(Engine, CaptureFailsWhenNothingIsCapturable) {
    FakeHome home;
    EnvGuard env;
    home.wire(env);
    const fs::path out = home.path() / "nothing.envbox";

    AdapterRegistry registry;
    registry.add(adapters::make_git_adapter());
    ProfileInfo info;
    info.name = "Empty Profile";
    auto report = capture_profile(out, info, registry);
    ASSERT_FALSE(report);
    EXPECT_EQ(report.error().code, ErrorCode::not_found);
    EXPECT_FALSE(fs::exists(out));
}

TEST(Engine, RestoreIntoFreshHomePlacesConfigAtHostDefault) {
    FakeHome source;
    EnvGuard env;
    source.wire(env);
    const fs::path out = source.path() / "captured.envbox";
    auto captured = capture_git_profile(source, out);

    FakeHome target_home;
    target_home.wire(env);
    AdapterRegistry registry;
    registry.add(adapters::make_git_adapter());

    auto plan = plan_restore(captured.profile, registry);
    ASSERT_TRUE(plan);
    ASSERT_EQ(plan->actions.size(), 1u);
    EXPECT_TRUE(plan->conflicts.empty());
    EXPECT_TRUE(plan->missing.empty());
    EXPECT_EQ(plan->actions[0].target, host_default_target(target_home.path()));

    const fs::path backup_root = target_home.path() / "backups";
    auto result = apply_restore(*plan, RestoreOptions{backup_root, false, false});
    ASSERT_TRUE(result);
    const fs::path expected = host_default_target(target_home.path());
    ASSERT_TRUE(fs::exists(expected));
    EXPECT_EQ(read_text_file(expected), kCapturedConfig);
}

TEST(Engine, RestoreWithConflictRefusesWithoutForce) {
    FakeHome source;
    EnvGuard env;
    source.wire(env);
    const fs::path out = source.path() / "captured.envbox";
    auto captured = capture_git_profile(source, out);

    FakeHome target_home;
    target_home.wire(env);
    const fs::path target = host_default_target(target_home.path());
    write_text_file(target, "[user]\n\tname = PreExisting\n");

    AdapterRegistry registry;
    registry.add(adapters::make_git_adapter());
    auto plan = plan_restore(captured.profile, registry);
    ASSERT_TRUE(plan);
    ASSERT_EQ(plan->conflicts.size(), 1u);

    const fs::path backup_root = target_home.path() / "backups";
    auto refused = apply_restore(*plan, RestoreOptions{backup_root, false, false});
    ASSERT_FALSE(refused);
    EXPECT_EQ(refused.error().code, ErrorCode::conflict);
    EXPECT_EQ(read_text_file(target), "[user]\n\tname = PreExisting\n");
}

TEST(Engine, RestoreWithForceBacksUpOriginal) {
    FakeHome source;
    EnvGuard env;
    source.wire(env);
    const fs::path out = source.path() / "captured.envbox";
    auto captured = capture_git_profile(source, out);

    FakeHome target_home;
    target_home.wire(env);
    const fs::path target = host_default_target(target_home.path());
    write_text_file(target, "[user]\n\tname = PreExisting\n");

    AdapterRegistry registry;
    registry.add(adapters::make_git_adapter());
    auto plan = plan_restore(captured.profile, registry);
    ASSERT_TRUE(plan);
    ASSERT_EQ(plan->conflicts.size(), 1u);

    const fs::path backup_root = target_home.path() / "backups";
    auto result = apply_restore(*plan, RestoreOptions{backup_root, true, false});
    ASSERT_TRUE(result);
    EXPECT_EQ(read_text_file(target), kCapturedConfig);
    ASSERT_FALSE(result->backup_dir.empty());
    ASSERT_TRUE(fs::exists(result->backup_dir / "backup.json"));
    EXPECT_NE(read_text_file(result->backup_dir / "backup.json").find("envbox-restore-backup"), std::string::npos);

    bool found_original = false;
    for (const auto& entry : fs::directory_iterator(result->backup_dir)) {
        if (entry.path().filename() == "backup.json") {
            continue;
        }
        if (read_text_file(entry.path()) == "[user]\n\tname = PreExisting\n") {
            found_original = true;
        }
    }
    EXPECT_TRUE(found_original);
}

TEST(Engine, DryRunDoesNotTouchDisk) {
    FakeHome source;
    EnvGuard env;
    source.wire(env);
    const fs::path out = source.path() / "captured.envbox";
    auto captured = capture_git_profile(source, out);

    FakeHome target_home;
    target_home.wire(env);
    const fs::path target = host_default_target(target_home.path());
    write_text_file(target, "[user]\n\tname = PreExisting\n");

    AdapterRegistry registry;
    registry.add(adapters::make_git_adapter());
    auto plan = plan_restore(captured.profile, registry);
    ASSERT_TRUE(plan);

    const fs::path backup_root = target_home.path() / "backups";
    auto result = apply_restore(*plan, RestoreOptions{backup_root, false, true});
    ASSERT_TRUE(result);
    EXPECT_EQ(read_text_file(target), "[user]\n\tname = PreExisting\n");
    EXPECT_FALSE(fs::exists(backup_root));
    ASSERT_EQ(result->applied.size(), 1u);
}

TEST(Engine, PlanReportsMissingAdapter) {
    Profile profile;
    profile.info.name = "Bogus Profile";
    profile.info.source = {"linux", "x86_64"};
    profile.info.applications = {{"bogus", "", "bogus-adapter", 1}};
    profile.payload = {{"applications/bogus/config", {std::byte{'x'}}, false}};

    AdapterRegistry registry;
    auto plan = plan_restore(profile, registry);
    ASSERT_TRUE(plan);
    ASSERT_EQ(plan->missing.size(), 1u);
    EXPECT_EQ(plan->missing[0].first, "bogus-adapter");
    EXPECT_TRUE(plan->actions.empty());
}

TEST(Engine, PlanWarnsAboutUnmanagedPayloadRoots) {
    Profile profile;
    profile.info.name = "System Profile";
    profile.info.source = {"linux", "x86_64"};
    profile.payload = {{"system/environment.json", {std::byte{0x7B}, std::byte{0x7D}}, false}};

    AdapterRegistry registry;
    auto plan = plan_restore(profile, registry);
    ASSERT_TRUE(plan);
    ASSERT_EQ(plan->warnings.size(), 1u);
    EXPECT_NE(plan->warnings[0].find("system/environment.json"), std::string::npos);
    EXPECT_TRUE(plan->actions.empty());
}

TEST(Engine, PlanActionsCarryAdaptedBytes) {
    FakeHome source;
    EnvGuard env;
    source.wire(env);
    const fs::path out = source.path() / "captured.envbox";
    auto captured = capture_git_profile(source, out);

    AdapterRegistry registry;
    registry.add(adapters::make_git_adapter());
    auto plan = plan_restore(captured.profile, registry);
    ASSERT_TRUE(plan);
    EXPECT_TRUE(plan->warnings.empty());
    ASSERT_EQ(plan->actions.size(), 1u);
    EXPECT_FALSE(plan->actions[0].adapted);
    EXPECT_EQ(plan->actions[0].data.size(), std::char_traits<char>::length(kCapturedConfig));
}

TEST(Engine, CaptureOnlySelectedAdapters) {
    FakeHome home;
    EnvGuard env;
    home.wire(env);
    write_text_file(home.path() / ".gitconfig", kCapturedConfig);
    const fs::path out = home.path() / "scoped.envbox";

    AdapterRegistry registry;
    registry.add(adapters::make_git_adapter());
    registry.add(adapters::make_vscode_adapter());
    ProfileInfo info;
    info.name = "Scoped Capture";
    CaptureOptions options;
    options.only_adapters = {"git"};

    auto report = capture_profile(out, info, registry, options);
    ASSERT_TRUE(report) << report.error().message;
    EXPECT_EQ(report->captured, std::vector<std::string>{"git"});
    EXPECT_TRUE(report->skipped.empty());

    auto profile = open_profile(out);
    ASSERT_TRUE(profile);
    ASSERT_EQ(profile->info.applications.size(), 1u);
    EXPECT_EQ(profile->info.applications[0].id, "git");
}

TEST(Engine, CaptureRejectsUnregisteredSelectedAdapter) {
    FakeHome home;
    EnvGuard env;
    home.wire(env);
    write_text_file(home.path() / ".gitconfig", kCapturedConfig);
    const fs::path out = home.path() / "scoped.envbox";

    AdapterRegistry registry;
    registry.add(adapters::make_git_adapter());
    ProfileInfo info;
    info.name = "Bad Selection";
    CaptureOptions options;
    options.only_adapters = {"firefox"};

    auto report = capture_profile(out, info, registry, options);
    ASSERT_FALSE(report);
    EXPECT_EQ(report.error().code, ErrorCode::not_found);
    EXPECT_NE(report.error().message.find("not registered"), std::string::npos);
}

TEST(Engine, PlanOnlySelectedApplications) {
    Profile profile;
    profile.info.name = "Scoped Plan";
    profile.info.source = {"linux", "x86_64"};
    profile.info.applications = {{"git", "", "git", 1}, {"vscode", "", "vscode", 1}};
    profile.payload = {
        {"applications/git/config", {std::byte{'a'}}, false},
        {"applications/vscode/settings.json", {std::byte{'{'}, std::byte{'}'}}, false},
    };

    FakeHome home;
    EnvGuard env;
    home.wire(env);
    AdapterRegistry registry;
    registry.add(adapters::make_git_adapter());
    registry.add(adapters::make_vscode_adapter());

    PlanOptions options;
    options.only_applications = {"git"};
    auto plan = plan_restore(profile, registry, options);
    ASSERT_TRUE(plan);
    ASSERT_EQ(plan->actions.size(), 1u);
    EXPECT_EQ(plan->actions[0].application_id, "git");
    bool warned = false;
    for (const auto& warning : plan->warnings) {
        if (warning.find("vscode") != std::string::npos && warning.find("not selected") != std::string::npos) {
            warned = true;
        }
    }
    EXPECT_TRUE(warned);
}

TEST(Engine, PlanWarnsAboutUnknownSelectedApplication) {
    Profile profile;
    profile.info.name = "Unknown Selection";
    profile.info.source = {"linux", "x86_64"};
    profile.info.applications = {{"git", "", "git", 1}};
    profile.payload = {{"applications/git/config", {std::byte{'a'}}, false}};

    FakeHome home;
    EnvGuard env;
    home.wire(env);
    AdapterRegistry registry;
    registry.add(adapters::make_git_adapter());

    PlanOptions options;
    options.only_applications = {"does-not-exist"};
    auto plan = plan_restore(profile, registry, options);
    ASSERT_TRUE(plan);
    EXPECT_TRUE(plan->actions.empty());
    bool warned = false;
    for (const auto& warning : plan->warnings) {
        if (warning.find("does-not-exist") != std::string::npos) {
            warned = true;
        }
    }
    EXPECT_TRUE(warned);
}

TEST(Engine, PlanWarnsWhenApplicationHasNoPayload) {
    Profile profile;
    profile.info.name = "Empty App Profile";
    profile.info.source = {"linux", "x86_64"};
    profile.info.applications = {{"git", "", "git", 1}};

    AdapterRegistry registry;
    registry.add(adapters::make_git_adapter());
    auto plan = plan_restore(profile, registry);
    ASSERT_TRUE(plan);
    ASSERT_EQ(plan->warnings.size(), 1u);
    EXPECT_TRUE(plan->actions.empty());
    EXPECT_TRUE(plan->missing.empty());
}

}
}
