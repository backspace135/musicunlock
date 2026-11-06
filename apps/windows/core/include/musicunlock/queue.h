#pragma once
// The audio workspace's conversion queue, without UI: jobs, filters, serial batches and the
// user-facing wording. Mirrors AppModel.swift on macOS. Not thread-safe; owned by the UI thread.

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "musicunlock/conversion.h"
#include "musicunlock/import.h"

namespace mu {

enum class JobState { waiting, running, done, failed };
std::string_view state_label(JobState state);  // 待转换 / 转换中 / 已完成 / 未完成

enum class QueueFilter { all, waiting, done, failed };
inline constexpr QueueFilter all_filters[] = {QueueFilter::all, QueueFilter::waiting, QueueFilter::done,
                                              QueueFilter::failed};
std::string_view filter_label(QueueFilter filter);  // 全部 / 待转换 / 已完成 / 未完成
bool filter_includes(QueueFilter filter, JobState state);

/// Decimal units like Foundation's ByteCountFormatter (.file): "512 字节", "12 KB", "28.4 MB", "1.25 GB".
std::string format_file_size(std::uint64_t bytes);

struct AudioJob {
    std::uint64_t id = 0;
    std::filesystem::path source;
    std::uint64_t size = 0;
    JobState state = JobState::waiting;
    std::filesystem::path output;
    std::string output_extension;
    std::string title;   // from the restored tags, if any
    std::string detail;  // output file name, or the failure reason
    Bytes cover;

    std::string display_title() const;  // tag title, else the source file name without extension
    std::string source_format() const;  // "NCM"
    std::string subtitle() const;       // "网易云 · 28.4 MB"
    /// The whole row for screen readers: "远行的旋律，网易云 · 28.4 MB，NCM，未完成：<reason>".
    std::string spoken_summary() const;
};

class AudioQueue {
public:
    /// Adds files not already queued. Returns the notice to show; empty clears it.
    std::string add(const ImportScan& scan);

    const std::vector<AudioJob>& jobs() const { return jobs_; }
    std::vector<const AudioJob*> visible(QueueFilter filter) const;
    const AudioJob* find(std::uint64_t id) const;
    std::size_t count(JobState state) const;
    std::uint64_t total_size() const;

    bool remove(std::uint64_t id);  // running jobs stay
    void clear_completed();
    void clear();  // ignored while a batch runs

    /// Starts a serial batch over the waiting jobs; empty when one is running or nothing waits.
    std::vector<std::uint64_t> begin_batch();
    void job_started(std::uint64_t id);
    void job_succeeded(std::uint64_t id, const ConversionOutput& output);
    void job_failed(std::uint64_t id, std::string message);
    void request_stop() { stop_requested_ = running_; }
    bool stop_requested() const { return stop_requested_; }
    /// Ends the batch; returns the notice to show (the stop message) or empty.
    std::string end_batch();
    /// Failed jobs go back to waiting. Returns false while a batch runs.
    bool retry_failed();

    bool running() const { return running_; }
    std::size_t batch_finished() const { return batch_finished_; }
    std::size_t batch_count() const { return batch_count_; }

    /// Footer text: "正在转换 2 / 5", "1 个待转换，1 个已完成，1 个未完成", "准备好，随时开始".
    std::string status_title() const;
    /// Second footer line: total size, or the folder import hint for an empty queue.
    std::string status_detail() const;

private:
    AudioJob* find_mutable(std::uint64_t id);

    std::vector<AudioJob> jobs_;
    std::uint64_t next_id_ = 1;
    bool running_ = false;
    bool stop_requested_ = false;
    std::size_t batch_finished_ = 0;
    std::size_t batch_count_ = 0;
};

}  // namespace mu
