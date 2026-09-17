#ifndef EPUB_HPP
#define EPUB_HPP

#include "epub_book.hpp"

class epub {
public:
    epub();
    ~epub();

    epub(const epub&) = delete;
    epub& operator=(const epub&) = delete;
    epub(epub&& other) noexcept;
    epub& operator=(epub&& other) noexcept;

    // Open one EPUB and return a borrowed handle.  The epub object owns the
    // handle; it becomes invalid after close(), close_all() or destruction of
    // this epub object.
    eb_t open(const char* path);

    // Open every file with a .epub extension in a directory.  The returned
    // handles are borrowed from this epub object and remain valid until they
    // are closed or this object is destroyed.
    std::vector<eb_t> open_dir(const char* directory);

    // Load the content resources referenced by the manifest/spine, in spine
    // order.  The returned XHTML/HTML is kept as raw source for the renderer.
    const std::vector<epub_content_item>& read(eb_t book);

    // Close one handle.  Passing nullptr or a handle owned by another epub
    // object is ignored.
    void close(eb_t book) noexcept;

    // Close all currently opened EPUBs.
    void close_all() noexcept;

    bool owns(eb_t book) const noexcept;
    std::size_t size() const noexcept;

    const std::string& path(eb_t book) const;
    const std::string& opf_path(eb_t book) const;
    const epub_metadata& metadata(eb_t book) const;
    const epub_manifest& manifest(eb_t book) const;
    const epub_spine& spine(eb_t book) const;
    const epub_toc& toc(eb_t book) const;
    const std::vector<epub_content_item>& content(eb_t book) const;

private:
    std::vector<std::unique_ptr<epub_book>> opened_books_;
};

#endif
