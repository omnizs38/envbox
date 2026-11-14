#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "common/test_support.hpp"
#include "envbox/core/os.hpp"
#include "envbox/core/profile.hpp"
#include "envbox/storage/library.hpp"

namespace envbox::storage {
namespace {

namespace fs = std::filesystem;

class LibraryTest : public ::testing::Test {
  protected:
    void SetUp() override {
        static int counter = 0;
        const auto unique = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "-" +
                            std::to_string(counter++);
        dir_ = fs::temp_directory_path() / ("envbox-library-test-" + unique);
        fs::create_directories(dir_);
    }

    void TearDown() override {
        std::error_code ec;
        fs::remove_all(dir_, ec);
    }

    fs::path make_profile(const std::string& name, const std::vector<std::string>& tags = {}) {
        ProfileInfo info;
        info.name = name;
        info.tags = tags;
        info.applications = {{"git", "", "git", 1}};
        const fs::path path = dir_ / (name + ".envbox");
        const PayloadFile payload{"applications/git/config", {std::byte{'x'}}};
        EXPECT_TRUE(create_profile(path, info, {payload}));
        return path;
    }

    fs::path dir_;
};

TEST_F(LibraryTest, AddAndList) {
    auto library = Library::open(dir_ / "library.db");
    ASSERT_TRUE(library) << library.error().message;

    const fs::path profile = make_profile("Workstation", {"work", "primary"});
    ASSERT_TRUE(library->add(profile));

    auto entries = library->list();
    ASSERT_TRUE(entries);
    ASSERT_EQ(entries->size(), 1u);
    const auto& entry = (*entries)[0];
    EXPECT_EQ(entry.name, "Workstation");
    EXPECT_EQ(entry.source_os, std::string(to_string(current_os().kind)));
    EXPECT_EQ(entry.format_version, 1u);
    EXPECT_EQ(entry.applications, std::vector<std::string>{"git"});
    EXPECT_EQ(entry.tags, (std::vector<std::string>{"work", "primary"}));
    EXPECT_TRUE(entry.file_exists);
    EXPECT_FALSE(entry.added_at.empty());
}

TEST_F(LibraryTest, AddIsUpsert) {
    auto library = Library::open(dir_ / "library.db");
    ASSERT_TRUE(library);

    const fs::path profile = make_profile("Original");
    ASSERT_TRUE(library->add(profile));
    auto first = library->list();
    ASSERT_TRUE(first);
    const std::string added_at = (*first)[0].added_at;

    ProfileInfo renamed;
    renamed.name = "Renamed";
    renamed.applications = {{"git", "", "git", 1}};
    ASSERT_TRUE(create_profile(profile, renamed, {{"applications/git/config", {std::byte{'y'}}}}));
    ASSERT_TRUE(library->add(profile));

    auto entries = library->list();
    ASSERT_TRUE(entries);
    ASSERT_EQ(entries->size(), 1u);
    EXPECT_EQ((*entries)[0].name, "Renamed");
    EXPECT_EQ((*entries)[0].added_at, added_at);
}

TEST_F(LibraryTest, RemoveEntry) {
    auto library = Library::open(dir_ / "library.db");
    ASSERT_TRUE(library);

    const fs::path profile = make_profile("Removable");
    ASSERT_TRUE(library->add(profile));
    ASSERT_TRUE(library->remove(profile));

    auto entries = library->list();
    ASSERT_TRUE(entries);
    EXPECT_TRUE(entries->empty());

    auto again = library->remove(profile);
    ASSERT_FALSE(again);
    EXPECT_EQ(again.error().code, ErrorCode::not_found);
}

TEST_F(LibraryTest, MissingFileIsFlagged) {
    auto library = Library::open(dir_ / "library.db");
    ASSERT_TRUE(library);

    const fs::path profile = make_profile("Vanishing");
    ASSERT_TRUE(library->add(profile));
    std::error_code ec;
    fs::remove(profile, ec);
    ASSERT_FALSE(ec);

    auto entries = library->list();
    ASSERT_TRUE(entries);
    ASSERT_EQ(entries->size(), 1u);
    EXPECT_FALSE((*entries)[0].file_exists);
}

TEST_F(LibraryTest, RejectsInvalidProfile) {
    auto library = Library::open(dir_ / "library.db");
    ASSERT_TRUE(library);

    const fs::path garbage = dir_ / "garbage.envbox";
    {
        std::ofstream stream(garbage, std::ios::binary);
        stream << "not an envbox profile";
    }
    auto added = library->add(garbage);
    ASSERT_FALSE(added);
    EXPECT_NE(added.error().code, ErrorCode::conflict);

    auto entries = library->list();
    ASSERT_TRUE(entries);
    EXPECT_TRUE(entries->empty());
}

TEST_F(LibraryTest, PersistsAcrossReopen) {
    const fs::path database = dir_ / "library.db";
    const fs::path profile = make_profile("Persistent");
    {
        auto library = Library::open(database);
        ASSERT_TRUE(library);
        ASSERT_TRUE(library->add(profile));
    }
    {
        auto library = Library::open(database);
        ASSERT_TRUE(library);
        auto entries = library->list();
        ASSERT_TRUE(entries);
        ASSERT_EQ(entries->size(), 1u);
        EXPECT_EQ((*entries)[0].name, "Persistent");
    }
}

TEST_F(LibraryTest, FindByPath) {
    auto library = Library::open(dir_ / "library.db");
    ASSERT_TRUE(library);

    const fs::path profile = make_profile("Findable");
    ASSERT_TRUE(library->add(profile));

    auto found = library->find(profile);
    ASSERT_TRUE(found);
    ASSERT_TRUE(found->has_value());
    EXPECT_EQ((*found)->name, "Findable");

    auto missing = library->find(dir_ / "other.envbox");
    ASSERT_TRUE(missing);
    EXPECT_FALSE(missing->has_value());
}

TEST_F(LibraryTest, DefaultPathLivesInEnvboxDataDir) {
    test::EnvGuard env;
    const fs::path data_dir = dir_ / "AppData" / "Local";
    env.set("LOCALAPPDATA", data_dir.string());
    env.set("XDG_DATA_HOME", data_dir.string());

    const fs::path path = default_library_path();
    ASSERT_FALSE(path.empty());
    EXPECT_EQ(path.filename(), "library.db");
    EXPECT_EQ(path.parent_path().filename(), "envbox");
}

}
}
