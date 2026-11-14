#include "envbox/storage/library.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <string>
#include <utility>

#include <nlohmann/json.hpp>
#include <sqlite3.h>

#include "envbox/core/profile.hpp"
#include "envbox/platform/paths.hpp"

namespace envbox::storage {
namespace {

constexpr const char* kSchema =
    "CREATE TABLE IF NOT EXISTS profiles ("
    "id INTEGER PRIMARY KEY AUTOINCREMENT,"
    "path TEXT NOT NULL UNIQUE,"
    "name TEXT NOT NULL,"
    "description TEXT NOT NULL DEFAULT '',"
    "source_os TEXT NOT NULL,"
    "architecture TEXT NOT NULL,"
    "created_at TEXT NOT NULL,"
    "added_at TEXT NOT NULL,"
    "format_version INTEGER NOT NULL,"
    "applications TEXT NOT NULL DEFAULT '[]',"
    "tags TEXT NOT NULL DEFAULT '[]'"
    ");"
    "CREATE INDEX IF NOT EXISTS idx_profiles_name ON profiles(name);";

std::string now_iso8601_utc() {
    const std::time_t t = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm tm{};
#if defined(_WIN32)
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif
    char buffer[32];
    std::snprintf(buffer, sizeof buffer, "%04d-%02d-%02dT%02d:%02d:%02dZ", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                  tm.tm_hour, tm.tm_min, tm.tm_sec);
    return buffer;
}

std::string json_array(const std::vector<std::string>& values) {
    nlohmann::json array = nlohmann::json::array();
    for (const auto& value : values) {
        array.push_back(value);
    }
    return array.dump();
}

Result<std::vector<std::string>> parse_json_array(const char* text, const std::string& column) {
    std::vector<std::string> values;
    try {
        const auto parsed = nlohmann::json::parse(text != nullptr ? text : "[]");
        if (!parsed.is_array()) {
            return std::unexpected(Error::make(ErrorCode::format, "library database: '" + column + "' is not a JSON array"));
        }
        for (const auto& item : parsed) {
            if (!item.is_string()) {
                return std::unexpected(Error::make(ErrorCode::format, "library database: '" + column + "' contains a non-string value"));
            }
            values.push_back(item.get<std::string>());
        }
    } catch (const nlohmann::json::exception&) {
        return std::unexpected(Error::make(ErrorCode::format, "library database: '" + column + "' is not valid JSON"));
    }
    return values;
}

std::string canonical_path_string(const std::filesystem::path& path) {
    std::error_code ec;
    auto absolute = std::filesystem::absolute(path, ec);
    if (ec) {
        absolute = path;
    }
    return absolute.lexically_normal().string();
}

std::string text_column(sqlite3_stmt* statement, int index) {
    const auto* text = sqlite3_column_text(statement, index);
    return text != nullptr ? std::string(reinterpret_cast<const char*>(text)) : std::string{};
}

LibraryEntry entry_from_statement(sqlite3_stmt* statement) {
    LibraryEntry entry;
    entry.id = sqlite3_column_int64(statement, 0);
    entry.path = text_column(statement, 1);
    entry.name = text_column(statement, 2);
    entry.description = text_column(statement, 3);
    entry.source_os = text_column(statement, 4);
    entry.architecture = text_column(statement, 5);
    entry.created_at = text_column(statement, 6);
    entry.added_at = text_column(statement, 7);
    entry.format_version = static_cast<std::uint32_t>(sqlite3_column_int(statement, 8));
    auto applications = parse_json_array(text_column(statement, 9).c_str(), "applications");
    auto tags = parse_json_array(text_column(statement, 10).c_str(), "tags");
    if (applications) {
        entry.applications = std::move(*applications);
    }
    if (tags) {
        entry.tags = std::move(*tags);
    }
    std::error_code ec;
    entry.file_exists = std::filesystem::is_regular_file(entry.path, ec) && !ec;
    return entry;
}

constexpr const char* kSelectColumns =
    "SELECT id, path, name, description, source_os, architecture, created_at, added_at, format_version, applications, tags FROM profiles";

}

struct Library::Impl {
    sqlite3* db = nullptr;
    std::filesystem::path database_path;

    ~Impl() {
        if (db != nullptr) {
            sqlite3_close(db);
        }
    }
};

Library::Library(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}

Library::Library(Library&&) noexcept = default;
Library& Library::operator=(Library&&) noexcept = default;
Library::~Library() = default;

Result<Library> Library::open(const std::filesystem::path& database_path) {
    if (database_path.empty()) {
        return std::unexpected(Error::make(ErrorCode::io, "library database path is empty"));
    }
    std::error_code ec;
    const auto parent = database_path.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent, ec);
        if (ec) {
            return std::unexpected(Error::make(ErrorCode::io, "cannot create library directory " + parent.string() + ": " + ec.message()));
        }
    }

    sqlite3* db = nullptr;
    const int rc = sqlite3_open_v2(database_path.string().c_str(), &db,
                                   SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr);
    if (rc != SQLITE_OK) {
        std::string message = db != nullptr ? sqlite3_errmsg(db) : "unknown error";
        if (db != nullptr) {
            sqlite3_close(db);
        }
        return std::unexpected(Error::make(ErrorCode::io, "cannot open library database " + database_path.string() + ": " + message));
    }

    char* sqlite_error = nullptr;
    if (sqlite3_exec(db, kSchema, nullptr, nullptr, &sqlite_error) != SQLITE_OK) {
        const std::string message = sqlite_error != nullptr ? sqlite_error : "unknown error";
        sqlite3_free(sqlite_error);
        sqlite3_close(db);
        return std::unexpected(Error::make(ErrorCode::io, "cannot initialize library schema: " + message));
    }

    auto impl = std::make_unique<Impl>();
    impl->db = db;
    impl->database_path = database_path;
    return Library(std::move(impl));
}

Result<Library> Library::open_default() {
    const auto path = default_library_path();
    if (path.empty()) {
        return std::unexpected(Error::make(ErrorCode::not_found, "no data directory available for the profile library"));
    }
    return open(path);
}

const std::filesystem::path& Library::database_path() const {
    return impl_->database_path;
}

Status Library::add(const std::filesystem::path& profile_path) {
    auto profile = open_profile(profile_path);
    if (!profile) {
        return std::unexpected(profile.error());
    }

    constexpr const char* kUpsert =
        "INSERT INTO profiles (path, name, description, source_os, architecture, created_at, added_at, format_version, applications, tags) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?) "
        "ON CONFLICT(path) DO UPDATE SET "
        "name = excluded.name, description = excluded.description, source_os = excluded.source_os, "
        "architecture = excluded.architecture, created_at = excluded.created_at, "
        "format_version = excluded.format_version, applications = excluded.applications, tags = excluded.tags";

    std::vector<std::string> application_ids;
    for (const auto& app : profile->info.applications) {
        application_ids.push_back(app.id);
    }

    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v2(impl_->db, kUpsert, -1, &statement, nullptr) != SQLITE_OK) {
        return std::unexpected(Error::make(ErrorCode::io, std::string("library database: ") + sqlite3_errmsg(impl_->db)));
    }

    const std::string canonical = canonical_path_string(profile_path);
    const std::string applications_json = json_array(application_ids);
    const std::string tags_json = json_array(profile->info.tags);
    const std::string added_at = now_iso8601_utc();

    const int bind_results[] = {
        sqlite3_bind_text(statement, 1, canonical.c_str(), -1, SQLITE_TRANSIENT),
        sqlite3_bind_text(statement, 2, profile->info.name.c_str(), -1, SQLITE_TRANSIENT),
        sqlite3_bind_text(statement, 3, profile->info.description.c_str(), -1, SQLITE_TRANSIENT),
        sqlite3_bind_text(statement, 4, profile->info.source.os.c_str(), -1, SQLITE_TRANSIENT),
        sqlite3_bind_text(statement, 5, profile->info.source.architecture.c_str(), -1, SQLITE_TRANSIENT),
        sqlite3_bind_text(statement, 6, profile->info.created_at.c_str(), -1, SQLITE_TRANSIENT),
        sqlite3_bind_text(statement, 7, added_at.c_str(), -1, SQLITE_TRANSIENT),
        sqlite3_bind_int(statement, 8, static_cast<int>(profile->manifest.format_version)),
        sqlite3_bind_text(statement, 9, applications_json.c_str(), -1, SQLITE_TRANSIENT),
        sqlite3_bind_text(statement, 10, tags_json.c_str(), -1, SQLITE_TRANSIENT),
    };

    for (const int result : bind_results) {
        if (result != SQLITE_OK) {
            sqlite3_finalize(statement);
            return std::unexpected(Error::make(ErrorCode::io, std::string("library database: ") + sqlite3_errmsg(impl_->db)));
        }
    }

    const int step = sqlite3_step(statement);
    const std::string step_error = step != SQLITE_DONE ? sqlite3_errmsg(impl_->db) : "";
    sqlite3_finalize(statement);
    if (step != SQLITE_DONE) {
        return std::unexpected(Error::make(ErrorCode::io, "library database: " + step_error));
    }
    return {};
}

Status Library::remove(const std::filesystem::path& profile_path) {
    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v2(impl_->db, "DELETE FROM profiles WHERE path = ?", -1, &statement, nullptr) != SQLITE_OK) {
        return std::unexpected(Error::make(ErrorCode::io, std::string("library database: ") + sqlite3_errmsg(impl_->db)));
    }
    const std::string canonical = canonical_path_string(profile_path);
    sqlite3_bind_text(statement, 1, canonical.c_str(), -1, SQLITE_TRANSIENT);
    const int step = sqlite3_step(statement);
    sqlite3_finalize(statement);
    if (step != SQLITE_DONE) {
        return std::unexpected(Error::make(ErrorCode::io, std::string("library database: ") + sqlite3_errmsg(impl_->db)));
    }
    if (sqlite3_changes(impl_->db) == 0) {
        return std::unexpected(Error::make(ErrorCode::not_found, "profile is not in the library: " + profile_path.string()));
    }
    return {};
}

Result<std::vector<LibraryEntry>> Library::list() const {
    sqlite3_stmt* statement = nullptr;
    const std::string query = std::string(kSelectColumns) + " ORDER BY name COLLATE NOCASE, path";
    if (sqlite3_prepare_v2(impl_->db, query.c_str(), -1, &statement, nullptr) != SQLITE_OK) {
        return std::unexpected(Error::make(ErrorCode::io, std::string("library database: ") + sqlite3_errmsg(impl_->db)));
    }

    std::vector<LibraryEntry> entries;
    for (;;) {
        const int step = sqlite3_step(statement);
        if (step == SQLITE_ROW) {
            entries.push_back(entry_from_statement(statement));
            continue;
        }
        if (step == SQLITE_DONE) {
            break;
        }
        const std::string message = sqlite3_errmsg(impl_->db);
        sqlite3_finalize(statement);
        return std::unexpected(Error::make(ErrorCode::io, "library database: " + message));
    }
    sqlite3_finalize(statement);
    return entries;
}

Result<std::optional<LibraryEntry>> Library::find(const std::filesystem::path& profile_path) const {
    sqlite3_stmt* statement = nullptr;
    const std::string query = std::string(kSelectColumns) + " WHERE path = ? LIMIT 1";
    if (sqlite3_prepare_v2(impl_->db, query.c_str(), -1, &statement, nullptr) != SQLITE_OK) {
        return std::unexpected(Error::make(ErrorCode::io, std::string("library database: ") + sqlite3_errmsg(impl_->db)));
    }
    const std::string canonical = canonical_path_string(profile_path);
    sqlite3_bind_text(statement, 1, canonical.c_str(), -1, SQLITE_TRANSIENT);

    const int step = sqlite3_step(statement);
    if (step == SQLITE_ROW) {
        auto entry = entry_from_statement(statement);
        sqlite3_finalize(statement);
        return std::optional<LibraryEntry>(std::move(entry));
    }
    if (step == SQLITE_DONE) {
        sqlite3_finalize(statement);
        return std::optional<LibraryEntry>(std::nullopt);
    }
    const std::string message = sqlite3_errmsg(impl_->db);
    sqlite3_finalize(statement);
    return std::unexpected(Error::make(ErrorCode::io, "library database: " + message));
}

std::filesystem::path default_library_path() {
    const auto data = platform::envbox_data_dir();
    if (data.empty()) {
        return {};
    }
    return data / "library.db";
}

}
