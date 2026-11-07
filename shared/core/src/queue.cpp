#include "musicunlock/queue.h"

#include <algorithm>
#include <cstdio>
#include <set>

#include "musicunlock/text.h"

namespace mu {

std::string_view state_label(JobState state) {
    switch (state) {
        case JobState::running: return "转换中";
        case JobState::done: return "已完成";
        case JobState::failed: return "未完成";
        case JobState::waiting: break;
    }
    return "待转换";
}

std::string_view filter_label(QueueFilter filter) {
    switch (filter) {
        case QueueFilter::waiting: return "待转换";
        case QueueFilter::done: return "已完成";
        case QueueFilter::failed: return "未完成";
        case QueueFilter::all: break;
    }
    return "全部";
}

bool filter_includes(QueueFilter filter, JobState state) {
    switch (filter) {
        case QueueFilter::waiting: return state == JobState::waiting || state == JobState::running;
        case QueueFilter::done: return state == JobState::done;
        case QueueFilter::failed: return state == JobState::failed;
        case QueueFilter::all: break;
    }
    return true;
}

std::string format_file_size(std::uint64_t bytes) {
    if (bytes < 1000) return std::to_string(bytes) + " 字节";
    struct Unit {
        const char* name;
        double scale;
        int decimals;
    };
    constexpr Unit units[] = {{"KB", 1e3, 0}, {"MB", 1e6, 1}, {"GB", 1e9, 2}, {"TB", 1e12, 2}};
    const Unit* unit = &units[0];
    for (const auto& candidate : units) {
        if (static_cast<double>(bytes) >= candidate.scale * 0.9995) unit = &candidate;
    }
    char text[32];
    std::snprintf(text, sizeof text, "%.*f %s", unit->decimals, static_cast<double>(bytes) / unit->scale, unit->name);
    return text;
}

std::string AudioJob::display_title() const {
    return title.empty() ? path_to_utf8(source.stem()) : title;
}

std::string AudioJob::source_format() const {
    std::string ext = path_to_utf8(source.extension());
    if (!ext.empty()) ext.erase(0, 1);
    for (char& c : ext) {
        if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 32);
    }
    return ext;
}

std::string AudioJob::subtitle() const {
    std::string ext = path_to_utf8(source.extension());
    if (!ext.empty()) ext.erase(0, 1);
    return platform_for(ascii_lower(ext)) + " · " + format_file_size(size);
}

std::string AudioJob::spoken_summary() const {
    std::string text = display_title() + "，" + subtitle() + "，" + source_format() + "，" + std::string(state_label(state));
    if (state == JobState::failed && !detail.empty()) text += "：" + detail;
    if (state == JobState::done && !output_extension.empty()) {
        std::string ext = output_extension;
        for (char& c : ext) {
            if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 32);
        }
        text += "，输出 " + ext;
    }
    return text;
}

std::string AudioQueue::add(const ImportScan& scan) {
    std::set<std::filesystem::path> existing;
    for (const auto& job : jobs_) existing.insert(job.source);
    std::size_t added = 0;
    for (const auto& file : scan.files) {
        if (!existing.insert(file.path).second) continue;
        AudioJob job;
        job.id = next_id_++;
        job.source = file.path;
        job.size = file.size;
        jobs_.push_back(std::move(job));
        ++added;
    }
    if (added == 0) {
        return scan.skipped > 0 ? "没有找到支持的加密音频文件。可添加 NCM、QMC、MFLAC、MGG 等格式。" : "这些文件已在队列中。";
    }
    if (scan.skipped > 0) {
        return "已添加 " + std::to_string(added) + " 个文件，跳过 " + std::to_string(scan.skipped) + " 个其他格式的文件。";
    }
    return "";
}

std::vector<const AudioJob*> AudioQueue::visible(QueueFilter filter) const {
    std::vector<const AudioJob*> out;
    for (const auto& job : jobs_) {
        if (filter_includes(filter, job.state)) out.push_back(&job);
    }
    return out;
}

const AudioJob* AudioQueue::find(std::uint64_t id) const {
    auto it = std::find_if(jobs_.begin(), jobs_.end(), [&](const AudioJob& job) { return job.id == id; });
    return it == jobs_.end() ? nullptr : &*it;
}

AudioJob* AudioQueue::find_mutable(std::uint64_t id) { return const_cast<AudioJob*>(find(id)); }

std::size_t AudioQueue::count(JobState state) const {
    return static_cast<std::size_t>(
        std::count_if(jobs_.begin(), jobs_.end(), [&](const AudioJob& job) { return job.state == state; }));
}

std::uint64_t AudioQueue::total_size() const {
    std::uint64_t total = 0;
    for (const auto& job : jobs_) total += job.size;
    return total;
}

bool AudioQueue::remove(std::uint64_t id) {
    return std::erase_if(jobs_, [&](const AudioJob& job) { return job.id == id && job.state != JobState::running; }) > 0;
}

void AudioQueue::clear_completed() {
    std::erase_if(jobs_, [](const AudioJob& job) { return job.state == JobState::done; });
}

void AudioQueue::clear() {
    if (running_) return;
    jobs_.clear();
    batch_finished_ = batch_count_ = 0;
}

std::vector<std::uint64_t> AudioQueue::begin_batch() {
    std::vector<std::uint64_t> pending;
    if (running_) return pending;
    for (const auto& job : jobs_) {
        if (job.state == JobState::waiting) pending.push_back(job.id);
    }
    if (pending.empty()) return pending;
    running_ = true;
    stop_requested_ = false;
    batch_count_ = pending.size();
    batch_finished_ = 0;
    return pending;
}

void AudioQueue::job_started(std::uint64_t id) {
    if (auto* job = find_mutable(id)) job->state = JobState::running;
}

void AudioQueue::job_succeeded(std::uint64_t id, const ConversionOutput& output) {
    if (auto* job = find_mutable(id)) {
        job->state = JobState::done;
        job->output = output.path;
        job->output_extension = output.file_extension;
        job->title = output.metadata.title;
        job->detail = path_to_utf8(output.path.filename());
        job->cover = output.metadata.cover.value_or(Bytes{});
    }
    ++batch_finished_;
}

void AudioQueue::job_failed(std::uint64_t id, std::string message) {
    if (auto* job = find_mutable(id)) {
        job->state = JobState::failed;
        job->detail = std::move(message);
    }
    ++batch_finished_;
}

std::string AudioQueue::end_batch() {
    const bool stopped = stop_requested_;
    running_ = false;
    stop_requested_ = false;
    return stopped ? "已停止。未开始的文件仍保留在队列中，可以继续转换。" : "";
}

bool AudioQueue::retry_failed() {
    if (running_) return false;
    for (auto& job : jobs_) {
        if (job.state == JobState::failed) {
            job.state = JobState::waiting;
            job.detail.clear();
        }
    }
    return true;
}

std::string AudioQueue::status_title() const {
    if (running_) {
        if (stop_requested_) return "将在当前文件完成后停止";
        return "正在转换 " + std::to_string(std::min(batch_finished_ + 1, batch_count_)) + " / " + std::to_string(batch_count_);
    }
    if (jobs_.empty()) return "准备好，随时开始";
    std::string text = std::to_string(count(JobState::waiting)) + " 个待转换，" + std::to_string(count(JobState::done)) + " 个已完成";
    if (const auto failed = count(JobState::failed)) text += "，" + std::to_string(failed) + " 个未完成";
    return text;
}

std::string AudioQueue::status_detail() const {
    return jobs_.empty() ? "支持文件夹批量导入" : format_file_size(total_size());
}

}  // namespace mu
