#include "envbox/core/container.hpp"

#include <cstdint>
#include <cstring>
#include <fstream>
#include <vector>

#include <zstd.h>

#include "envbox/core/version.hpp"

namespace envbox {

namespace {

constexpr std::uint64_t kMaxDecompressedBytes = 512ull * 1024 * 1024;

Result<std::vector<std::byte>> read_file(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    if (!stream) {
        return std::unexpected(Error::make(ErrorCode::io, "cannot open file for reading: " + path.string()));
    }
    const std::streampos end = stream.tellg();
    if (end < 0) {
        return std::unexpected(Error::make(ErrorCode::io, "cannot determine file size: " + path.string()));
    }
    std::vector<std::byte> bytes(static_cast<std::size_t>(end));
    stream.seekg(0);
    if (!bytes.empty()) {
        stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        if (!stream) {
            return std::unexpected(Error::make(ErrorCode::io, "failed while reading file: " + path.string()));
        }
    }
    return bytes;
}

}

Result<std::vector<std::byte>> encode_container(const std::vector<TarEntry>& entries) {
    auto tar_bytes = tar_write(entries);
    if (!tar_bytes) {
        return std::unexpected(tar_bytes.error());
    }

    const std::size_t bound = ZSTD_compressBound(tar_bytes->size());
    std::vector<std::byte> out(kContainerMagicSize + bound);
    std::memcpy(out.data(), kContainerMagic, kContainerMagicSize);

    const std::size_t written = ZSTD_compress(out.data() + kContainerMagicSize, bound, tar_bytes->data(),
                                              tar_bytes->size(), ZSTD_CLEVEL_DEFAULT);
    if (ZSTD_isError(written)) {
        return std::unexpected(Error::make(ErrorCode::format, std::string("zstd compression failed: ") + ZSTD_getErrorName(written)));
    }
    out.resize(kContainerMagicSize + written);
    return out;
}

Status write_container(const std::filesystem::path& out_path, const std::vector<TarEntry>& entries) {
    auto bytes = encode_container(entries);
    if (!bytes) {
        return std::unexpected(bytes.error());
    }

    std::filesystem::path tmp_path = out_path;
    tmp_path += ".tmp";

    {
        std::ofstream stream(tmp_path, std::ios::binary | std::ios::trunc);
        if (!stream) {
            return std::unexpected(Error::make(ErrorCode::io, "cannot open file for writing: " + tmp_path.string()));
        }
        stream.write(reinterpret_cast<const char*>(bytes->data()), static_cast<std::streamsize>(bytes->size()));
        stream.flush();
        if (!stream) {
            stream.close();
            std::error_code ec;
            std::filesystem::remove(tmp_path, ec);
            return std::unexpected(Error::make(ErrorCode::io, "failed while writing file: " + tmp_path.string()));
        }
    }

    std::error_code ec;
    std::filesystem::rename(tmp_path, out_path, ec);
    if (ec) {
        std::error_code ignored;
        std::filesystem::remove(tmp_path, ignored);
        return std::unexpected(Error::make(ErrorCode::io, "cannot move temporary file into place: " + out_path.string() + ": " + ec.message()));
    }
    return {};
}

Result<std::vector<TarEntry>> decode_container(std::span<const std::byte> bytes) {
    if (bytes.size() < kContainerMagicSize + 1) {
        return std::unexpected(Error::make(ErrorCode::format, "file too small to be an .envbox container"));
    }
    if (std::memcmp(bytes.data(), kContainerMagic, kContainerMagicSize) != 0) {
        return std::unexpected(Error::make(ErrorCode::format, "missing .envbox signature (not an envbox container?)"));
    }

    const std::byte* zstd_data = bytes.data() + kContainerMagicSize;
    const std::size_t zstd_size = bytes.size() - kContainerMagicSize;

    const unsigned long long frame_size = ZSTD_getFrameContentSize(zstd_data, zstd_size);
    if (frame_size == ZSTD_CONTENTSIZE_ERROR) {
        return std::unexpected(Error::make(ErrorCode::format, "corrupt zstd frame in .envbox container"));
    }
    if (frame_size == ZSTD_CONTENTSIZE_UNKNOWN) {
        std::size_t capacity = zstd_size * 4 + 64 * 1024;
        for (;;) {
            if (capacity > kMaxDecompressedBytes) {
                return std::unexpected(Error::make(ErrorCode::format, "decompressed payload exceeds the 512 MiB safety cap"));
            }
            std::vector<std::byte> buffer(capacity);
            const std::size_t result = ZSTD_decompress(buffer.data(), buffer.size(), zstd_data, zstd_size);
            if (!ZSTD_isError(result)) {
                buffer.resize(result);
                return tar_read(buffer);
            }
            capacity *= 2;
        }
    }

    if (frame_size > kMaxDecompressedBytes) {
        return std::unexpected(Error::make(ErrorCode::format, "decompressed payload exceeds the 512 MiB safety cap"));
    }
    std::vector<std::byte> tar_bytes(static_cast<std::size_t>(frame_size));
    const std::size_t result = ZSTD_decompress(tar_bytes.data(), tar_bytes.size(), zstd_data, zstd_size);
    if (ZSTD_isError(result)) {
        return std::unexpected(Error::make(ErrorCode::format, std::string("zstd decompression failed: ") + ZSTD_getErrorName(result)));
    }
    tar_bytes.resize(result);
    return tar_read(tar_bytes);
}

Result<std::vector<TarEntry>> read_container(const std::filesystem::path& in_path) {
    auto bytes = read_file(in_path);
    if (!bytes) {
        return std::unexpected(bytes.error());
    }
    return decode_container(*bytes);
}

}
