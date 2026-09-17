#ifndef EPUB_PARSER_HPP
#define EPUB_PARSER_HPP

#include "epub_book.hpp"

// The complete result of parsing META-INF/container.xml and content.opf.
// pugixml is deliberately not part of this interface.
struct parsed_book {
    std::string opf_path;
    epub_metadata metadata;
    epub_manifest manifest;
    epub_spine spine;
    epub_toc toc;
};

// The only interface between epub_book and the XML parser.
parsed_book parse_archive(zip_archive& archive);

#endif
