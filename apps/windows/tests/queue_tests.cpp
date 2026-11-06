// Conversion queue rules and wording, mirroring AppModel.swift on macOS.
#include <string>
#include <vector>

#include "musicunlock/queue.h"
#include "musicunlock/text.h"
#include "test_support.h"

namespace {

mu::ImportScan scan_of(std::vector<std::pair<std::string, std::uint64_t>> files, std::size_t skipped = 0) {
    mu::ImportScan scan;
    for (auto& [name, size] : files) scan.files.push_back({mu::utf8_to_path("/示例/" + name), size});
    scan.skipped = skipped;
    return scan;
}

TEST(file_sizes_use_decimal_units) {
    CHECK_EQ(mu::format_file_size(512), std::string("512 字节"), "bytes");
    CHECK_EQ(mu::format_file_size(12'345), std::string("12 KB"), "kilobytes");
    CHECK_EQ(mu::format_file_size(28'400'000), std::string("28.4 MB"), "megabytes");
    CHECK_EQ(mu::format_file_size(999'999), std::string("1.0 MB"), "rounds up a unit");
    CHECK_EQ(mu::format_file_size(1'250'000'000), std::string("1.25 GB"), "gigabytes");
}

TEST(adding_files_reports_like_macos) {
    mu::AudioQueue queue;
    CHECK_EQ(queue.add(scan_of({{"远行的旋律.ncm", 28'400'000}, {"午后唱片.mflac", 36'800'000}})), std::string(), "plain add");
    CHECK_EQ(queue.add(scan_of({{"远行的旋律.ncm", 1}})), std::string("这些文件已在队列中。"), "duplicate");
    CHECK_EQ(queue.add(scan_of({}, 2)), std::string("没有找到支持的加密音频文件。可添加 NCM、QMC、MFLAC、MGG 等格式。"), "nothing");
    CHECK_EQ(queue.add(scan_of({{"旧日时光.mgg", 8'600'000}}, 3)), std::string("已添加 1 个文件，跳过 3 个其他格式的文件。"), "partial");
    CHECK_EQ(queue.jobs().size(), std::size_t{3}, "count");
    const auto& job = queue.jobs().front();
    CHECK_EQ(job.display_title(), std::string("远行的旋律"), "title from stem");
    CHECK_EQ(job.source_format(), std::string("NCM"), "format");
    CHECK_EQ(job.subtitle(), std::string("网易云 · 28.4 MB"), "subtitle");
    CHECK_EQ(queue.status_title(), std::string("3 个待转换，0 个已完成"), "idle summary");
    CHECK_EQ(queue.status_detail(), std::string("73.8 MB"), "total size");
}

TEST(serial_batch_stop_retry_and_filters) {
    mu::AudioQueue queue;
    CHECK_EQ(queue.status_title(), std::string("准备好，随时开始"), "empty");
    CHECK_EQ(queue.status_detail(), std::string("支持文件夹批量导入"), "empty hint");
    queue.add(scan_of({{"a.ncm", 1}, {"b.ncm", 1}, {"c.ncm", 1}}));
    const auto ids = queue.begin_batch();
    CHECK_EQ(ids.size(), std::size_t{3}, "pending");
    CHECK(queue.begin_batch().empty());
    CHECK(!queue.retry_failed());
    queue.job_started(ids[0]);
    CHECK_EQ(queue.status_title(), std::string("正在转换 1 / 3"), "progress");
    CHECK(!queue.remove(ids[0]));
    mu::ConversionOutput output{mu::utf8_to_path("/输出/a.flac"), "flac", {}};
    output.metadata.title = "标签标题";
    queue.job_succeeded(ids[0], output);
    queue.job_started(ids[1]);
    queue.job_failed(ids[1], "此文件未内嵌解密密钥，目前无法离线转换");
    queue.request_stop();
    CHECK_EQ(queue.status_title(), std::string("将在当前文件完成后停止"), "stopping");
    CHECK_EQ(queue.end_batch(), std::string("已停止。未开始的文件仍保留在队列中，可以继续转换。"), "stop notice");
    CHECK_EQ(queue.status_title(), std::string("1 个待转换，1 个已完成，1 个未完成"), "summary");
    CHECK_EQ(queue.find(ids[0])->display_title(), std::string("标签标题"), "tag title");
    CHECK_EQ(queue.find(ids[0])->detail, std::string("a.flac"), "output name");
    CHECK_EQ(queue.visible(mu::QueueFilter::waiting).size(), std::size_t{1}, "waiting filter");
    CHECK_EQ(queue.visible(mu::QueueFilter::done).size(), std::size_t{1}, "done filter");
    CHECK_EQ(queue.visible(mu::QueueFilter::failed).front()->id, ids[1], "failed filter");
    CHECK_EQ(queue.find(ids[0])->spoken_summary(), std::string("标签标题，网易云 · 1 字节，NCM，已完成，输出 FLAC"),
             "spoken done row");
    CHECK_EQ(queue.find(ids[1])->spoken_summary(),
             std::string("b，网易云 · 1 字节，NCM，未完成：此文件未内嵌解密密钥，目前无法离线转换"), "spoken failed row");
    CHECK_EQ(queue.find(ids[2])->spoken_summary(), std::string("c，网易云 · 1 字节，NCM，待转换"), "spoken waiting row");

    CHECK(queue.retry_failed());
    CHECK_EQ(queue.count(mu::JobState::waiting), std::size_t{2}, "retry resets failed");
    CHECK(queue.find(ids[1])->detail.empty());
    queue.begin_batch();
    CHECK_EQ(queue.end_batch(), std::string(), "no notice without stop");
    queue.clear_completed();
    CHECK_EQ(queue.jobs().size(), std::size_t{2}, "clear completed");
    queue.clear();
    CHECK(queue.jobs().empty());
}

}  // namespace
