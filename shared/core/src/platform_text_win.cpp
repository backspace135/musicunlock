// Pinyin through the ICU that ships with Windows, and GBK through the system code page tables.
// ICU is loaded at run time: icu.dll exists from Windows 10 1903, icuin.dll from 1703, so the
// app still starts on 1809 and only the pinyin option reports that it is unavailable.
#include <windows.h>

#include <memory>

#include "detail.h"
#include "musicunlock/lyrics.h"
#include "musicunlock/text.h"

namespace mu {

namespace {

using UErrorCode = int;
struct UTransliterator;
using OpenFn = UTransliterator* (*)(const char16_t*, std::int32_t, int, const char16_t*, std::int32_t, void*, UErrorCode*);
using TransFn = void (*)(const UTransliterator*, char16_t*, std::int32_t*, std::int32_t, std::int32_t, std::int32_t*,
                         UErrorCode*);
using CloseFn = void (*)(UTransliterator*);

struct IcuTransliteration {
    OpenFn open = nullptr;
    TransFn transform = nullptr;
    CloseFn close = nullptr;

    IcuTransliteration() {
        for (const wchar_t* name : {L"icu.dll", L"icuin.dll"}) {
            HMODULE module = LoadLibraryExW(name, nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
            if (!module) continue;
            open = reinterpret_cast<OpenFn>(GetProcAddress(module, "utrans_openU"));
            transform = reinterpret_cast<TransFn>(GetProcAddress(module, "utrans_transUChars"));
            close = reinterpret_cast<CloseFn>(GetProcAddress(module, "utrans_close"));
            if (open && transform && close) return;
            open = nullptr;
        }
    }
};

const IcuTransliteration& icu() {
    static const IcuTransliteration instance;
    return instance;
}

struct Closer {
    void operator()(UTransliterator* t) const { icu().close(t); }
};

/// ICU transliterators are not thread-safe; compiling Han-Latin is costly, so keep one per thread.
UTransliterator* han_latin() {
    thread_local std::unique_ptr<UTransliterator, Closer> instance = [] {
        UErrorCode status = 0;
        UTransliterator* opened = icu().open(u"Han-Latin", -1, 0 /* UTRANS_FORWARD */, nullptr, 0, nullptr, &status);
        if (status > 0 && opened) {  // U_FAILURE
            icu().close(opened);
            opened = nullptr;
        }
        return std::unique_ptr<UTransliterator, Closer>(status > 0 ? nullptr : opened);
    }();
    return instance.get();
}

}  // namespace

std::optional<std::string> mandarin_to_latin(std::string_view text) {
    if (!icu().open) return std::nullopt;
    UTransliterator* transliterator = han_latin();
    if (!transliterator) return std::nullopt;
    std::u16string buffer = utf8_to_utf16(text);
    // Pinyin with tone marks is at most a few units per Han character.
    std::int32_t length = static_cast<std::int32_t>(buffer.size());
    buffer.resize(buffer.size() * 8 + 16);
    std::int32_t limit = length;
    UErrorCode status = 0;
    icu().transform(transliterator, buffer.data(), &length, static_cast<std::int32_t>(buffer.size()), 0, &limit, &status);
    if (status > 0) return std::nullopt;
    buffer.resize(static_cast<std::size_t>(length));
    return utf16_to_utf8(buffer);
}

namespace detail {

std::optional<std::string> gb18030_to_utf8(ByteSpan bytes) {
    if (bytes.empty()) return std::string();
    constexpr UINT gb18030 = 54936;
    const auto* source = reinterpret_cast<const char*>(bytes.data());
    const int size = static_cast<int>(bytes.size());
    const int units = MultiByteToWideChar(gb18030, MB_ERR_INVALID_CHARS, source, size, nullptr, 0);
    if (units <= 0) return std::nullopt;
    std::u16string wide(static_cast<std::size_t>(units), u'\0');
    MultiByteToWideChar(gb18030, MB_ERR_INVALID_CHARS, source, size, reinterpret_cast<wchar_t*>(wide.data()), units);
    return utf16_to_utf8(wide);
}

}  // namespace detail

}  // namespace mu
