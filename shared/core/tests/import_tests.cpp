// Import scanning used by the app and the CLI (mirrors AppModel.importURLs on macOS).
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "musicunlock/import.h"
#include "musicunlock/text.h"
#include "test_support.h"

namespace fs = std::filesystem;

namespace {

void touch(const fs::path& path, std::size_t size = 4) {
    fs::create_directories(path.parent_path());
    std::ofstream(path, std::ios::binary) << std::string(size, 'x');
}

std::vector<std::string> names(const mu::ImportScan& scan) {
    std::vector<std::string> out;
    for (const auto& file : scan.files) out.push_back(mu::path_to_utf8(file.path.filename()));
    return out;
}

TEST(natural_order_matches_explorer) {
    CHECK(mu::natural_less("track2.ncm", "track10.ncm"));
    CHECK(!mu::natural_less("track10.ncm", "track2.ncm"));
    CHECK(mu::natural_less("Alpha.ncm", "beta.ncm"));
    CHECK(mu::natural_less("a01.ncm", "a2.ncm"));
    CHECK(mu::natural_less("歌曲 9", "歌曲 10"));
    CHECK(!mu::natural_less("same", "same"));
}

TEST(import_expands_folders_skips_hidden_and_deduplicates) {
    const fs::path root = mu::utf8_to_path(MU_TEST_SCRATCH) / "import";
    fs::remove_all(root);
    // MSVC reads narrow path strings in the ANSI code page, so non-ASCII names go through utf8_to_path.
    const fs::path album = root / mu::utf8_to_path("专辑");
    touch(album / mu::utf8_to_path("10 歌.ncm"), 10);
    touch(album / mu::utf8_to_path("2 歌.mflac"));
    touch(album / mu::utf8_to_path("封面.jpg"));
    touch(album / mu::utf8_to_path(".隐藏.ncm"));
    touch(album / ".cache" / "inside.ncm");
    touch(root / mu::utf8_to_path("单曲.QMC0"));
    touch(root / mu::utf8_to_path("说明.txt"));

    const auto scan = mu::scan_import({album, root / mu::utf8_to_path("单曲.QMC0"), album / mu::utf8_to_path("2 歌.mflac"),
                                       root / mu::utf8_to_path("说明.txt"), root / mu::utf8_to_path("不存在.ncm")});
    CHECK_EQ(names(scan), (std::vector<std::string>{"2 歌.mflac", "10 歌.ncm", "单曲.QMC0"}), "files");
    CHECK_EQ(scan.skipped, std::size_t{3}, "jpg, txt and the missing path");
    CHECK_EQ(scan.files.at(1).size, std::uint64_t{10}, "size");
    CHECK(scan.files.at(0).path.is_absolute());

    std::size_t skipped = 0;
    CHECK_EQ(mu::find_audio_files(root, &skipped).size(), std::size_t{3}, "recursive find");
    CHECK_EQ(skipped, std::size_t{2}, "jpg and txt");
    fs::remove_all(root);
}

}  // namespace
