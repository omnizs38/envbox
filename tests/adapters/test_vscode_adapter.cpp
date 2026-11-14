#include <gtest/gtest.h>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

#include "common/test_support.hpp"
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

constexpr const char* kSettings = R"({
    // Envbox test settings
    "terminal.integrated.defaultProfile.windows": "PowerShell",
    "editor.fontSize": 14,
    "git.path": "C:\\tools\\git\\bin\\git.exe",
    "terminal.integrated.fontFamily": "Cascadia Mono",
})";

constexpr const char* kKeybindings = R"([
    { "key": "ctrl+f", "command": "actions.find" }
])";

fs::path vscode_user_dir(const fs::path& home) {
    return test::host_vscode_user_dir(home);
}

void setup_vscode_source(const FakeHome& home) {
    const auto user_dir = vscode_user_dir(home.path());
    write_text_file(user_dir / "settings.json", kSettings);
    write_text_file(user_dir / "keybindings.json", kKeybindings);
    write_text_file(user_dir / "snippets" / "widgets.code-snippets", R"({"x": {"prefix": "x"}})");
    write_text_file(home.path() / ".vscode" / "extensions" / "ms-python.python-2024.1.1" / "package.json", "{}");
    write_text_file(home.path() / ".vscode" / "extensions" / "ms-vscode.cpptools-1.23.4-universal" / "package.json", "{}");
    write_text_file(home.path() / ".vscode" / "extensions" / "not-an-extension" / "package.json", "{}");
}

struct CapturedVscode {
    fs::path profile_path;
    Profile profile;
};

std::optional<CapturedVscode> capture_vscode(const FakeHome&, const fs::path& out) {
    AdapterRegistry registry;
    registry.add(adapters::make_vscode_adapter());
    ProfileInfo info;
    info.name = "VS Code Test";
    info.source = {"windows", "x86_64"};
    auto report = capture_profile(out, info, registry);
    if (!report) {
        ADD_FAILURE() << "capture failed: " << report.error().message;
        return std::nullopt;
    }
    auto profile = open_profile(out);
    if (!profile) {
        ADD_FAILURE() << "open failed: " << profile.error().message;
        return std::nullopt;
    }
    return CapturedVscode{out, std::move(*profile)};
}

TEST(VscodeAdapter, CaptureNothingReportsNotFound) {
    FakeHome home;
    EnvGuard env;
    home.wire(env);

    auto adapter = adapters::make_vscode_adapter();
    auto captured = adapter->capture();
    ASSERT_FALSE(captured);
    EXPECT_EQ(captured.error().code, ErrorCode::not_found);
}

TEST(VscodeAdapter, CaptureSettingsKeybindingsSnippetsAndExtensions) {
    FakeHome home;
    EnvGuard env;
    home.wire(env);
    setup_vscode_source(home);

    auto adapter = adapters::make_vscode_adapter();
    auto captured = adapter->capture();
    ASSERT_TRUE(captured);
    EXPECT_EQ(captured->spec.id, "vscode");

    std::vector<std::string> paths;
    for (const auto& file : captured->files) {
        paths.push_back(file.path);
    }
    EXPECT_NE(std::find(paths.begin(), paths.end(), "applications/vscode/settings.json"), paths.end());
    EXPECT_NE(std::find(paths.begin(), paths.end(), "applications/vscode/keybindings.json"), paths.end());
    EXPECT_NE(std::find(paths.begin(), paths.end(), "applications/vscode/snippets/widgets.code-snippets"), paths.end());
    EXPECT_NE(std::find(paths.begin(), paths.end(), "applications/vscode/extensions.json"), paths.end());

    const std::vector<std::byte>* extensions = nullptr;
    for (const auto& file : captured->files) {
        if (file.path == "applications/vscode/extensions.json") {
            extensions = &file.data;
        }
    }
    ASSERT_NE(extensions, nullptr);
    const std::string manifest(reinterpret_cast<const char*>(extensions->data()), extensions->size());
    EXPECT_NE(manifest.find("ms-python.python"), std::string::npos);
    EXPECT_NE(manifest.find("2024.1.1"), std::string::npos);
    EXPECT_NE(manifest.find("1.23.4"), std::string::npos);
    EXPECT_EQ(manifest.find("not-an-extension"), std::string::npos);
}

TEST(VscodeAdapter, PlanWarnsAboutWindowsToLinuxTerminalProfile) {
    FakeHome home;
    EnvGuard env;
    home.wire(env);
    setup_vscode_source(home);
    const fs::path out = home.path() / "vscode.envbox";
    auto captured_holder = capture_vscode(home, out);
    ASSERT_TRUE(captured_holder);

    PlanOptions options;
    options.target_os_override = OsKind::linux;
    AdapterRegistry registry;
    registry.add(adapters::make_vscode_adapter());
    auto plan = plan_restore(captured_holder->profile, registry, options);
    ASSERT_TRUE(plan);

    std::string all_warnings;
    for (const auto& warning : plan->warnings) {
        all_warnings += warning;
        all_warnings += '\n';
    }
    EXPECT_NE(all_warnings.find("no terminal profile configured for linux"), std::string::npos);
    EXPECT_NE(all_warnings.find("PowerShell"), std::string::npos);
    EXPECT_NE(all_warnings.find("bash"), std::string::npos);
    EXPECT_NE(all_warnings.find("Windows path"), std::string::npos);
    EXPECT_NE(all_warnings.find("Cascadia"), std::string::npos);

    const RestoreAction* settings_action = nullptr;
    for (const auto& action : plan->actions) {
        if (action.payload_path == "applications/vscode/settings.json") {
            settings_action = &action;
        }
    }
    ASSERT_NE(settings_action, nullptr);
    EXPECT_FALSE(settings_action->adapted);
    EXPECT_EQ(settings_action->data.size(), std::strlen(kSettings));
}

TEST(VscodeAdapter, WindowsTargetDoesNotFlagWindowsValues) {
    FakeHome home;
    EnvGuard env;
    home.wire(env);
    setup_vscode_source(home);
    const fs::path out = home.path() / "vscode.envbox";
    auto captured_holder = capture_vscode(home, out);
    ASSERT_TRUE(captured_holder);

    PlanOptions options;
    options.target_os_override = OsKind::windows;
    AdapterRegistry registry;
    registry.add(adapters::make_vscode_adapter());
    auto plan = plan_restore(captured_holder->profile, registry, options);
    ASSERT_TRUE(plan);

    std::string all_warnings;
    for (const auto& warning : plan->warnings) {
        all_warnings += warning;
        all_warnings += '\n';
    }
    EXPECT_EQ(all_warnings.find("Windows path"), std::string::npos);
    EXPECT_EQ(all_warnings.find("Cascadia"), std::string::npos);
    EXPECT_EQ(all_warnings.find("no terminal profile configured for windows"), std::string::npos);
}

TEST(VscodeAdapter, UrlsAreNotFlaggedAsWindowsPaths) {
    FakeHome home;
    EnvGuard env;
    home.wire(env);
    const auto user_dir = vscode_user_dir(home.path());
    write_text_file(user_dir / "settings.json", R"({
    "http.proxy": "https://proxy.example.com:8080",
    "editor.fontFamily": "Fira Code"
})");
    const fs::path out = home.path() / "vscode.envbox";
    auto captured_holder = capture_vscode(home, out);
    ASSERT_TRUE(captured_holder);

    PlanOptions options;
    options.target_os_override = OsKind::linux;
    AdapterRegistry registry;
    registry.add(adapters::make_vscode_adapter());
    auto plan = plan_restore(captured_holder->profile, registry, options);
    ASSERT_TRUE(plan);

    std::string all_warnings;
    for (const auto& warning : plan->warnings) {
        all_warnings += warning;
        all_warnings += '\n';
    }
    EXPECT_EQ(all_warnings.find("references a Windows path"), std::string::npos);
}

TEST(VscodeAdapter, PlanSkipsExtensionsJsonWithWarning) {
    FakeHome home;
    EnvGuard env;
    home.wire(env);
    setup_vscode_source(home);
    const fs::path out = home.path() / "vscode.envbox";
    auto captured_holder = capture_vscode(home, out);
    ASSERT_TRUE(captured_holder);

    AdapterRegistry registry;
    registry.add(adapters::make_vscode_adapter());
    auto plan = plan_restore(captured_holder->profile, registry);
    ASSERT_TRUE(plan);

    std::string all_warnings;
    for (const auto& warning : plan->warnings) {
        all_warnings += warning;
        all_warnings += '\n';
    }
    EXPECT_NE(all_warnings.find("applications/vscode/extensions.json"), std::string::npos);

    std::vector<std::string> resolved;
    for (const auto& action : plan->actions) {
        resolved.push_back(action.payload_path);
    }
    EXPECT_NE(std::find(resolved.begin(), resolved.end(), "applications/vscode/settings.json"), resolved.end());
    EXPECT_EQ(std::find(resolved.begin(), resolved.end(), "applications/vscode/extensions.json"), resolved.end());
}

TEST(VscodeAdapter, RestoreWritesConfigIntoFreshHome) {
    FakeHome source;
    EnvGuard env;
    source.wire(env);
    setup_vscode_source(source);
    const fs::path out = source.path() / "vscode.envbox";
    auto captured_holder = capture_vscode(source, out);
    ASSERT_TRUE(captured_holder);

    FakeHome target_home;
    target_home.wire(env);

    AdapterRegistry registry;
    registry.add(adapters::make_vscode_adapter());
    auto plan = plan_restore(captured_holder->profile, registry);
    ASSERT_TRUE(plan);
    EXPECT_TRUE(plan->conflicts.empty());

    const fs::path backup_root = target_home.path() / "backups";
    auto result = apply_restore(*plan, RestoreOptions{backup_root, false, false});
    ASSERT_TRUE(result);

    const auto target_dir = vscode_user_dir(target_home.path());
    EXPECT_EQ(read_text_file(target_dir / "settings.json"), kSettings);
    EXPECT_EQ(read_text_file(target_dir / "keybindings.json"), kKeybindings);
    EXPECT_TRUE(fs::exists(target_dir / "snippets" / "widgets.code-snippets"));
}

TEST(VscodeAdapter, RestoreConflictRefusesWithoutForce) {
    FakeHome source;
    EnvGuard env;
    source.wire(env);
    setup_vscode_source(source);
    const fs::path out = source.path() / "vscode.envbox";
    auto captured_holder = capture_vscode(source, out);
    ASSERT_TRUE(captured_holder);

    FakeHome target_home;
    target_home.wire(env);
    const auto target_settings = vscode_user_dir(target_home.path()) / "settings.json";
    write_text_file(target_settings, R"({"editor.fontSize": 20})");

    AdapterRegistry registry;
    registry.add(adapters::make_vscode_adapter());
    auto plan = plan_restore(captured_holder->profile, registry);
    ASSERT_TRUE(plan);
    ASSERT_EQ(plan->conflicts.size(), 1u);

    const fs::path backup_root = target_home.path() / "backups";
    auto refused = apply_restore(*plan, RestoreOptions{backup_root, false, false});
    ASSERT_FALSE(refused);
    EXPECT_EQ(refused.error().code, ErrorCode::conflict);
    EXPECT_EQ(read_text_file(target_settings), R"({"editor.fontSize": 20})");
}

}
}
