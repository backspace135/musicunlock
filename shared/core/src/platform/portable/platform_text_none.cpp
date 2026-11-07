// No platform text conversion: Android until phase 2 adds the JNI-backed version, and the Linux CI host.
// Callers already handle nullopt (pinyin is reported as unsupported; GBK lyric files fail to decode).
#include "detail.h"
#include "musicunlock/lyrics.h"

namespace mu {

std::optional<std::string> mandarin_to_latin(std::string_view) { return std::nullopt; }

namespace detail {

std::optional<std::string> gb18030_to_utf8(ByteSpan) { return std::nullopt; }

}  // namespace detail

}  // namespace mu
