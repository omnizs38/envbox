#include "envbox/engine/capture.hpp"

#include <iterator>
#include <set>
#include <sstream>

namespace envbox::engine {

Result<CaptureReport> capture_profile(const std::filesystem::path& out_path,
                                      ProfileInfo info,
                                      const AdapterRegistry& registry,
                                      const CaptureOptions& options) {
    std::vector<PayloadFile> payload;
    std::vector<ApplicationSpec> specs;
    std::vector<std::string> captured_ids;
    std::vector<std::pair<std::string, std::string>> skipped;
    std::vector<std::string> warnings;

    const std::set<std::string> selected(options.only_adapters.begin(), options.only_adapters.end());
    std::set<std::string> seen_adapters;

    for (const Adapter* adapter : registry.all()) {
        if (!selected.empty() && !selected.contains(std::string(adapter->id()))) {
            continue;
        }
        seen_adapters.insert(std::string(adapter->id()));

        auto captured = adapter->capture();
        if (!captured) {
            skipped.emplace_back(std::string(adapter->id()), captured.error().message);
            continue;
        }
        for (auto& warning : captured->warnings) {
            warnings.push_back(std::string(adapter->id()) + ": " + warning);
        }
        captured_ids.push_back(captured->spec.id);
        specs.push_back(captured->spec);
        payload.insert(payload.end(), std::make_move_iterator(captured->files.begin()),
                       std::make_move_iterator(captured->files.end()));
    }

    for (const auto& id : selected) {
        if (!seen_adapters.contains(id)) {
            skipped.emplace_back(id, "adapter is not registered");
        }
    }

    if (specs.empty()) {
        std::ostringstream message;
        message << "nothing could be captured:";
        for (const auto& [id, reason] : skipped) {
            message << " [" << id << ": " << reason << "]";
        }
        return std::unexpected(Error::make(ErrorCode::not_found, message.str()));
    }

    info.applications = std::move(specs);
    auto status = create_profile(out_path, std::move(info), std::move(payload));
    if (!status) {
        return std::unexpected(status.error());
    }

    CaptureReport report;
    report.profile_path = out_path.string();
    report.captured = std::move(captured_ids);
    report.skipped = std::move(skipped);
    report.warnings = std::move(warnings);
    return report;
}

}
