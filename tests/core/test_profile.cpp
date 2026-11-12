#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "envbox/core/container.hpp"
#include "envbox/core/profile.hpp"

namespace envbox {
namespace {

namespace fs = std::filesystem;

[[nodiscard]] std::vector<std::byte> to_bytes(std::string_view text) {
    return {reinterpret_cast<const std::byte*>(text.data()),
            reinterpret_cast<const std::byte*>(text.data() + text.size())};
}

[[nodiscard]] std::string to_string(const std::vector<std::byte>& bytes) {
    return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}

fs::path temp_file(const std::string& name) {
    static int counter = 0;
    const auto unique = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "-" +
                        std::to_string(counter++);
    return fs::temp_directory_path() / ("envbox-test-" + unique + "-" + name);
}

PayloadFile make_payload(std::string path, std::string content) {
    PayloadFile file;
    file.path = std::move(path);
    file.data = to_bytes(content);
    return file;
}

ProfileInfo sample_info() {
    ProfileInfo info;
    info.name = "Test Workstation";
    info.description = "unit test profile";
    info.tags = {"test", "ci"};
    info.created_at = "2026-11-12T10:30:00Z";
    info.source = {"windows", "x86_64"};
    info.applications = {{"vscode", "1.106.0", "vscode", 1}, {"git", "", "git", 1}};
    return info;
}

std::vector<PayloadFile> sample_payload() {
    return {make_payload("applications/vscode/settings.json", R"({"editor.fontSize": 14})"),
            make_payload("applications/vscode/keybindings.json", "[]"),
            make_payload("applications/git/config", "[core]\n\teditor = nvim\n")};
}

class ProfileTest : public ::testing::Test {
  protected:
    void TearDown() override {
        for (const auto& path : temp_paths_) {
            std::error_code ec;
            fs::remove(path, ec);
        }
    }
    fs::path temp_path_(const std::string& name) {
        auto path = temp_file(name);
        temp_paths_.push_back(path);
        return path;
    }

  private:
    std::vector<fs::path> temp_paths_;
};

TEST_F(ProfileTest, CreateOpenRoundtripPreservesEverything) {
    const fs::path path = temp_path_("roundtrip.envbox");
    ASSERT_TRUE(create_profile(path, sample_info(), sample_payload()));

    auto profile = open_profile(path);
    ASSERT_TRUE(profile);
    EXPECT_EQ(profile->info.name, "Test Workstation");
    EXPECT_EQ(profile->info.description, "unit test profile");
    EXPECT_EQ(profile->info.tags, (std::vector<std::string>{"test", "ci"}));
    EXPECT_EQ(profile->info.created_at, "2026-11-12T10:30:00Z");
    EXPECT_EQ(profile->info.source.os, "windows");
    EXPECT_EQ(profile->info.source.architecture, "x86_64");
    ASSERT_EQ(profile->info.applications.size(), 2u);
    EXPECT_EQ(profile->info.applications[0].id, "vscode");
    EXPECT_EQ(profile->info.applications[0].version, "1.106.0");
    EXPECT_EQ(profile->info.applications[1].adapter, "git");
    EXPECT_EQ(profile->info.applications[1].adapter_version, 1u);

    ASSERT_EQ(profile->payload.size(), 3u);
    EXPECT_EQ(to_string(profile->payload[0].data), "[core]\n\teditor = nvim\n");

    EXPECT_EQ(profile->manifest.files.size(), 4u);
    EXPECT_EQ(profile->manifest.format_version, kFormatVersion);
}

TEST_F(ProfileTest, CreatedProfileVerifiesAndDefaultsSource) {
    const fs::path path = temp_path_("defaults.envbox");
    ProfileInfo info;
    info.name = "Minimal";
    ASSERT_TRUE(create_profile(path, info, {}));

    auto profile = open_profile(path);
    ASSERT_TRUE(profile);
    EXPECT_FALSE(profile->info.created_at.empty());
    EXPECT_FALSE(profile->info.source.os.empty());
    EXPECT_FALSE(profile->info.source.architecture.empty());
    EXPECT_TRUE(profile->info.applications.empty());
}

TEST_F(ProfileTest, OpenDetectsPayloadTampering) {
    const fs::path path = temp_path_("tampered.envbox");
    ASSERT_TRUE(create_profile(path, sample_info(), sample_payload()));

    auto entries = read_container(path);
    ASSERT_TRUE(entries);
    auto it = std::find_if(entries->begin(), entries->end(),
                           [](const TarEntry& e) { return e.path == "applications/git/config"; });
    ASSERT_NE(it, entries->end());
    ASSERT_FALSE(it->data.empty());
    it->data[0] ^= std::byte{0xFF};
    ASSERT_TRUE(write_container(path, *entries));

    auto profile = open_profile(path);
    ASSERT_FALSE(profile);
    EXPECT_EQ(profile.error().code, ErrorCode::integrity);
}

TEST_F(ProfileTest, VerifyHashesFalseAllowsToolingAccess) {
    const fs::path path = temp_path_("tooling.envbox");
    ASSERT_TRUE(create_profile(path, sample_info(), sample_payload()));

    auto entries = read_container(path);
    ASSERT_TRUE(entries);
    auto it = std::find_if(entries->begin(), entries->end(),
                           [](const TarEntry& e) { return e.path == "applications/git/config"; });
    ASSERT_NE(it, entries->end());
    it->data[0] ^= std::byte{0xFF};
    ASSERT_TRUE(write_container(path, *entries));

    auto profile = open_profile(path, OpenOptions{.verify_hashes = false});
    ASSERT_TRUE(profile);
}

TEST_F(ProfileTest, OpenDetectsMissingIndexedFile) {
    const fs::path path = temp_path_("missing.envbox");
    ASSERT_TRUE(create_profile(path, sample_info(), sample_payload()));

    auto entries = read_container(path);
    ASSERT_TRUE(entries);
    entries->erase(std::remove_if(entries->begin(), entries->end(),
                                  [](const TarEntry& e) { return e.path == "applications/git/config"; }),
                   entries->end());
    ASSERT_TRUE(write_container(path, *entries));

    auto profile = open_profile(path);
    ASSERT_FALSE(profile);
    EXPECT_EQ(profile.error().code, ErrorCode::integrity);
}

TEST_F(ProfileTest, OpenDetectsUnindexedExtraMember) {
    const fs::path path = temp_path_("extra.envbox");
    ASSERT_TRUE(create_profile(path, sample_info(), sample_payload()));

    auto entries = read_container(path);
    ASSERT_TRUE(entries);
    entries->push_back({"assets/sneaky.bin", to_bytes("sneaky"), false});
    ASSERT_TRUE(write_container(path, *entries));

    auto profile = open_profile(path);
    ASSERT_FALSE(profile);
    EXPECT_EQ(profile.error().code, ErrorCode::integrity);
}

TEST_F(ProfileTest, OpenRejectsNewerFormatVersion) {
    const fs::path path = temp_path_("version.envbox");
    ASSERT_TRUE(create_profile(path, sample_info(), sample_payload()));

    auto entries = read_container(path);
    ASSERT_TRUE(entries);
    auto manifest_it = std::find_if(entries->begin(), entries->end(),
                                    [](const TarEntry& e) { return e.path == "manifest.json"; });
    ASSERT_NE(manifest_it, entries->end());

    auto manifest = manifest_from_json(to_string(manifest_it->data));
    ASSERT_TRUE(manifest);
    manifest->format_version = 999;
    auto manifest_json = manifest_to_json(*manifest);
    ASSERT_TRUE(manifest_json);
    manifest_it->data = to_bytes(*manifest_json);
    ASSERT_TRUE(write_container(path, *entries));

    auto profile = open_profile(path);
    ASSERT_FALSE(profile);
    EXPECT_EQ(profile.error().code, ErrorCode::unsupported);
}

TEST_F(ProfileTest, OpenRejectsMissingProfileJson) {
    const fs::path path = temp_path_("noprofile.envbox");
    ASSERT_TRUE(create_profile(path, sample_info(), sample_payload()));

    auto entries = read_container(path);
    ASSERT_TRUE(entries);
    entries->erase(std::remove_if(entries->begin(), entries->end(),
                                  [](const TarEntry& e) { return e.path == "profile.json"; }),
                   entries->end());
    ASSERT_TRUE(write_container(path, *entries));

    auto profile = open_profile(path);
    ASSERT_FALSE(profile);
    EXPECT_EQ(profile.error().code, ErrorCode::not_found);
}

TEST_F(ProfileTest, OpenRejectsUnknownSourceOs) {
    const fs::path path = temp_path_("bados.envbox");
    ASSERT_TRUE(create_profile(path, sample_info(), sample_payload()));

    auto entries = read_container(path);
    ASSERT_TRUE(entries);
    auto profile_it = std::find_if(entries->begin(), entries->end(),
                                   [](const TarEntry& e) { return e.path == "profile.json"; });
    ASSERT_NE(profile_it, entries->end());
    std::string text = to_string(profile_it->data);
    const auto pos = text.find("\"os\": \"windows\"");
    ASSERT_NE(pos, std::string::npos);
    text.replace(pos, std::string("\"os\": \"windows\"").size(), "\"os\": \"beos\"");
    profile_it->data = to_bytes(text);

    auto manifest_it = std::find_if(entries->begin(), entries->end(),
                                    [](const TarEntry& e) { return e.path == "manifest.json"; });
    ASSERT_NE(manifest_it, entries->end());
    auto manifest = manifest_from_json(to_string(manifest_it->data));
    ASSERT_TRUE(manifest);
    auto hash = blake3_hex(profile_it->data);
    ASSERT_TRUE(hash);
    for (auto& file : manifest->files) {
        if (file.path == "profile.json") {
            file.size = profile_it->data.size();
            file.blake3 = *hash;
        }
    }
    auto manifest_json = manifest_to_json(*manifest);
    ASSERT_TRUE(manifest_json);
    manifest_it->data = to_bytes(*manifest_json);
    ASSERT_TRUE(write_container(path, *entries));

    auto profile = open_profile(path);
    ASSERT_FALSE(profile);
    EXPECT_EQ(profile.error().code, ErrorCode::validation);
}

TEST_F(ProfileTest, CreateRejectsInvalidPayloadPaths) {
    const fs::path path = temp_path_("invalid.envbox");
    const auto expect_validation = [&](PayloadFile file) {
        auto result = create_profile(path, sample_info(), std::vector<PayloadFile>{file});
        ASSERT_FALSE(result);
        EXPECT_EQ(result.error().code, ErrorCode::validation);
    };
    expect_validation(make_payload("../escape", "x"));
    expect_validation(make_payload("/absolute", "x"));
    expect_validation(make_payload("etc/passwd", "x"));
    expect_validation(make_payload("applications/../manifest.json", "x"));
    expect_validation(make_payload("", "x"));
}

TEST_F(ProfileTest, CreateRejectsDuplicatePayloadPaths) {
    const fs::path path = temp_path_("dup.envbox");
    auto result = create_profile(path, sample_info(),
                                 std::vector<PayloadFile>{make_payload("system/a.json", "1"),
                                                          make_payload("system/./a.json", "2")});
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code, ErrorCode::validation);
}

TEST_F(ProfileTest, CreateRejectsEmptyName) {
    const fs::path path = temp_path_("emptyname.envbox");
    ProfileInfo info;
    info.name = "   ";
    auto result = create_profile(path, info, {});
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code, ErrorCode::validation);
}

TEST_F(ProfileTest, ManifestJsonRoundtrip) {
    Manifest manifest;
    manifest.format_version = 1;
    manifest.generator_name = "envbox";
    manifest.generator_version = "0.1.0";
    manifest.files = {{"applications/git/config", 42, std::string(64, 'a')},
                      {"profile.json", 10, std::string(64, 'b')}};

    auto json_text = manifest_to_json(manifest);
    ASSERT_TRUE(json_text);
    auto parsed = manifest_from_json(*json_text);
    ASSERT_TRUE(parsed);
    ASSERT_EQ(parsed->files.size(), 2u);
    EXPECT_EQ(parsed->files[0].path, "applications/git/config");
    EXPECT_EQ(parsed->files[0].size, 42u);
    EXPECT_EQ(parsed->files[0].blake3, std::string(64, 'a'));
    EXPECT_EQ(parsed->files[1].path, "profile.json");
}

TEST_F(ProfileTest, ManifestFromJsonRejectsBadBlake3) {
    const std::string json_text = R"({
  "format": "envbox",
  "format_version": 1,
  "files": [ { "path": "profile.json", "size": 1, "blake3": "xyz" } ]
})";
    auto parsed = manifest_from_json(json_text);
    ASSERT_FALSE(parsed);
    EXPECT_EQ(parsed.error().code, ErrorCode::format);
}

TEST_F(ProfileTest, ManifestFromJsonRejectsWrongFormatName) {
    const std::string json_text = R"({
  "format": "not-envbox",
  "format_version": 1,
  "files": []
})";
    auto parsed = manifest_from_json(json_text);
    ASSERT_FALSE(parsed);
    EXPECT_EQ(parsed.error().code, ErrorCode::format);
}

}
}
