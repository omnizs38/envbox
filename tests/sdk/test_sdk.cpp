#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "common/test_support.hpp"
#include "envbox/core/os.hpp"
#include "envbox/core/profile.hpp"
#include "envbox/engine/capture.hpp"
#include "envbox/engine/restore.hpp"
#include "envbox/sdk/loader.hpp"

namespace envbox::sdk {
namespace {

namespace fs = std::filesystem;
using test::EnvGuard;
using test::FakeHome;
using test::read_text_file;
using test::write_text_file;

fs::path example_library() {
    return fs::path(ENVBOX_EXAMPLE_ADAPTER_PATH);
}

TEST(SdkLoader, LoadsExampleAdapter) {
    auto loaded = load_dynamic_adapter(example_library());
    ASSERT_TRUE(loaded) << loaded.error().message;
    EXPECT_EQ(loaded->adapter->id(), "example");
    EXPECT_EQ(loaded->adapter->adapter_version(), 1u);
}

TEST(SdkLoader, RejectsMissingLibrary) {
    auto loaded = load_dynamic_adapter(fs::temp_directory_path() / "envbox-sdk-does-not-exist.dll");
    ASSERT_FALSE(loaded);
    EXPECT_EQ(loaded.error().code, ErrorCode::not_found);
}

TEST(SdkLoader, RejectsLibraryWithoutEntryPoint) {
    auto loaded = load_dynamic_adapter(fs::path(ENVBOX_MISSING_SYMBOL_ADAPTER_PATH));
    ASSERT_FALSE(loaded);
    EXPECT_EQ(loaded.error().code, ErrorCode::format);
}

TEST(SdkLoader, RejectsIncompatibleAbiVersion) {
    auto loaded = load_dynamic_adapter(fs::path(ENVBOX_BAD_ABI_ADAPTER_PATH));
    ASSERT_FALSE(loaded);
    EXPECT_EQ(loaded.error().code, ErrorCode::unsupported);
}

TEST(SdkAdapter, CaptureThroughCAbi) {
    FakeHome home;
    EnvGuard env;
    home.wire(env);
    write_text_file(home.path() / ".example" / "config.json", R"({"enabled": true})");

    auto loaded = load_dynamic_adapter(example_library());
    ASSERT_TRUE(loaded);

    auto captured = loaded->adapter->capture();
    ASSERT_TRUE(captured) << captured.error().message;
    EXPECT_EQ(captured->spec.id, "example");
    ASSERT_EQ(captured->files.size(), 1u);
    EXPECT_EQ(captured->files[0].path, "applications/example/config.json");
    const std::string content(reinterpret_cast<const char*>(captured->files[0].data.data()), captured->files[0].data.size());
    EXPECT_EQ(content, R"({"enabled": true})");
}

TEST(SdkAdapter, ResolveThroughCAbi) {
    FakeHome home;
    EnvGuard env;
    home.wire(env);

    auto loaded = load_dynamic_adapter(example_library());
    ASSERT_TRUE(loaded);

    auto resolutions = loaded->adapter->resolve_targets({"applications/example/config.json"});
    ASSERT_TRUE(resolutions) << resolutions.error().message;
    ASSERT_EQ(resolutions->size(), 1u);
    EXPECT_EQ((*resolutions)[0].target, home.path() / ".example" / "config.json");
}

TEST(SdkAdapter, UnknownPayloadMemberIsRejected) {
    FakeHome home;
    EnvGuard env;
    home.wire(env);

    auto loaded = load_dynamic_adapter(example_library());
    ASSERT_TRUE(loaded);

    auto resolutions = loaded->adapter->resolve_targets({"applications/example/other.json"});
    ASSERT_FALSE(resolutions);
    EXPECT_EQ(resolutions.error().code, ErrorCode::validation);
}

TEST(SdkAdapter, CaptureFailurePropagatesStatusAndMessage) {
    FakeHome home;
    EnvGuard env;
    home.wire(env);

    auto loaded = load_dynamic_adapter(example_library());
    ASSERT_TRUE(loaded);

    auto captured = loaded->adapter->capture();
    ASSERT_FALSE(captured);
    EXPECT_EQ(captured.error().code, ErrorCode::not_found);
    EXPECT_NE(captured.error().message.find("no example configuration"), std::string::npos);
}

TEST(SdkAdapter, EndToEndWithEngine) {
    FakeHome source;
    EnvGuard env;
    source.wire(env);
    write_text_file(source.path() / ".example" / "config.json", "from-source");
    const fs::path out = source.path() / "sdk.envbox";

    auto loaded = load_dynamic_adapter(example_library());
    ASSERT_TRUE(loaded);

    engine::AdapterRegistry capture_registry;
    capture_registry.add(loaded->adapter);
    ProfileInfo info;
    info.name = "SDK End to End";
    auto report = engine::capture_profile(out, info, capture_registry);
    ASSERT_TRUE(report) << report.error().message;
    auto profile = open_profile(out);
    ASSERT_TRUE(profile);

    FakeHome target;
    target.wire(env);
    engine::AdapterRegistry restore_registry;
    restore_registry.add(loaded->adapter);
    auto plan = engine::plan_restore(*profile, restore_registry);
    ASSERT_TRUE(plan);
    ASSERT_EQ(plan->actions.size(), 1u);
    EXPECT_TRUE(plan->conflicts.empty());

    auto result = engine::apply_restore(*plan, engine::RestoreOptions{target.path() / "backups", false, false});
    ASSERT_TRUE(result);
    EXPECT_EQ(read_text_file(target.path() / ".example" / "config.json"), "from-source");
}

}
}
