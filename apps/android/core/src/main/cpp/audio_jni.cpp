// audio.h, conversion.h, import.h and queue.h for AudioFormats.kt, AudioQueue.kt and AudioConverter.kt.
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <memory>
#include <mutex>
#include <numeric>

#include <nlohmann/json.hpp>

#include "detail.h"
#include "jni_support.h"
#include "musicunlock/conversion.h"
#include "musicunlock/import.h"
#include "musicunlock/queue.h"
#include "musicunlock/text.h"

namespace mu::jni {

namespace {

std::mutex mask_mutex;
std::shared_ptr<const Bytes> kgm_mask = std::make_shared<Bytes>();  // guarded by mask_mutex; from the APK's assets

std::shared_ptr<const Bytes> current_mask() {
    std::lock_guard lock(mask_mutex);
    return kgm_mask;
}

jclass converted_class = nullptr;
jmethodID converted_init = nullptr;

constexpr NamingPolicy naming_policies[] = {NamingPolicy::original, NamingPolicy::artist_title, NamingPolicy::title};
constexpr JobState job_states[] = {JobState::waiting, JobState::running, JobState::done, JobState::failed};

template <class T, std::size_t N>
T pick(const T (&values)[N], jint index) {
    return index >= 0 && static_cast<std::size_t>(index) < N ? values[index] : values[0];
}

jint state_index(JobState state) {
    return static_cast<jint>(std::find(std::begin(job_states), std::end(job_states), state) - std::begin(job_states));
}

std::string dump(const nlohmann::json& json) {
    // Core strings are UTF-8 but file names may not be; never let the dump throw on them.
    return json.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
}

/// The source as one span: mapped when the descriptor is a regular file, so a 100 MB source does not also sit on
/// the heap while it is decoded; read otherwise (pipes from cloud document providers).
class Source {
public:
    explicit Source(int fd) {
        struct stat status {};
        if (fstat(fd, &status) != 0) fail(errno);
        if (S_ISREG(status.st_mode) && status.st_size > 0) {
            if (static_cast<unsigned long long>(status.st_size) > (std::size_t{1} << 32)) {
                detail::fail_invalid("文件过大（超过 4 GB）");
            }
            size_ = static_cast<std::size_t>(status.st_size);
            void* mapped = mmap(nullptr, size_, PROT_READ, MAP_PRIVATE, fd, 0);
            if (mapped != MAP_FAILED) {
                madvise(mapped, size_, MADV_SEQUENTIAL);
                mapped_ = static_cast<std::uint8_t*>(mapped);
                return;
            }
        }
        read_all(fd);
    }

    ~Source() {
        if (mapped_) munmap(mapped_, size_);
    }

    Source(const Source&) = delete;
    Source& operator=(const Source&) = delete;

    ByteSpan span() const { return mapped_ ? ByteSpan(mapped_, size_) : ByteSpan(buffer_); }

private:
    [[noreturn]] static void fail(int error) {
        detail::fail_invalid("无法读取文件（" + std::string(std::strerror(error)) + "）");
    }

    void read_all(int fd) {
        if (lseek(fd, 0, SEEK_SET) < 0 && errno != ESPIPE) fail(errno);
        std::uint8_t chunk[1 << 16];
        for (;;) {
            const ssize_t count = ::read(fd, chunk, sizeof chunk);
            if (count < 0) {
                if (errno == EINTR) continue;
                fail(errno);
            }
            if (count == 0) break;
            buffer_.insert(buffer_.end(), chunk, chunk + count);
            if (buffer_.size() > (std::size_t{1} << 32)) detail::fail_invalid("文件过大（超过 4 GB）");
        }
    }

    std::uint8_t* mapped_ = nullptr;
    std::size_t size_ = 0;
    Bytes buffer_;
};

// AudioFormats

jobjectArray supported_extensions(JNIEnv* env, jclass) {
    return guarded(env, [&] {
        const auto& extensions = mu::supported_extensions();
        jobjectArray array = env->NewObjectArray(static_cast<jsize>(extensions.size()), classes().string, nullptr);
        jsize index = 0;
        for (const auto& ext : extensions) {
            jstring text = string(env, ext);
            env->SetObjectArrayElement(array, index++, text);
            env->DeleteLocalRef(text);
        }
        return array;
    });
}

jboolean is_supported_extension(JNIEnv* env, jclass, jstring ext) {
    return guarded(env, [&] { return static_cast<jboolean>(mu::is_supported_extension(utf8(env, ext))); });
}

jboolean is_supported_file_name(JNIEnv* env, jclass, jstring name) {
    // The rule of import.cpp's has_supported_extension: the path's extension, so ".ncm" alone has none.
    return guarded(env, [&] {
        std::string ext = path_to_utf8(utf8_to_path(utf8(env, name)).extension());
        if (!ext.empty()) ext.erase(0, 1);
        return static_cast<jboolean>(mu::is_supported_extension(ext));
    });
}

jstring platform_for(JNIEnv* env, jclass, jstring ext) {
    return guarded(env, [&] { return string(env, mu::platform_for(utf8(env, ext))); });
}

jboolean natural_less(JNIEnv* env, jclass, jstring a, jstring b) {
    return guarded(env, [&] { return static_cast<jboolean>(mu::natural_less(utf8(env, a), utf8(env, b))); });
}

jintArray natural_order(JNIEnv* env, jclass, jobjectArray keys) {
    return guarded(env, [&] {
        const jsize count = env->GetArrayLength(keys);
        std::vector<std::string> values;
        values.reserve(static_cast<std::size_t>(count));
        for (jsize i = 0; i < count; ++i) {
            auto key = static_cast<jstring>(env->GetObjectArrayElement(keys, i));
            values.push_back(utf8(env, key));
            env->DeleteLocalRef(key);
        }
        std::vector<jint> order(values.size());
        std::iota(order.begin(), order.end(), 0);
        std::stable_sort(order.begin(), order.end(),
                         [&](jint x, jint y) { return mu::natural_less(values[x], values[y]); });
        jintArray array = env->NewIntArray(count);
        if (array) env->SetIntArrayRegion(array, 0, count, order.data());
        return array;
    });
}

jstring format_file_size(JNIEnv* env, jclass, jlong bytes) {
    return guarded(env, [&] { return string(env, mu::format_file_size(static_cast<std::uint64_t>(bytes))); });
}

jstring sanitize_file_stem(JNIEnv* env, jclass, jstring name) {
    return guarded(env, [&] { return string(env, mu::sanitize_file_stem(utf8(env, name))); });
}

jstring numbered_file_name(JNIEnv* env, jclass, jstring stem, jstring ext, jint suffix) {
    return guarded(env, [&] { return string(env, mu::numbered_file_name(utf8(env, stem), utf8(env, ext), suffix)); });
}

jint max_name_suffix(JNIEnv*, jclass) { return mu::max_name_suffix; }

jstring naming_label(JNIEnv* env, jclass, jint policy) {
    return guarded(env, [&] { return string(env, mu::naming_label(pick(naming_policies, policy))); });
}

jstring state_label(JNIEnv* env, jclass, jint state) {
    return guarded(env, [&] { return string(env, mu::state_label(pick(job_states, state))); });
}

jstring filter_label(JNIEnv* env, jclass, jint filter) {
    return guarded(env, [&] { return string(env, mu::filter_label(pick(all_filters, filter))); });
}

jboolean filter_includes(JNIEnv* env, jclass, jint filter, jint state) {
    return guarded(env, [&] {
        return static_cast<jboolean>(mu::filter_includes(pick(all_filters, filter), pick(job_states, state)));
    });
}

// AudioQueue

nlohmann::json job_json(const AudioJob& job) {
    return {{"id", job.id},
            {"source", path_to_utf8(job.source)},
            {"size", job.size},
            {"state", state_index(job.state)},
            {"output", path_to_utf8(job.output)},
            {"outputExtension", job.output_extension},
            {"title", job.title},
            {"detail", job.detail},
            {"displayTitle", job.display_title()},
            {"sourceFormat", job.source_format()},
            {"subtitle", job.subtitle()},
            {"spokenSummary", job.spoken_summary()},
            {"hasCover", !job.cover.empty()}};
}

jlong queue_create(JNIEnv* env, jclass) {
    return guarded(env, [&] { return make_handle(new AudioQueue()); });
}

void queue_destroy(JNIEnv*, jclass, jlong queue) { delete &handle<AudioQueue>(queue); }

jstring queue_add(JNIEnv* env, jclass, jlong queue, jobjectArray paths, jlongArray sizes, jint skipped) {
    return guarded(env, [&] {
        ImportScan scan;
        const jsize count = env->GetArrayLength(paths);
        std::vector<jlong> size_values(static_cast<std::size_t>(count));
        env->GetLongArrayRegion(sizes, 0, count, size_values.data());
        for (jsize i = 0; i < count; ++i) {
            auto path = static_cast<jstring>(env->GetObjectArrayElement(paths, i));
            scan.files.push_back({utf8_to_path(utf8(env, path)), static_cast<std::uint64_t>(size_values[i])});
            env->DeleteLocalRef(path);
        }
        scan.skipped = static_cast<std::size_t>(std::max(0, skipped));
        return string(env, handle<AudioQueue>(queue).add(scan));
    });
}

jstring queue_jobs(JNIEnv* env, jclass, jlong queue) {
    return guarded(env, [&] {
        nlohmann::json jobs = nlohmann::json::array();
        for (const auto& job : handle<AudioQueue>(queue).jobs()) jobs.push_back(job_json(job));
        return string(env, dump(jobs));
    });
}

jbyteArray queue_cover(JNIEnv* env, jclass, jlong queue, jlong id) {
    return guarded(env, [&]() -> jbyteArray {
        const AudioJob* job = handle<AudioQueue>(queue).find(static_cast<std::uint64_t>(id));
        return job && !job->cover.empty() ? byte_array(env, job->cover) : nullptr;
    });
}

jboolean queue_remove(JNIEnv* env, jclass, jlong queue, jlong id) {
    return guarded(env, [&] { return static_cast<jboolean>(handle<AudioQueue>(queue).remove(static_cast<std::uint64_t>(id))); });
}

void queue_clear_completed(JNIEnv* env, jclass, jlong queue) {
    guarded(env, [&] { handle<AudioQueue>(queue).clear_completed(); });
}

void queue_clear(JNIEnv* env, jclass, jlong queue) {
    guarded(env, [&] { handle<AudioQueue>(queue).clear(); });
}

jlongArray queue_begin_batch(JNIEnv* env, jclass, jlong queue) {
    return guarded(env, [&] {
        const auto ids = handle<AudioQueue>(queue).begin_batch();
        std::vector<jlong> values(ids.begin(), ids.end());
        jlongArray array = env->NewLongArray(static_cast<jsize>(values.size()));
        if (array) env->SetLongArrayRegion(array, 0, static_cast<jsize>(values.size()), values.data());
        return array;
    });
}

void queue_job_started(JNIEnv* env, jclass, jlong queue, jlong id) {
    guarded(env, [&] { handle<AudioQueue>(queue).job_started(static_cast<std::uint64_t>(id)); });
}

void queue_job_succeeded(JNIEnv* env, jclass, jlong queue, jlong id, jstring path, jstring extension, jstring title,
                         jstring artist, jstring album, jbyteArray cover) {
    guarded(env, [&] {
        ConversionOutput output{utf8_to_path(utf8(env, path)), utf8(env, extension),
                                {utf8(env, title), utf8(env, artist), utf8(env, album), std::nullopt}};
        if (cover) output.metadata.cover = bytes(env, cover);
        handle<AudioQueue>(queue).job_succeeded(static_cast<std::uint64_t>(id), output);
    });
}

void queue_job_failed(JNIEnv* env, jclass, jlong queue, jlong id, jstring message) {
    guarded(env, [&] { handle<AudioQueue>(queue).job_failed(static_cast<std::uint64_t>(id), utf8(env, message)); });
}

void queue_request_stop(JNIEnv* env, jclass, jlong queue) {
    guarded(env, [&] { handle<AudioQueue>(queue).request_stop(); });
}

jstring queue_end_batch(JNIEnv* env, jclass, jlong queue) {
    return guarded(env, [&] { return string(env, handle<AudioQueue>(queue).end_batch()); });
}

jboolean queue_retry_failed(JNIEnv* env, jclass, jlong queue) {
    return guarded(env, [&] { return static_cast<jboolean>(handle<AudioQueue>(queue).retry_failed()); });
}

/// running, stop_requested, batch_finished, batch_count, total_size: the numbers the footer needs, in one call.
jlongArray queue_status(JNIEnv* env, jclass, jlong queue) {
    return guarded(env, [&] {
        const AudioQueue& q = handle<AudioQueue>(queue);
        const jlong values[] = {q.running(), q.stop_requested(), static_cast<jlong>(q.batch_finished()),
                                static_cast<jlong>(q.batch_count()), static_cast<jlong>(q.total_size())};
        jlongArray array = env->NewLongArray(5);
        if (array) env->SetLongArrayRegion(array, 0, 5, values);
        return array;
    });
}

jstring queue_status_title(JNIEnv* env, jclass, jlong queue) {
    return guarded(env, [&] { return string(env, handle<AudioQueue>(queue).status_title()); });
}

jstring queue_status_detail(JNIEnv* env, jclass, jlong queue) {
    return guarded(env, [&] { return string(env, handle<AudioQueue>(queue).status_detail()); });
}

// AudioConverter

void set_kgm_mask(JNIEnv* env, jclass, jbyteArray mask) {
    guarded(env, [&] {
        auto value = std::make_shared<const Bytes>(bytes(env, mask));
        std::lock_guard lock(mask_mutex);
        kgm_mask = std::move(value);
    });
}

jobject convert_descriptor(JNIEnv* env, jclass, jint fd, jstring source_name, jstring directory, jint naming) {
    return guarded(env, [&]() -> jobject {
        const auto mask = current_mask();
        ConversionOutput output;
        {
            const Source source(fd);
            output = convert_data(source.span(), utf8(env, source_name), utf8_to_path(utf8(env, directory)),
                                  pick(naming_policies, naming), *mask);
        }
        const AudioMetadata& meta = output.metadata;
        jstring path = string(env, path_to_utf8(output.path));
        jstring ext = string(env, output.file_extension);
        jstring title = string(env, meta.title);
        jstring artist = string(env, meta.artist);
        jstring album = string(env, meta.album);
        jbyteArray cover = meta.cover ? byte_array(env, *meta.cover) : nullptr;
        if (env->ExceptionCheck()) return nullptr;
        return env->NewObject(converted_class, converted_init, path, ext, title, artist, album, cover);
    });
}

#define NATIVE(name, signature, function) {name, signature, reinterpret_cast<void*>(function)}

const JNINativeMethod format_methods[] = {
    NATIVE("nativeSupportedExtensions", "()[Ljava/lang/String;", supported_extensions),
    NATIVE("nativeIsSupportedExtension", "(Ljava/lang/String;)Z", is_supported_extension),
    NATIVE("nativeIsSupportedFileName", "(Ljava/lang/String;)Z", is_supported_file_name),
    NATIVE("nativePlatformFor", "(Ljava/lang/String;)Ljava/lang/String;", platform_for),
    NATIVE("nativeNaturalLess", "(Ljava/lang/String;Ljava/lang/String;)Z", natural_less),
    NATIVE("nativeNaturalOrder", "([Ljava/lang/String;)[I", natural_order),
    NATIVE("nativeFormatFileSize", "(J)Ljava/lang/String;", format_file_size),
    NATIVE("nativeSanitizeFileStem", "(Ljava/lang/String;)Ljava/lang/String;", sanitize_file_stem),
    NATIVE("nativeNumberedFileName", "(Ljava/lang/String;Ljava/lang/String;I)Ljava/lang/String;", numbered_file_name),
    NATIVE("nativeMaxNameSuffix", "()I", max_name_suffix),
    NATIVE("nativeNamingLabel", "(I)Ljava/lang/String;", naming_label),
    NATIVE("nativeStateLabel", "(I)Ljava/lang/String;", state_label),
    NATIVE("nativeFilterLabel", "(I)Ljava/lang/String;", filter_label),
    NATIVE("nativeFilterIncludes", "(II)Z", filter_includes),
};

const JNINativeMethod queue_methods[] = {
    NATIVE("nativeCreate", "()J", queue_create),
    NATIVE("nativeDestroy", "(J)V", queue_destroy),
    NATIVE("nativeAdd", "(J[Ljava/lang/String;[JI)Ljava/lang/String;", queue_add),
    NATIVE("nativeJobs", "(J)Ljava/lang/String;", queue_jobs),
    NATIVE("nativeCover", "(JJ)[B", queue_cover),
    NATIVE("nativeRemove", "(JJ)Z", queue_remove),
    NATIVE("nativeClearCompleted", "(J)V", queue_clear_completed),
    NATIVE("nativeClear", "(J)V", queue_clear),
    NATIVE("nativeBeginBatch", "(J)[J", queue_begin_batch),
    NATIVE("nativeJobStarted", "(JJ)V", queue_job_started),
    NATIVE("nativeJobSucceeded",
           "(JJLjava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;[B)V",
           queue_job_succeeded),
    NATIVE("nativeJobFailed", "(JJLjava/lang/String;)V", queue_job_failed),
    NATIVE("nativeRequestStop", "(J)V", queue_request_stop),
    NATIVE("nativeEndBatch", "(J)Ljava/lang/String;", queue_end_batch),
    NATIVE("nativeRetryFailed", "(J)Z", queue_retry_failed),
    NATIVE("nativeStatus", "(J)[J", queue_status),
    NATIVE("nativeStatusTitle", "(J)Ljava/lang/String;", queue_status_title),
    NATIVE("nativeStatusDetail", "(J)Ljava/lang/String;", queue_status_detail),
};

const JNINativeMethod converter_methods[] = {
    NATIVE("nativeSetKgmMask", "([B)V", set_kgm_mask),
    NATIVE("nativeConvertDescriptor",
           "(ILjava/lang/String;Ljava/lang/String;I)Lio/github/backspace135/musicunlock/core/ConvertedAudio;",
           convert_descriptor),
};

}  // namespace

void register_audio(JNIEnv* env) {
    register_natives(env, "AudioFormats", format_methods);
    register_natives(env, "AudioQueue", queue_methods);
    register_natives(env, "AudioConverter", converter_methods);
    converted_class = find_class(env, "io/github/backspace135/musicunlock/core/ConvertedAudio");
    converted_init = env->GetMethodID(converted_class, "<init>",
                                      "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;"
                                      "Ljava/lang/String;[B)V");
    if (!converted_init) std::abort();
}

}  // namespace mu::jni
