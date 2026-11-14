#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "envbox/core/profile.hpp"
#include "envbox/engine/migration.hpp"

namespace envbox::engine {
namespace {

Profile make_profile() {
    Profile profile;
    profile.info.name = "Migration Test";
    profile.info.source = {"linux", "x86_64"};
    profile.info.applications = {{"git", "", "git", 1}};
    profile.payload = {{"applications/git/config", {std::byte{'a'}, std::byte{'b'}}, false}};
    profile.manifest.format_version = 1;
    profile.manifest.files = {{"applications/git/config", 2, std::string(64, 'a')},
                              {"profile.json", 10, std::string(64, 'b')}};
    return profile;
}

TEST(Migration, MigrateToCurrentVersionIsNoOp) {
    MigrationRegistry registry = default_migrations();
    auto migrated = registry.migrate(make_profile(), 1);
    ASSERT_TRUE(migrated) << migrated.error().message;
    EXPECT_EQ(migrated->manifest.format_version, 1u);
    EXPECT_EQ(migrated->info.name, "Migration Test");
    ASSERT_EQ(migrated->payload.size(), 1u);
}

TEST(Migration, NewerVersionWithoutStepIsUnsupported) {
    MigrationRegistry registry = default_migrations();
    auto migrated = registry.migrate(make_profile(), 2);
    ASSERT_FALSE(migrated);
    EXPECT_EQ(migrated.error().code, ErrorCode::unsupported);
}

TEST(Migration, DowngradeIsRejected) {
    Profile profile = make_profile();
    profile.manifest.format_version = 2;
    MigrationRegistry registry = default_migrations();
    auto migrated = registry.migrate(profile, 1);
    ASSERT_FALSE(migrated);
    EXPECT_EQ(migrated.error().code, ErrorCode::validation);
}

TEST(Migration, StepChainAppliesTransformsAndReindexes) {
    MigrationRegistry registry;
    registry.add({1, 2, [](Profile profile) -> Result<Profile> {
                      profile.info.description = "migrated to two";
                      return profile;
                  }});
    registry.add({2, 3, [](Profile profile) -> Result<Profile> {
                      profile.info.tags.push_back("three");
                      return profile;
                  }});

    auto migrated = registry.migrate(make_profile(), 3);
    ASSERT_TRUE(migrated) << migrated.error().message;
    EXPECT_EQ(migrated->manifest.format_version, 3u);
    EXPECT_EQ(migrated->info.description, "migrated to two");
    ASSERT_EQ(migrated->info.tags.size(), 1u);
    EXPECT_EQ(migrated->info.tags[0], "three");

    ASSERT_EQ(migrated->manifest.files.size(), 2u);
    const auto payload_entry = std::find_if(migrated->manifest.files.begin(), migrated->manifest.files.end(),
                                            [](const auto& entry) { return entry.path == "applications/git/config"; });
    ASSERT_NE(payload_entry, migrated->manifest.files.end());
    EXPECT_EQ(payload_entry->size, 2u);
    auto expected_hash = blake3_hex(migrated->payload[0].data);
    ASSERT_TRUE(expected_hash);
    EXPECT_EQ(payload_entry->blake3, *expected_hash);
    EXPECT_EQ(payload_entry->blake3.size(), 64u);
}

TEST(Migration, MissingIntermediateStepIsUnsupported) {
    MigrationRegistry registry;
    registry.add({2, 3, [](Profile profile) -> Result<Profile> { return profile; }});
    auto migrated = registry.migrate(make_profile(), 3);
    ASSERT_FALSE(migrated);
    EXPECT_EQ(migrated.error().code, ErrorCode::unsupported);
}

TEST(Migration, OvershootingStepIsUnsupported) {
    MigrationRegistry registry;
    registry.add({1, 3, [](Profile profile) -> Result<Profile> { return profile; }});
    auto migrated = registry.migrate(make_profile(), 2);
    ASSERT_FALSE(migrated);
    EXPECT_EQ(migrated.error().code, ErrorCode::unsupported);
}

TEST(Migration, FailingTransformPropagatesError) {
    MigrationRegistry registry;
    registry.add({1, 2, [](Profile) -> Result<Profile> {
                      return std::unexpected(Error::make(ErrorCode::format, "transform failed"));
                  }});
    auto migrated = registry.migrate(make_profile(), 2);
    ASSERT_FALSE(migrated);
    EXPECT_EQ(migrated.error().code, ErrorCode::format);
}

} // namespace
} // namespace envbox::engine
