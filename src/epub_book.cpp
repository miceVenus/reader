#include "epub_book.hpp"

#include "epub_parser.hpp"

#include <filesystem>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

std::vector<epub_content_item> read_content(
    zip_archive& archive, const epub_manifest& manifest,
    const epub_spine& spine) {
    std::vector<epub_content_item> content;
    content.reserve(spine.items.size());

    for (const epub_spine_item& spine_item : spine.items) {
        const auto manifest_iterator =
            manifest.id_to_index.find(spine_item.idref);
        if (manifest_iterator == manifest.id_to_index.end() ||
            manifest_iterator->second >= manifest.items.size()) {
            throw std::runtime_error(
                "spine 引用了不存在的 manifest item: " +
                spine_item.idref);
        }

        const epub_manifest_item& manifest_item =
            manifest.items[manifest_iterator->second];
        if (manifest_item.archive_path.empty()) {
            throw std::runtime_error(
                "manifest item 没有可读取的归档路径: " + manifest_item.id);
        }

        epub_content_item item;
        item.id = manifest_item.id;
        item.archive_path = manifest_item.archive_path;
        item.media_type = manifest_item.media_type;
        item.linear = spine_item.linear;
        item.source = archive.read_entry(item.archive_path);
        content.push_back(std::move(item));
    }

    return content;
}

} // namespace

epub_book::epub_book(const char* path) {
    open(path);
}

epub_book::~epub_book() = default;

epub_book::epub_book(epub_book&& other) noexcept = default;

epub_book& epub_book::operator=(epub_book&& other) noexcept = default;

void epub_book::open(const char* path) {
    const std::string key = make_cache_key(path);

    // Parse into temporaries first.  A failed parse does not leave a partially
    // initialized epub_book behind.
    zip_archive archive(path);
    parsed_book parsed = parse_archive(archive);

    archive_ = std::move(archive);
    source_path_ = path;
    cache_key_ = key;
    opf_path_ = std::move(parsed.opf_path);
    metadata_ = std::move(parsed.metadata);
    manifest_ = std::move(parsed.manifest);
    spine_ = std::move(parsed.spine);
    toc_ = std::move(parsed.toc);
    content_.clear();
}

const std::vector<epub_content_item>& epub_book::read() {
    if (!archive_.is_open()) {
        throw std::runtime_error("EPUB 句柄没有打开");
    }

    std::vector<epub_content_item> content =
        read_content(archive_, manifest_, spine_);
    content_ = std::move(content);
    return content_;
}

std::string epub_book::make_cache_key(const char* path) {
    if (path == nullptr || *path == '\0') {
        throw std::invalid_argument("EPUB 路径不能为空");
    }

    const std::filesystem::path input(path);
    std::error_code error;
    const std::filesystem::path absolute =
        std::filesystem::absolute(input, error);
    if (error) {
        return input.lexically_normal().generic_string();
    }

    const std::filesystem::path canonical =
        std::filesystem::weakly_canonical(absolute, error);
    if (!error) {
        return canonical.generic_string();
    }

    return absolute.lexically_normal().generic_string();
}

const std::string& epub_book::path() const noexcept {
    return source_path_;
}

const std::string& epub_book::opf_path() const noexcept {
    return opf_path_;
}

const epub_metadata& epub_book::metadata() const noexcept {
    return metadata_;
}

const epub_manifest& epub_book::manifest() const noexcept {
    return manifest_;
}

const epub_spine& epub_book::spine() const noexcept {
    return spine_;
}

const epub_toc& epub_book::toc() const noexcept {
    return toc_;
}

const std::vector<epub_content_item>& epub_book::content() const noexcept {
    return content_;
}

const std::string& epub_book::cache_key() const noexcept {
    return cache_key_;
}
