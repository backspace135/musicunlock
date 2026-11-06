// Development Mac backend: the same CoreFoundation transform the macOS app uses.
#include <CoreFoundation/CoreFoundation.h>

#include <vector>

#include "detail.h"
#include "musicunlock/lyrics.h"

namespace mu {

namespace {

std::optional<std::string> to_utf8(CFStringRef string) {
    if (!string) return std::nullopt;
    const CFIndex length = CFStringGetLength(string);
    const CFIndex capacity = CFStringGetMaximumSizeForEncoding(length, kCFStringEncodingUTF8) + 1;
    std::vector<char> buffer(static_cast<std::size_t>(capacity));
    if (!CFStringGetCString(string, buffer.data(), capacity, kCFStringEncodingUTF8)) return std::nullopt;
    return std::string(buffer.data());
}

}  // namespace

std::optional<std::string> mandarin_to_latin(std::string_view text) {
    CFMutableStringRef string = CFStringCreateMutable(kCFAllocatorDefault, 0);
    CFStringRef source = CFStringCreateWithBytes(kCFAllocatorDefault, reinterpret_cast<const UInt8*>(text.data()),
                                                 static_cast<CFIndex>(text.size()), kCFStringEncodingUTF8, false);
    std::optional<std::string> result;
    if (string && source) {
        CFStringAppend(string, source);
        if (CFStringTransform(string, nullptr, kCFStringTransformMandarinLatin, false)) result = to_utf8(string);
    }
    if (source) CFRelease(source);
    if (string) CFRelease(string);
    return result;
}

namespace detail {

std::optional<std::string> gb18030_to_utf8(ByteSpan bytes) {
    CFStringRef string = CFStringCreateWithBytes(kCFAllocatorDefault, bytes.data(), static_cast<CFIndex>(bytes.size()),
                                                 kCFStringEncodingGB_18030_2000, false);
    auto result = to_utf8(string);
    if (string) CFRelease(string);
    return result;
}

}  // namespace detail

}  // namespace mu
