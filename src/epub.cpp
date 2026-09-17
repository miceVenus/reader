#include "epub.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <stdexcept>
#include <vector>

namespace {

epub_book* find_book(std::vector<std::unique_ptr<epub_book>>& books,
                     eb_t handle) noexcept {
    const auto iterator = std::find_if(
        books.begin(), books.end(),
        [handle](const std::unique_ptr<epub_book>& book) {
            return book.get() == handle;
        });

    return iterator == books.end() ? nullptr : iterator->get();
}

const epub_book* find_book(
    const std::vector<std::unique_ptr<epub_book>>& books,
    eb_t handle) noexcept {
    const auto iterator = std::find_if(
        books.begin(), books.end(),
        [handle](const std::unique_ptr<epub_book>& book) {
            return book.get() == handle;
        });

    return iterator == books.end() ? nullptr : iterator->get();
}

epub_book& require_book(std::vector<std::unique_ptr<epub_book>>& books,
                        eb_t handle) {
    epub_book* book = find_book(books, handle);
    if (book == nullptr) {
        throw std::invalid_argument("无效的 EPUB 句柄");
    }

    return *book;
}

const epub_book& require_book(
    const std::vector<std::unique_ptr<epub_book>>& books,
    eb_t handle) {
    const epub_book* book = find_book(books, handle);
    if (book == nullptr) {
        throw std::invalid_argument("无效的 EPUB 句柄");
    }

    return *book;
}

bool is_epub_file(const std::filesystem::path& path) {
    std::string extension = path.extension().string();
    std::transform(
        extension.begin(), extension.end(), extension.begin(),
        [](unsigned char character) {
            return static_cast<char>(std::tolower(character));
        });
    return extension == ".epub";
}

} // namespace

epub::epub() = default;

epub::~epub() {
    close_all();
}

epub::epub(epub&& other) noexcept = default;

epub& epub::operator=(epub&& other) noexcept = default;

eb_t epub::open(const char* path) {
    const std::string key = epub_book::make_cache_key(path);
    for (const std::unique_ptr<epub_book>& book : opened_books_) {
        if (book->cache_key() == key) {
            return book.get();
        }
    }

    std::unique_ptr<epub_book> candidate(new epub_book(path));
    eb_t handle = candidate.get();
    opened_books_.push_back(std::move(candidate));
    return handle;
}

std::vector<eb_t> epub::open_dir(const char* directory) {
    if (directory == nullptr || *directory == '\0') {
        throw std::invalid_argument("EPUB 目录不能为空");
    }

    namespace filesystem = std::filesystem;
    const filesystem::path directory_path(directory);
    std::error_code error;
    if (!filesystem::is_directory(directory_path, error)) {
        if (error) {
            throw std::runtime_error(
                "无法访问 EPUB 目录 '" + directory_path.string() +
                "': " + error.message());
        }
        throw std::invalid_argument(
            "路径不是目录: " + directory_path.string());
    }

    std::vector<filesystem::path> paths;
    for (const filesystem::directory_entry& entry :
         filesystem::directory_iterator(directory_path)) {
        std::error_code entry_error;
        if (entry.is_regular_file(entry_error) && !entry_error &&
            is_epub_file(entry.path())) {
            paths.push_back(entry.path());
        }
    }

    std::sort(paths.begin(), paths.end(),
              [](const filesystem::path& left, const filesystem::path& right) {
                  return left.generic_string() < right.generic_string();
              });

    std::vector<eb_t> handles;
    handles.reserve(paths.size());
    const std::size_t old_size = opened_books_.size();
    try {
        for (const filesystem::path& path : paths) {
            const std::string path_string = path.string();
            const eb_t handle = open(path_string.c_str());
            if (std::find(handles.begin(), handles.end(), handle) ==
                handles.end()) {
                handles.push_back(handle);
            }
        }
    } catch (...) {
        opened_books_.erase(opened_books_.begin() + old_size,
                            opened_books_.end());
        throw;
    }

    return handles;
}

const std::vector<epub_content_item>& epub::read(eb_t handle) {
    return require_book(opened_books_, handle).read();
}

void epub::close(eb_t handle) noexcept {
    if (handle == nullptr) {
        return;
    }

    const auto iterator = std::find_if(
        opened_books_.begin(), opened_books_.end(),
        [handle](const std::unique_ptr<epub_book>& book) {
            return book.get() == handle;
        });

    if (iterator != opened_books_.end()) {
        opened_books_.erase(iterator);
    }
}

void epub::close_all() noexcept {
    opened_books_.clear();
}

bool epub::owns(eb_t handle) const noexcept {
    return handle != nullptr && find_book(opened_books_, handle) != nullptr;
}

std::size_t epub::size() const noexcept {
    return opened_books_.size();
}

const std::string& epub::path(eb_t handle) const {
    return require_book(opened_books_, handle).path();
}

const std::string& epub::opf_path(eb_t handle) const {
    return require_book(opened_books_, handle).opf_path();
}

const epub_metadata& epub::metadata(eb_t handle) const {
    return require_book(opened_books_, handle).metadata();
}

const epub_manifest& epub::manifest(eb_t handle) const {
    return require_book(opened_books_, handle).manifest();
}

const epub_spine& epub::spine(eb_t handle) const {
    return require_book(opened_books_, handle).spine();
}

const epub_toc& epub::toc(eb_t handle) const {
    return require_book(opened_books_, handle).toc();
}

const std::vector<epub_content_item>& epub::content(eb_t handle) const {
    return require_book(opened_books_, handle).content();
}
