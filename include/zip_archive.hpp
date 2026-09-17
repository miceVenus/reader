#ifndef ZIP_ARCHIVE_HPP
#define ZIP_ARCHIVE_HPP

#include <memory>
#include <string>

// A small adapter around libzip.  The rest of the EPUB code only deals with
// this class and does not include zip.h or use zip_t directly.
class zip_archive {
public:
    zip_archive();
    explicit zip_archive(const char* path);
    ~zip_archive();

    zip_archive(const zip_archive&) = delete;
    zip_archive& operator=(const zip_archive&) = delete;
    zip_archive(zip_archive&& other) noexcept;
    zip_archive& operator=(zip_archive&& other) noexcept;

    bool is_open() const noexcept;
    std::string read_entry(const std::string& path) const;

private:
    struct impl;
    std::unique_ptr<impl> impl_;
};

#endif
