#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "envbox/core/error.hpp"

namespace envbox::storage {

struct LibraryEntry {
    std::int64_t id = 0;
    std::filesystem::path path;
    std::string name;
    std::string description;
    std::string source_os;
    std::string architecture;
    std::string created_at;
    std::string added_at;
    std::uint32_t format_version = 0;
    std::vector<std::string> tags;
    std::vector<std::string> applications;
    bool file_exists = true;
};

class Library {
  public:
    Library(Library&&) noexcept;
    Library& operator=(Library&&) noexcept;
    Library(const Library&) = delete;
    Library& operator=(const Library&) = delete;
    ~Library();

    [[nodiscard]] static Result<Library> open(const std::filesystem::path& database_path);
    [[nodiscard]] static Result<Library> open_default();

    [[nodiscard]] const std::filesystem::path& database_path() const;

    [[nodiscard]] Status add(const std::filesystem::path& profile_path);
    [[nodiscard]] Status remove(const std::filesystem::path& profile_path);
    [[nodiscard]] Result<std::vector<LibraryEntry>> list() const;
    [[nodiscard]] Result<std::optional<LibraryEntry>> find(const std::filesystem::path& profile_path) const;

  private:
    struct Impl;
    explicit Library(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};

[[nodiscard]] std::filesystem::path default_library_path();

}
