#include "envbox/core/tar.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <optional>
#include <set>
#include <string_view>

namespace envbox {
namespace {

constexpr std::size_t kBlockSize = 512;

bool is_all_zero(const unsigned char* p, std::size_t n) {
    for (std::size_t i = 0; i < n; ++i) {
        if (p[i] != 0) {
            return false;
        }
    }
    return true;
}

std::uint64_t round_up_block(std::uint64_t n) {
    return (n + kBlockSize - 1) / kBlockSize * kBlockSize;
}

std::uint64_t parse_octal(const unsigned char* field, std::size_t len) {
    std::uint64_t value = 0;
    std::size_t i = 0;
    while (i < len && (field[i] == ' ' || field[i] == '\0')) {
        ++i;
    }
    while (i < len && field[i] >= '0' && field[i] <= '7') {
        value = value * 8 + static_cast<std::uint64_t>(field[i] - '0');
        ++i;
    }
    return value;
}

void write_octal(unsigned char* dst, std::size_t len, std::uint64_t value) {
    std::snprintf(reinterpret_cast<char*>(dst), len, "%0*llo", static_cast<int>(len) - 1,
                  static_cast<unsigned long long>(value));
}

std::string_view field_string(const unsigned char* field, std::size_t len) {
    std::size_t n = 0;
    while (n < len && field[n] != '\0') {
        ++n;
    }
    return {reinterpret_cast<const char*>(field), n};
}

bool split_name(const std::string& path, std::string& prefix, std::string& name) {
    if (path.size() <= 100) {
        prefix.clear();
        name = path;
        return true;
    }
    for (std::size_t i = path.size(); i-- > 0;) {
        if (path[i] != '/') {
            continue;
        }
        const std::string_view p(path.data(), i);
        const std::string_view n(path.data() + i + 1, path.size() - i - 1);
        if (n.size() <= 100 && p.size() <= 155) {
            prefix.assign(p);
            name.assign(n);
            return true;
        }
    }
    return false;
}

struct PaxOverrides {
    std::string path;
    std::optional<std::uint64_t> size;
};

Result<PaxOverrides> parse_pax(std::string_view body) {
    PaxOverrides out;
    std::size_t i = 0;
    while (i < body.size()) {
        const std::size_t sp = body.find(' ', i);
        if (sp == std::string_view::npos) {
            return std::unexpected(Error::make(ErrorCode::format, "malformed pax record: missing length terminator"));
        }
        std::uint64_t len = 0;
        try {
            len = std::stoull(std::string(body.substr(i, sp - i)));
        } catch (const std::exception&) {
            return std::unexpected(Error::make(ErrorCode::format, "malformed pax record: bad length field"));
        }
        if (len == 0 || i + len > body.size()) {
            return std::unexpected(Error::make(ErrorCode::format, "malformed pax record: length out of bounds"));
        }
        std::string_view rec = body.substr(i, static_cast<std::size_t>(len));
        i += static_cast<std::size_t>(len);

        const std::size_t rsp = rec.find(' ');
        if (rsp == std::string_view::npos) {
            return std::unexpected(Error::make(ErrorCode::format, "malformed pax record: no separator"));
        }
        std::string_view kv = rec.substr(rsp + 1);
        if (!kv.empty() && kv.back() == '\n') {
            kv.remove_suffix(1);
        }
        const std::size_t eq = kv.find('=');
        if (eq == std::string_view::npos) {
            continue;
        }
        const std::string_view key = kv.substr(0, eq);
        const std::string_view value = kv.substr(eq + 1);
        if (key == "path") {
            out.path.assign(value);
        } else if (key == "size") {
            try {
                out.size = std::stoull(std::string(value));
            } catch (const std::exception&) {
                return std::unexpected(Error::make(ErrorCode::format, "malformed pax record: bad size value"));
            }
        }
    }
    return out;
}

}

Result<std::string> normalize_member_path(std::string_view path) {
    std::string text;
    text.reserve(path.size());
    for (const unsigned char c : path) {
        text += (c == '\\') ? '/' : static_cast<char>(c);
        if (c < 0x20 || c == 0x7F) {
            return std::unexpected(Error::make(ErrorCode::validation, "archive member path contains a control character"));
        }
    }
    if (text.empty()) {
        return std::unexpected(Error::make(ErrorCode::validation, "archive member path is empty"));
    }
    if (text.front() == '/') {
        return std::unexpected(Error::make(ErrorCode::validation, "archive member path must be relative: " + text));
    }
    if (text.size() >= 2 && text[1] == ':' &&
        ((text[0] >= 'a' && text[0] <= 'z') || (text[0] >= 'A' && text[0] <= 'Z'))) {
        return std::unexpected(Error::make(ErrorCode::validation, "archive member path must not contain a drive letter: " + text));
    }

    std::vector<std::string_view> parts;
    std::size_t start = 0;
    while (start <= text.size()) {
        const std::size_t slash = text.find('/', start);
        const std::string_view part = (slash == std::string::npos)
                                          ? std::string_view(text).substr(start)
                                          : std::string_view(text).substr(start, slash - start);
        if (part == "..") {
            return std::unexpected(Error::make(ErrorCode::validation, "archive member path escapes the archive root: " + text));
        }
        if (!part.empty() && part != ".") {
            parts.push_back(part);
        }
        if (slash == std::string::npos) {
            break;
        }
        start = slash + 1;
    }
    if (parts.empty()) {
        return std::unexpected(Error::make(ErrorCode::validation, "archive member path is empty after normalization"));
    }

    std::string normalized;
    for (const std::string_view part : parts) {
        if (!normalized.empty()) {
            normalized += '/';
        }
        normalized += part;
    }
    return normalized;
}

Result<std::vector<std::byte>> tar_write(const std::vector<TarEntry>& entries) {
    std::vector<std::byte> out;
    std::set<std::string> seen;
    const std::uint64_t mtime = static_cast<std::uint64_t>(std::time(nullptr));

    for (const TarEntry& entry : entries) {
        auto norm = normalize_member_path(entry.path);
        if (!norm) {
            return std::unexpected(norm.error());
        }
        if (!seen.insert(*norm).second) {
            return std::unexpected(Error::make(ErrorCode::validation, "duplicate archive member: " + *norm));
        }

        std::string prefix;
        std::string name;
        if (!split_name(*norm, prefix, name)) {
            return std::unexpected(Error::make(ErrorCode::validation, "archive member path too long for ustar: " + *norm));
        }

        std::array<unsigned char, kBlockSize> header{};
        std::memcpy(header.data(), name.data(), name.size());
        write_octal(header.data() + 100, 8, entry.is_directory ? 0755 : 0644);
        write_octal(header.data() + 108, 8, 0);
        write_octal(header.data() + 116, 8, 0);
        write_octal(header.data() + 124, 12, entry.data.size());
        write_octal(header.data() + 136, 12, mtime);
        std::memset(header.data() + 148, ' ', 8);
        header[156] = entry.is_directory ? '5' : '0';
        std::memcpy(header.data() + 257, "ustar\0", 6);
        std::memcpy(header.data() + 263, "00", 2);
        std::memcpy(header.data() + 265, "envbox", 6);
        std::memcpy(header.data() + 297, "envbox", 6);
        std::memcpy(header.data() + 345, prefix.data(), prefix.size());

        unsigned int sum = 0;
        for (const unsigned char byte : header) {
            sum += byte;
        }
        std::snprintf(reinterpret_cast<char*>(header.data() + 148), 7, "%06o", sum);
        header[154] = '\0';
        header[155] = ' ';

        const auto* bytes = reinterpret_cast<const std::byte*>(header.data());
        out.insert(out.end(), bytes, bytes + kBlockSize);
        out.insert(out.end(), entry.data.begin(), entry.data.end());
        const std::uint64_t padding = (kBlockSize - entry.data.size() % kBlockSize) % kBlockSize;
        out.insert(out.end(), static_cast<std::size_t>(padding), std::byte{0});
    }

    out.insert(out.end(), 2 * kBlockSize, std::byte{0});
    return out;
}

Result<std::vector<TarEntry>> tar_read(std::span<const std::byte> bytes) {
    if (bytes.size() % kBlockSize != 0) {
        return std::unexpected(Error::make(ErrorCode::format, "ustar image size is not a multiple of 512 bytes"));
    }

    std::vector<TarEntry> entries;
    std::set<std::string> seen;
    std::size_t offset = 0;
    std::string pax_path;
    std::optional<std::uint64_t> pax_size;

    while (offset + kBlockSize <= bytes.size()) {
        const auto* header = reinterpret_cast<const unsigned char*>(bytes.data() + offset);
        if (is_all_zero(header, kBlockSize)) {
            break;
        }

        const std::uint64_t stored = parse_octal(header + 148, 8);
        unsigned int sum = 0;
        for (std::size_t i = 0; i < kBlockSize; ++i) {
            const unsigned char c = (i >= 148 && i < 156) ? static_cast<unsigned char>(' ') : header[i];
            sum += c;
        }
        if (stored != sum) {
            return std::unexpected(Error::make(ErrorCode::format, "bad ustar checksum at offset " + std::to_string(offset)));
        }
        if (std::memcmp(header + 257, "ustar", 5) != 0) {
            return std::unexpected(Error::make(ErrorCode::format, "missing ustar magic at offset " + std::to_string(offset)));
        }

        std::uint64_t size = parse_octal(header + 124, 12);
        const std::uint64_t data_offset = offset + kBlockSize;
        if (data_offset + size > bytes.size()) {
            return std::unexpected(Error::make(ErrorCode::format, "truncated entry at offset " + std::to_string(offset)));
        }

        const char typeflag = static_cast<char>(header[156]);
        if (typeflag == 'x' || typeflag == 'g') {
            const std::string_view body(reinterpret_cast<const char*>(bytes.data() + data_offset), static_cast<std::size_t>(size));
            auto overrides = parse_pax(body);
            if (!overrides) {
                return std::unexpected(overrides.error());
            }
            if (typeflag == 'x') {
                if (!overrides->path.empty()) {
                    pax_path = std::move(overrides->path);
                }
                if (overrides->size) {
                    pax_size = overrides->size;
                }
            }
            offset = static_cast<std::size_t>(data_offset + round_up_block(size));
            continue;
        }
        if (typeflag != '0' && typeflag != '\0' && typeflag != '5') {
            return std::unexpected(Error::make(ErrorCode::format,
                               std::string("unsupported tar entry type '") + typeflag + "' at offset " +
                                   std::to_string(offset)));
        }

        std::string full_path;
        const std::string_view prefix = field_string(header + 345, 155);
        if (!prefix.empty()) {
            full_path = std::string(prefix);
            full_path += '/';
            full_path += field_string(header, 100);
        } else {
            full_path.assign(field_string(header, 100));
        }
        if (!pax_path.empty()) {
            full_path = std::move(pax_path);
            pax_path.clear();
        }
        if (pax_size) {
            size = *pax_size;
            pax_size.reset();
            if (data_offset + size > bytes.size()) {
                return std::unexpected(Error::make(ErrorCode::format, "truncated entry at offset " + std::to_string(offset)));
            }
        }

        auto norm = normalize_member_path(full_path);
        if (!norm) {
            return std::unexpected(norm.error());
        }
        if (!seen.insert(*norm).second) {
            return std::unexpected(Error::make(ErrorCode::format, "duplicate archive member: " + *norm));
        }

        TarEntry entry;
        entry.path = std::move(*norm);
        entry.is_directory = (typeflag == '5');
        if (!entry.is_directory) {
            const auto* first = bytes.data() + data_offset;
            entry.data.assign(first, first + size);
        }
        entries.push_back(std::move(entry));
        offset = static_cast<std::size_t>(data_offset + round_up_block(size));
    }

    return entries;
}

}
