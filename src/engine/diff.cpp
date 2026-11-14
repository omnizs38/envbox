#include "envbox/engine/diff.hpp"

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <set>
#include <string>
#include <vector>

namespace envbox::engine {
namespace {

Result<std::vector<std::byte>> read_file_bytes(const std::filesystem::path& path) {
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

std::string bytes_as_string(const std::vector<std::byte>& data) {
    return {reinterpret_cast<const char*>(data.data()), data.size()};
}

bool looks_textual(const std::vector<std::byte>& data) {
    std::size_t control = 0;
    for (const auto value : data) {
        const unsigned char c = std::to_integer<unsigned char>(value);
        if (c == 0) {
            return false;
        }
        if (c < 0x20 && c != '\n' && c != '\r' && c != '\t') {
            ++control;
        }
    }
    return control * 20 <= data.size();
}

std::vector<std::string> split_lines(std::string_view text) {
    std::vector<std::string> lines;
    std::size_t start = 0;
    while (start <= text.size()) {
        const std::size_t newline = text.find('\n', start);
        const std::string_view line = (newline == std::string_view::npos)
                                          ? text.substr(start)
                                          : text.substr(start, newline - start);
        std::string value(line);
        if (!value.empty() && value.back() == '\r') {
            value.pop_back();
        }
        lines.push_back(std::move(value));
        if (newline == std::string_view::npos) {
            break;
        }
        start = newline + 1;
    }
    if (!lines.empty() && lines.back().empty()) {
        lines.pop_back();
    }
    return lines;
}

struct DiffOp {
    char type;
    std::string text;
};

bool line_diff(const std::vector<std::string>& old_lines,
               const std::vector<std::string>& new_lines,
               std::size_t max_cells,
               std::vector<DiffOp>& ops) {
    const std::size_t n = old_lines.size();
    const std::size_t m = new_lines.size();
    if (n > 4000 || m > 4000 || (n != 0 && m > max_cells / n)) {
        return false;
    }

    std::vector<std::vector<std::uint16_t>> lcs(n + 1, std::vector<std::uint16_t>(m + 1, 0));
    for (std::size_t i = 1; i <= n; ++i) {
        for (std::size_t j = 1; j <= m; ++j) {
            if (old_lines[i - 1] == new_lines[j - 1]) {
                lcs[i][j] = static_cast<std::uint16_t>(lcs[i - 1][j - 1] + 1);
            } else {
                lcs[i][j] = std::max(lcs[i - 1][j], lcs[i][j - 1]);
            }
        }
    }

    std::vector<DiffOp> reversed;
    std::size_t i = n;
    std::size_t j = m;
    while (i > 0 && j > 0) {
        if (old_lines[i - 1] == new_lines[j - 1]) {
            reversed.push_back({' ', old_lines[i - 1]});
            --i;
            --j;
        } else if (lcs[i - 1][j] > lcs[i][j - 1]) {
            reversed.push_back({'-', old_lines[i - 1]});
            --i;
        } else {
            reversed.push_back({'+', new_lines[j - 1]});
            --j;
        }
    }
    while (i > 0) {
        reversed.push_back({'-', old_lines[i - 1]});
        --i;
    }
    while (j > 0) {
        reversed.push_back({'+', new_lines[j - 1]});
        --j;
    }

    ops.assign(reversed.rbegin(), reversed.rend());
    return true;
}

std::string format_hunks(const std::vector<DiffOp>& ops, std::size_t max_lines, bool& truncated) {
    constexpr std::size_t kContext = 3;
    std::vector<std::size_t> changes;
    for (std::size_t i = 0; i < ops.size(); ++i) {
        if (ops[i].type != ' ') {
            changes.push_back(i);
        }
    }
    if (changes.empty()) {
        return {};
    }

    std::vector<std::pair<std::size_t, std::size_t>> hunks;
    std::size_t start = changes[0] > kContext ? changes[0] - kContext : 0;
    std::size_t end = std::min(ops.size(), changes[0] + kContext + 1);
    for (std::size_t k = 1; k < changes.size(); ++k) {
        const std::size_t change = changes[k];
        if (change < end) {
            continue;
        }
        if (change <= end + kContext) {
            end = std::min(ops.size(), change + kContext + 1);
        } else {
            hunks.emplace_back(start, end);
            start = change > kContext ? change - kContext : 0;
            end = std::min(ops.size(), change + kContext + 1);
        }
    }
    hunks.emplace_back(start, end);

    std::string out;
    std::size_t emitted = 0;
    for (const auto& [hunk_start, hunk_end] : hunks) {
        if (max_lines != 0 && emitted + (hunk_end - hunk_start) > max_lines) {
            truncated = true;
            break;
        }

        std::size_t old_line = 1;
        std::size_t new_line = 1;
        for (std::size_t i = 0; i < hunk_start; ++i) {
            if (ops[i].type != '+') {
                ++old_line;
            }
            if (ops[i].type != '-') {
                ++new_line;
            }
        }

        std::size_t old_count = 0;
        std::size_t new_count = 0;
        for (std::size_t i = hunk_start; i < hunk_end; ++i) {
            if (ops[i].type != '+') {
                ++old_count;
            }
            if (ops[i].type != '-') {
                ++new_count;
            }
        }

        const std::size_t old_start = old_count == 0 ? old_line - 1 : old_line;
        const std::size_t new_start = new_count == 0 ? new_line - 1 : new_line;
        out += "@@ -" + std::to_string(old_start) + "," + std::to_string(old_count) + " +" + std::to_string(new_start) + "," +
               std::to_string(new_count) + " @@\n";
        for (std::size_t i = hunk_start; i < hunk_end; ++i) {
            out += ops[i].type;
            out += ops[i].text;
            out += '\n';
        }
        emitted += hunk_end - hunk_start;
    }
    return out;
}

std::string note_for_skipped(std::string_view payload_path) {
    if (payload_path.starts_with("applications/")) {
        return "not resolved by its adapter; not restored";
    }
    return "no adapter handles this payload root; not restored";
}

}

std::size_t ProfileDiff::count(ChangeKind kind) const {
    return static_cast<std::size_t>(std::count_if(changes.begin(), changes.end(), [kind](const FileChange& change) {
        return change.kind == kind;
    }));
}

Result<ProfileDiff> diff_profile(const Profile& profile, const AdapterRegistry& registry, const DiffOptions& options) {
    PlanOptions plan_options;
    plan_options.target_os_override = options.target_os_override;
    plan_options.only_applications = options.only_applications;
    auto plan = plan_restore(profile, registry, plan_options);
    if (!plan) {
        return std::unexpected(plan.error());
    }

    ProfileDiff diff;
    diff.profile_name = plan->profile_name;
    diff.warnings = plan->warnings;
    diff.missing = plan->missing;

    std::set<std::string> resolved;
    for (const auto& action : plan->actions) {
        resolved.insert(action.payload_path);

        FileChange change;
        change.payload_path = action.payload_path;
        change.target = action.target;
        change.note = action.note;

        if (!action.target_exists) {
            change.kind = ChangeKind::new_file;
            diff.changes.push_back(std::move(change));
            continue;
        }

        auto current = read_file_bytes(action.target);
        if (!current) {
            return std::unexpected(current.error());
        }
        if (*current == action.data) {
            change.kind = ChangeKind::identical;
            diff.changes.push_back(std::move(change));
            continue;
        }

        change.kind = ChangeKind::modified;
        change.text = looks_textual(*current) && looks_textual(action.data);
        if (change.text) {
            std::vector<DiffOp> ops;
            if (line_diff(split_lines(bytes_as_string(*current)), split_lines(bytes_as_string(action.data)), 2000000, ops)) {
                change.diff_text = format_hunks(ops, options.max_diff_lines, change.truncated);
            } else {
                change.truncated = true;
            }
        }
        diff.changes.push_back(std::move(change));
    }

    for (const auto& entry : profile.payload) {
        if (resolved.contains(entry.path)) {
            continue;
        }
        FileChange change;
        change.payload_path = entry.path;
        change.kind = ChangeKind::skipped;
        change.note = note_for_skipped(entry.path);
        diff.changes.push_back(std::move(change));
    }

    if (!options.include_identical) {
        std::erase_if(diff.changes, [](const FileChange& change) { return change.kind == ChangeKind::identical; });
    }

    return diff;
}

}
