#include "core.hpp"
#include <chrono>
#include <fstream>
#include <mutex>

namespace cxs {
void runtime_log(const fs::path& state, const std::string& event, const Json& details) noexcept {
    // Fixed diagnostic fields only: never serialize a request, credential or file contents.
    try {
        static std::mutex mutex;
        std::lock_guard guard(mutex);
        auto folder = state / "logs";
        private_directory(folder);
        const auto now = std::chrono::system_clock::now();
        const auto day = std::chrono::duration_cast<std::chrono::hours>(now.time_since_epoch()).count() / 24;
        auto file = folder / ("runtime-" + std::to_string(day) + ".jsonl");
        std::error_code error;
        if (fs::exists(file, error) && fs::file_size(file, error) > 10 * 1024 * 1024) return;
        Json record{{"time_unix_ms", std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count()}, {"event", event}};
        for (auto field : {"operation", "operation_id", "job_id", "metadata_checked", "body_files", "body_bytes", "reused_files", "resumed_objects", "no_changes", "error_code", "retryable", "phase", "error", "elapsed_ms", "files", "bytes", "uploaded_objects", "skipped_count", "skipped", "snapshot"})
            if (details.contains(field)) record[field] = details.at(field);
        std::ofstream stream(file, std::ios::binary | std::ios::app);
        stream << record.dump() << '\n';
    } catch (...) { /* Logging must not fail a backup or expose diagnostic data remotely. */ }
}
}

