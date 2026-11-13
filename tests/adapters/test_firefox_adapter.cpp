#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

#include "common/test_support.hpp"
#include "envbox/adapters/firefox.hpp"
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

constexpr const char* kPrefsJs = R"(// Mozilla User Preferences
user_pref("browser.startup.homepage", "https://example.org");
user_pref("network.http.max-connections", 900);
user_pref("signon.rememberSignons", true);
user_pref("gfx.webrender.all", true);
user_pref("browser.download.dir", "C:\\Users\\tester\\Downloads");
user_pref("extensions.custom.api_key", "super-secret-value");
user_pref("browser.newtabpage.enabled", false);
)";

constexpr const char* kUserJs = R"(user_pref("browser.startup.homepage", "about:blank");
user_pref("toolkit.legacyUserProfileCustomizations.stylesheets", true);
)";

fs::path firefox_root(const fs::path& home) {
    return home / "AppData" / "Roaming" / "Mozilla" / "Firefox";
}

fs::path setup_firefox(const FakeHome& home, const std::string& profile_rel, bool with_user_js) {
    const auto root = firefox_root(home.path());
    write_text_file(root / "profiles.ini",
                    "[General]\nStartWithLastProfile=1\n\n[Profile0]\nName=default\nIsRelative=1\nPath=" + profile_rel + "\nDefault=1\n");
    const auto profile_dir = root / profile_rel;
    write_text_file(profile_dir / "prefs.js", kPrefsJs);
    if (with_user_js) {
        write_text_file(profile_dir / "user.js", kUserJs);
    }
    return profile_dir;
}

std::string payload_text(const CapturedApplication& captured) {
    return {reinterpret_cast<const char*>(captured.files[0].data.data()), captured.files[0].data.size()};
}

std::size_t count_occurrences(const std::string& text, const std::string& needle) {
    std::size_t count = 0;
    std::size_t position = text.find(needle);
    while (position != std::string::npos) {
        ++count;
        position = text.find(needle, position + needle.size());
    }
    return count;
}

TEST(FirefoxAdapter, CaptureMergesAndSanitizesPrefs) {
    FakeHome home;
    EnvGuard env;
    home.wire(env);
    setup_firefox(home, "abc.default-release", true);

    auto adapter = adapters::make_firefox_adapter();
    auto captured = adapter->capture();
    ASSERT_TRUE(captured);
    ASSERT_EQ(captured->files.size(), 1u);
    EXPECT_EQ(captured->files[0].path, "applications/firefox/user.js");

    const std::string text = payload_text(*captured);
    EXPECT_NE(text.find("toolkit.legacyUserProfileCustomizations.stylesheets"), std::string::npos);
    EXPECT_NE(text.find("browser.startup.homepage\", \"about:blank\""), std::string::npos);
    EXPECT_NE(text.find("network.http.max-connections\", 900"), std::string::npos);
    EXPECT_NE(text.find("browser.newtabpage.enabled\", false"), std::string::npos);

    EXPECT_EQ(count_occurrences(text, "browser.startup.homepage"), 1u);
    EXPECT_EQ(text.find("signon.rememberSignons"), std::string::npos);
    EXPECT_EQ(text.find("gfx.webrender"), std::string::npos);
    EXPECT_EQ(text.find("browser.download.dir"), std::string::npos);
    EXPECT_EQ(text.find("api_key"), std::string::npos);
    EXPECT_EQ(text.find("super-secret-value"), std::string::npos);

    ASSERT_FALSE(captured->warnings.empty());
}

TEST(FirefoxAdapter, CaptureWithoutFirefoxDataReportsNotFound) {
    FakeHome home;
    EnvGuard env;
    home.wire(env);

    auto adapter = adapters::make_firefox_adapter();
    auto captured = adapter->capture();
    ASSERT_FALSE(captured);
    EXPECT_EQ(captured.error().code, ErrorCode::not_found);
}

TEST(FirefoxAdapter, CaptureSupportsAbsoluteProfilePath) {
    FakeHome home;
    EnvGuard env;
    home.wire(env);
    const auto absolute_profile = home.path() / "profiles" / "custom";
    write_text_file(absolute_profile / "prefs.js", R"(user_pref("browser.newtabpage.enabled", true);
)");
    write_text_file(firefox_root(home.path()) / "profiles.ini",
                    "[Profile0]\nName=custom\nIsRelative=0\nPath=" + absolute_profile.string() + "\nDefault=1\n");

    auto adapter = adapters::make_firefox_adapter();
    auto captured = adapter->capture();
    ASSERT_TRUE(captured);
    EXPECT_NE(payload_text(*captured).find("browser.newtabpage.enabled"), std::string::npos);
}

TEST(FirefoxAdapter, ResolveTargetUsesDefaultProfile) {
    FakeHome home;
    EnvGuard env;
    home.wire(env);
    const auto profile_dir = setup_firefox(home, "xyz.default-release", false);

    auto adapter = adapters::make_firefox_adapter();
    auto resolutions = adapter->resolve_targets({"applications/firefox/user.js"});
    ASSERT_TRUE(resolutions);
    ASSERT_EQ(resolutions->size(), 1u);
    EXPECT_EQ((*resolutions)[0].target, profile_dir / "user.js");
}

TEST(FirefoxAdapter, ResolveWithoutProfileReportsNotFound) {
    FakeHome home;
    EnvGuard env;
    home.wire(env);

    auto adapter = adapters::make_firefox_adapter();
    auto resolutions = adapter->resolve_targets({"applications/firefox/user.js"});
    ASSERT_FALSE(resolutions);
    EXPECT_EQ(resolutions.error().code, ErrorCode::not_found);
}

TEST(FirefoxAdapter, ResolveRejectsUnknownPayloadMember) {
    FakeHome home;
    EnvGuard env;
    home.wire(env);
    setup_firefox(home, "xyz.default-release", false);

    auto adapter = adapters::make_firefox_adapter();
    auto resolutions = adapter->resolve_targets({"applications/firefox/prefs.js"});
    ASSERT_FALSE(resolutions);
    EXPECT_EQ(resolutions.error().code, ErrorCode::validation);
}

TEST(FirefoxAdapter, CaptureThenRestoreRoundtrip) {
    FakeHome source;
    EnvGuard env;
    source.wire(env);
    setup_firefox(source, "abc.default-release", true);
    const fs::path out = source.path() / "firefox.envbox";

    AdapterRegistry capture_registry;
    capture_registry.add(adapters::make_firefox_adapter());
    ProfileInfo info;
    info.name = "Firefox Test";
    auto report = capture_profile(out, info, capture_registry);
    ASSERT_TRUE(report);
    auto profile = open_profile(out);
    ASSERT_TRUE(profile);

    FakeHome target_home;
    target_home.wire(env);
    const auto target_profile = setup_firefox(target_home, "new.default-release", false);

    AdapterRegistry registry;
    registry.add(adapters::make_firefox_adapter());
    auto plan = plan_restore(*profile, registry);
    ASSERT_TRUE(plan);
    ASSERT_EQ(plan->actions.size(), 1u);
    EXPECT_TRUE(plan->conflicts.empty());

    auto result = apply_restore(*plan, RestoreOptions{target_home.path() / "backups", false, false});
    ASSERT_TRUE(result);
    const std::string restored = read_text_file(target_profile / "user.js");
    EXPECT_NE(restored.find("network.http.max-connections"), std::string::npos);
    EXPECT_EQ(restored.find("signon."), std::string::npos);
}

}
}
