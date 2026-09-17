#ifndef EPUB_BOOK_HPP
#define EPUB_BOOK_HPP

#include "zip_archive.hpp"

#include <cstddef>
#include <string>
#include <unordered_map>
#include <vector>

// A creator can be an author, translator, illustrator, etc.  The role is
// kept as metadata instead of assuming that every creator is an author.
struct epub_creator {
    std::string id;
    std::string value;
    std::string role;
    std::string file_as;
};

struct epub_identifier {
    std::string id;
    std::string value;
    std::string scheme;
};

// This intentionally contains the useful publication-level metadata only.
// Unknown EPUB metadata is not required for opening and reading a novel.
struct epub_metadata {
    std::string title;
    std::vector<epub_creator> creators;
    std::vector<epub_creator> contributors;
    std::string language;
    std::vector<epub_identifier> identifiers;
    std::string publisher;
    std::string description;
    std::vector<std::string> subjects;
    std::string date;
    std::string modified;
};

struct epub_manifest_item {
    std::string id;
    std::string href;          // The href as written in the OPF file.
    std::string archive_path;  // href resolved relative to the OPF file.
    std::string media_type;
    std::string title;         // Primary title from the NCX, if available.
    std::vector<std::string> properties;
    std::string fallback;
    std::string media_overlay;
};

struct epub_manifest {
    std::vector<epub_manifest_item> items;

    // Maps an item id to its index in items.  spine idrefs can therefore be
    // resolved without repeatedly scanning the manifest.
    std::unordered_map<std::string, std::size_t> id_to_index;
};

struct epub_spine_item {
    std::string idref;
    bool linear = true;
    std::vector<std::string> properties;
};

struct epub_spine {
    std::string id;
    std::string page_progression_direction;
    std::string toc; // EPUB 2 compatibility attribute.
    std::vector<epub_spine_item> items;
};

// One item in the hierarchical EPUB 2 NCX table of contents.
struct epub_toc_item {
    std::string id;
    std::string title;
    std::string href;          // The original NCX content@src value.
    std::string archive_path;  // href resolved relative to toc.ncx.
    std::string fragment;      // The part after '#', if present.
    std::string manifest_id;
    int play_order = 0;
    std::vector<epub_toc_item> children;
};

struct epub_toc {
    std::vector<epub_toc_item> items;
};

// The raw content resource selected by one spine item.  HTML/XHTML parsing
// and pagination can be added on top of source later without changing the
// EPUB archive/manifest layer.
struct epub_content_item {
    std::string id;
    std::string archive_path;
    std::string media_type;
    bool linear = true;
    std::string source;
};

// One opened EPUB book.  The parser implementation is kept in
// epub_parser.cpp; this class only exposes the parsed model and keeps the
// archive/resource state private.
class epub_book {
public:
    explicit epub_book(const char* path);
    ~epub_book();

    epub_book(const epub_book&) = delete;
    epub_book& operator=(const epub_book&) = delete;
    epub_book(epub_book&& other) noexcept;
    epub_book& operator=(epub_book&& other) noexcept;

    const std::string& path() const noexcept;
    const std::string& opf_path() const noexcept;
    const epub_metadata& metadata() const noexcept;
    const epub_manifest& manifest() const noexcept;
    const epub_spine& spine() const noexcept;
    const epub_toc& toc() const noexcept;
    const std::vector<epub_content_item>& content() const noexcept;

private:
    friend class epub;

    // These operations belong to one book.  epub only manages the lifetime
    // and cache of the object.
    void open(const char* path);
    const std::vector<epub_content_item>& read();
    static std::string make_cache_key(const char* path);
    const std::string& cache_key() const noexcept;

    zip_archive archive_;
    std::string source_path_;
    std::string cache_key_;
    std::string opf_path_;
    epub_metadata metadata_;
    epub_manifest manifest_;
    epub_spine spine_;
    epub_toc toc_;
    std::vector<epub_content_item> content_;
};


using eb_t = epub_book*;

#endif
