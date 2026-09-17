#include "zip_archive.hpp"

#include <limits>
#include <stdexcept>
#include <string>

#include <zip.h>

struct zip_archive::impl {
    zip_t* archive = nullptr;
};

zip_archive::zip_archive()
    : impl_(new impl()) {}

zip_archive::zip_archive(const char* path)
    : impl_(new impl()) {
    if (path == nullptr || *path == '\0') {
        throw std::invalid_argument("EPUB 路径不能为空");
    }

    int error_code = 0;
    impl_->archive = zip_open(path, ZIP_RDONLY, &error_code);
    if (impl_->archive != nullptr) {
        return;
    }

    zip_error_t error;
    zip_error_init_with_code(&error, error_code);
    const std::string message = zip_error_strerror(&error);
    zip_error_fini(&error);

    throw std::runtime_error(
        "无法打开 EPUB 文件 '" + std::string(path) + "': " + message);
}

zip_archive::~zip_archive() {
    if (impl_ != nullptr && impl_->archive != nullptr) {
        zip_close(impl_->archive);
        impl_->archive = nullptr;
    }
}

zip_archive::zip_archive(zip_archive&& other) noexcept = default;

zip_archive& zip_archive::operator=(zip_archive&& other) noexcept = default;

bool zip_archive::is_open() const noexcept {
    return impl_ != nullptr && impl_->archive != nullptr;
}

std::string zip_archive::read_entry(const std::string& path) const {
    if (!is_open()) {
        throw std::runtime_error("ZIP 归档没有打开");
    }

    zip_file_t* file =
        zip_fopen(impl_->archive, path.c_str(), ZIP_FL_ENC_UTF_8);
    if (file == nullptr) {
        throw std::runtime_error(
            "无法打开 EPUB 内部文件 '" + path + "': " +
            zip_strerror(impl_->archive));
    }

    std::string content;

    zip_stat_t stat;
    zip_stat_init(&stat);
    if (zip_stat(impl_->archive, path.c_str(), ZIP_FL_ENC_UTF_8, &stat) == 0 &&
        stat.size <= static_cast<zip_uint64_t>(
                         std::numeric_limits<std::size_t>::max())) {
        content.reserve(static_cast<std::size_t>(stat.size));
    }

    char buffer[8192];
    while (true) {
        const zip_int64_t bytes_read = zip_fread(file, buffer, sizeof(buffer));
        if (bytes_read < 0) {
            const std::string message = zip_file_strerror(file);
            zip_fclose(file);
            throw std::runtime_error(
                "读取 EPUB 内部文件 '" + path + "' 失败: " + message);
        }

        if (bytes_read == 0) {
            break;
        }

        content.append(buffer, static_cast<std::size_t>(bytes_read));
    }

    if (zip_fclose(file) < 0) {
        throw std::runtime_error(
            "关闭 EPUB 内部文件 '" + path + "' 失败: " +
            zip_strerror(impl_->archive));
    }

    return content;
}
