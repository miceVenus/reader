#include "epub_parser.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <pugixml.hpp>

namespace {

constexpr const char* kContainerNamespace =
    "urn:oasis:names:tc:opendocument:xmlns:container";
constexpr const char* kOpfNamespace = "http://www.idpf.org/2007/opf";
constexpr const char* kDublinCoreNamespace =
    "http://purl.org/dc/elements/1.1/";
constexpr const char* kNcxNamespace =
    "http://www.daisy.org/z3986/2005/ncx/";
constexpr const char* kXmlNamespace = "http://www.w3.org/XML/1998/namespace";

std::string trim_copy(const std::string& input) {
    std::size_t begin = 0;
    while (begin < input.size() &&
           std::isspace(static_cast<unsigned char>(input[begin]))) {
        ++begin;
    }

    std::size_t end = input.size();
    while (end > begin &&
           std::isspace(static_cast<unsigned char>(input[end - 1]))) {
        --end;
    }

    return input.substr(begin, end - begin);
}

std::vector<std::string> split_whitespace(const std::string& input) {
    std::istringstream stream(input);
    std::vector<std::string> result;
    std::string token;

    while (stream >> token) {
        result.push_back(token);
    }

    return result;
}

std::pair<std::string, std::string> split_qname(const char* qname) {
    const std::string name = qname == nullptr ? "" : qname;
    const std::size_t colon = name.find(':');

    if (colon == std::string::npos) {
        return std::make_pair(std::string(), name);
    }

    return std::make_pair(name.substr(0, colon), name.substr(colon + 1));
}

std::string namespace_uri_for_prefix(const pugi::xml_node& node,
                                     const std::string& prefix) {
    if (prefix == "xml") {
        return kXmlNamespace;
    }

    for (pugi::xml_node current = node; current;
         current = current.parent()) {
        const std::string declaration_name =
            prefix.empty() ? "xmlns" : "xmlns:" + prefix;
        const pugi::xml_attribute declaration =
            current.attribute(declaration_name.c_str());

        if (declaration) {
            return declaration.value();
        }
    }

    return std::string();
}

bool node_matches(const pugi::xml_node& node,
                  const char* namespace_uri,
                  const char* expected_local_name) {
    const std::pair<std::string, std::string> name = split_qname(node.name());
    if (name.second != expected_local_name) {
        return false;
    }

    // A namespace prefix is only an alias, so compare its URI instead.
    const std::string actual_namespace =
        namespace_uri_for_prefix(node, name.first);
    if (actual_namespace == namespace_uri) {
        return true;
    }

    // Tolerate EPUBs with missing namespace declarations.
    return actual_namespace.empty();
}

pugi::xml_node first_child(const pugi::xml_node& parent,
                           const char* namespace_uri,
                           const char* local_name) {
    for (pugi::xml_node child : parent.children()) {
        if (node_matches(child, namespace_uri, local_name)) {
            return child;
        }
    }

    return pugi::xml_node();
}

pugi::xml_attribute attribute_in_namespace(const pugi::xml_node& node,
                                           const char* namespace_uri,
                                           const char* expected_local_name) {
    for (pugi::xml_attribute attribute : node.attributes()) {
        const std::pair<std::string, std::string> name =
            split_qname(attribute.name());

        if (name.second != expected_local_name) {
            continue;
        }

        if (name.first.empty()) {
            if (*namespace_uri == '\0') {
                return attribute;
            }
        } else if (namespace_uri_for_prefix(node, name.first) ==
                   namespace_uri) {
            return attribute;
        }
    }

    // Also handle parser configurations where namespace declarations are not
    // exposed as ordinary attributes.
    if (*namespace_uri != '\0') {
        for (pugi::xml_attribute attribute : node.attributes()) {
            if (std::string(attribute.name()) ==
                std::string("opf:") + expected_local_name) {
                return attribute;
            }
        }
    }

    return pugi::xml_attribute();
}

std::string attribute_value(const pugi::xml_node& node,
                            const char* namespace_uri,
                            const char* local_name) {
    const pugi::xml_attribute attribute =
        attribute_in_namespace(node, namespace_uri, local_name);
    return attribute ? attribute.value() : std::string();
}

std::string text_value(const pugi::xml_node& node) {
    return trim_copy(node.child_value());
}

pugi::xml_document parse_xml(const std::string& content,
                             const std::string& description) {
    pugi::xml_document document;
    const pugi::xml_parse_result result =
        document.load_buffer(content.data(), content.size());

    if (!result) {
        throw std::runtime_error(
            "解析 " + description + " 失败: " + result.description());
    }

    return document;
}

std::string parent_path(const std::string& path) {
    const std::size_t slash = path.find_last_of('/');
    if (slash == std::string::npos) {
        return std::string();
    }

    return path.substr(0, slash);
}

std::string normalize_archive_path(const std::string& path) {
    std::vector<std::string> components;
    std::size_t begin = 0;

    while (begin <= path.size()) {
        const std::size_t slash = path.find('/', begin);
        const std::size_t end =
            slash == std::string::npos ? path.size() : slash;
        const std::string component = path.substr(begin, end - begin);

        if (component.empty() || component == ".") {
            // Ignore empty and current-directory components.
        } else if (component == "..") {
            if (!components.empty()) {
                components.pop_back();
            }
        } else {
            components.push_back(component);
        }

        if (slash == std::string::npos) {
            break;
        }
        begin = slash + 1;
    }

    std::string normalized;
    for (std::size_t i = 0; i < components.size(); ++i) {
        if (i != 0) {
            normalized += '/';
        }
        normalized += components[i];
    }

    return normalized;
}

std::string resolve_archive_path(const std::string& opf_path,
                                 const std::string& href) {
    std::string resource = href;

    const std::size_t fragment = resource.find('#');
    if (fragment != std::string::npos) {
        resource.erase(fragment);
    }

    const std::size_t query = resource.find('?');
    if (query != std::string::npos) {
        resource.erase(query);
    }

    if (resource.empty() || resource.find("://") != std::string::npos ||
        resource.compare(0, 5, "data:") == 0) {
        return std::string();
    }

    std::replace(resource.begin(), resource.end(), '\\', '/');

    const std::string base = parent_path(opf_path);
    if (!base.empty()) {
        resource = base + '/' + resource;
    }

    return normalize_archive_path(resource);
}

struct toc_target {
    std::string archive_path;
    std::string fragment;
};

toc_target resolve_toc_target(const std::string& ncx_path,
                             const std::string& href) {
    std::string reference = href;
    std::string fragment;

    const std::size_t fragment_position = reference.find('#');
    if (fragment_position != std::string::npos) {
        fragment = reference.substr(fragment_position + 1);
        reference.erase(fragment_position);
    }

    const std::size_t query_position = reference.find('?');
    if (query_position != std::string::npos) {
        reference.erase(query_position);
    }

    toc_target target;
    target.archive_path = resolve_archive_path(ncx_path, reference);
    target.fragment = std::move(fragment);
    return target;
}

epub_creator parse_creator(const pugi::xml_node& node) {
    epub_creator creator;
    creator.id = attribute_value(node, "", "id");
    creator.value = text_value(node);
    creator.role = attribute_value(node, kOpfNamespace, "role");
    creator.file_as = attribute_value(node, kOpfNamespace, "file-as");

    return creator;
}

epub_metadata parse_metadata(const pugi::xml_node& metadata_node) {
    epub_metadata metadata;

    for (pugi::xml_node child : metadata_node.children()) {
        if (node_matches(child, kDublinCoreNamespace, "title")) {
            if (metadata.title.empty()) {
                metadata.title = text_value(child);
            }
        } else if (node_matches(child, kDublinCoreNamespace, "creator")) {
            metadata.creators.push_back(parse_creator(child));
        } else if (node_matches(child, kDublinCoreNamespace, "contributor")) {
            metadata.contributors.push_back(parse_creator(child));
        } else if (node_matches(child, kDublinCoreNamespace, "language")) {
            if (metadata.language.empty()) {
                metadata.language = text_value(child);
            }
        } else if (node_matches(child, kDublinCoreNamespace, "identifier")) {
            epub_identifier identifier;
            identifier.id = attribute_value(child, "", "id");
            identifier.value = text_value(child);
            identifier.scheme =
                attribute_value(child, kOpfNamespace, "scheme");
            metadata.identifiers.push_back(identifier);
        } else if (node_matches(child, kDublinCoreNamespace, "publisher")) {
            if (metadata.publisher.empty()) {
                metadata.publisher = text_value(child);
            }
        } else if (node_matches(child, kDublinCoreNamespace, "description")) {
            if (metadata.description.empty()) {
                metadata.description = text_value(child);
            }
        } else if (node_matches(child, kDublinCoreNamespace, "subject")) {
            metadata.subjects.push_back(text_value(child));
        } else if (node_matches(child, kDublinCoreNamespace, "date")) {
            if (metadata.date.empty()) {
                metadata.date = text_value(child);
            }
        }
    }

    // EPUB 3 stores role/file-as and modified time in meta properties.
    for (pugi::xml_node child : metadata_node.children()) {
        if (!node_matches(child, kOpfNamespace, "meta")) {
            continue;
        }

        const std::string property = attribute_value(child, "", "property");
        const std::string value = text_value(child);

        if (property == "dcterms:modified") {
            metadata.modified = value;
            continue;
        }

        if (property != "role" && property != "file-as") {
            continue;
        }

        std::string refines = attribute_value(child, "", "refines");
        if (!refines.empty() && refines[0] == '#') {
            refines.erase(0, 1);
        }

        for (epub_creator& creator : metadata.creators) {
            if (creator.id != refines) {
                continue;
            }

            if (property == "role") {
                creator.role = value;
            } else {
                creator.file_as = value;
            }
        }

        for (epub_creator& contributor : metadata.contributors) {
            if (contributor.id != refines) {
                continue;
            }

            if (property == "role") {
                contributor.role = value;
            } else {
                contributor.file_as = value;
            }
        }
    }

    return metadata;
}

epub_manifest parse_manifest(const pugi::xml_node& manifest_node,
                             const std::string& opf_path) {
    epub_manifest manifest;

    for (pugi::xml_node child : manifest_node.children()) {
        if (!node_matches(child, kOpfNamespace, "item")) {
            continue;
        }

        epub_manifest_item item;
        item.id = attribute_value(child, "", "id");
        item.href = attribute_value(child, "", "href");
        item.archive_path = resolve_archive_path(opf_path, item.href);
        item.media_type = attribute_value(child, "", "media-type");
        item.properties =
            split_whitespace(attribute_value(child, "", "properties"));
        item.fallback = attribute_value(child, "", "fallback");
        item.media_overlay =
            attribute_value(child, "", "media-overlay");

        const std::size_t index = manifest.items.size();
        manifest.items.push_back(item);
        if (!item.id.empty()) {
            manifest.id_to_index[item.id] = index;
        }
    }

    return manifest;
}

epub_spine parse_spine(const pugi::xml_node& spine_node) {
    epub_spine spine;
    spine.id = attribute_value(spine_node, "", "id");
    spine.page_progression_direction =
        attribute_value(spine_node, "", "page-progression-direction");
    spine.toc = attribute_value(spine_node, "", "toc");

    for (pugi::xml_node child : spine_node.children()) {
        if (!node_matches(child, kOpfNamespace, "itemref")) {
            continue;
        }

        epub_spine_item item;
        item.idref = attribute_value(child, "", "idref");
        item.linear = attribute_value(child, "", "linear") != "no";
        item.properties =
            split_whitespace(attribute_value(child, "", "properties"));
        spine.items.push_back(item);
    }

    return spine;
}

std::string manifest_id_for_path(const epub_manifest& manifest,
                                 const std::string& archive_path) {
    if (archive_path.empty()) {
        return std::string();
    }

    for (const epub_manifest_item& item : manifest.items) {
        if (item.archive_path == archive_path) {
            return item.id;
        }
    }

    return std::string();
}

epub_toc_item parse_toc_item(const pugi::xml_node& nav_point,
                             const std::string& ncx_path,
                             const epub_manifest& manifest) {
    epub_toc_item item;
    item.id = attribute_value(nav_point, "", "id");
    item.play_order = nav_point.attribute("playOrder").as_int();

    const pugi::xml_node nav_label =
        first_child(nav_point, kNcxNamespace, "navLabel");
    const pugi::xml_node label_text =
        first_child(nav_label, kNcxNamespace, "text");
    item.title = text_value(label_text);

    const pugi::xml_node content =
        first_child(nav_point, kNcxNamespace, "content");
    item.href = attribute_value(content, "", "src");

    const toc_target target = resolve_toc_target(ncx_path, item.href);
    item.archive_path = target.archive_path;
    item.fragment = target.fragment;
    item.manifest_id =
        manifest_id_for_path(manifest, item.archive_path);

    for (pugi::xml_node child : nav_point.children()) {
        if (node_matches(child, kNcxNamespace, "navPoint")) {
            item.children.push_back(
                parse_toc_item(child, ncx_path, manifest));
        }
    }

    return item;
}

epub_toc parse_toc(const std::string& ncx_content,
                   const std::string& ncx_path,
                   const epub_manifest& manifest) {
    const pugi::xml_document document = parse_xml(ncx_content, ncx_path);
    const pugi::xml_node ncx =
        first_child(document, kNcxNamespace, "ncx");
    if (!ncx) {
        throw std::runtime_error("NCX 文件中找不到 ncx 节点");
    }

    const pugi::xml_node nav_map =
        first_child(ncx, kNcxNamespace, "navMap");
    if (!nav_map) {
        throw std::runtime_error("NCX 文件中找不到 navMap 节点");
    }

    epub_toc toc;
    for (pugi::xml_node child : nav_map.children()) {
        if (node_matches(child, kNcxNamespace, "navPoint")) {
            toc.items.push_back(
                parse_toc_item(child, ncx_path, manifest));
        }
    }

    return toc;
}

void apply_toc_titles(const epub_toc_item& toc_item,
                      epub_manifest& manifest) {
    if (!toc_item.manifest_id.empty() && toc_item.fragment.empty()) {
        const auto iterator =
            manifest.id_to_index.find(toc_item.manifest_id);
        if (iterator != manifest.id_to_index.end() &&
            iterator->second < manifest.items.size() &&
            manifest.items[iterator->second].title.empty()) {
            manifest.items[iterator->second].title = toc_item.title;
        }
    }

    for (const epub_toc_item& child : toc_item.children) {
        apply_toc_titles(child, manifest);
    }
}

void apply_toc_titles(const epub_toc& toc, epub_manifest& manifest) {
    for (const epub_toc_item& item : toc.items) {
        apply_toc_titles(item, manifest);
    }
}

const epub_manifest_item* find_ncx_item(const epub_manifest& manifest,
                                        const epub_spine& spine) {
    if (!spine.toc.empty()) {
        const auto iterator = manifest.id_to_index.find(spine.toc);
        if (iterator != manifest.id_to_index.end() &&
            iterator->second < manifest.items.size()) {
            return &manifest.items[iterator->second];
        }
    }

    for (const epub_manifest_item& item : manifest.items) {
        if (item.media_type == "application/x-dtbncx+xml") {
            return &item;
        }
    }

    return nullptr;
}

std::string find_opf_path(const std::string& container_content) {
    const pugi::xml_document document =
        parse_xml(container_content, "META-INF/container.xml");
    const pugi::xml_node container =
        first_child(document, kContainerNamespace, "container");
    if (!container) {
        throw std::runtime_error("container.xml 中找不到 container 节点");
    }

    const pugi::xml_node rootfiles =
        first_child(container, kContainerNamespace, "rootfiles");
    if (!rootfiles) {
        throw std::runtime_error("container.xml 中找不到 rootfiles 节点");
    }

    pugi::xml_node first_rootfile;
    pugi::xml_node selected_rootfile;

    for (pugi::xml_node child : rootfiles.children()) {
        if (!node_matches(child, kContainerNamespace, "rootfile")) {
            continue;
        }

        if (!first_rootfile) {
            first_rootfile = child;
        }

        const std::string media_type =
            attribute_value(child, "", "media-type");
        if (media_type == "application/oebps-package+xml") {
            selected_rootfile = child;
            break;
        }
    }

    if (!selected_rootfile) {
        selected_rootfile = first_rootfile;
    }

    if (!selected_rootfile) {
        throw std::runtime_error("container.xml 中找不到 rootfile 节点");
    }

    const std::string path =
        attribute_value(selected_rootfile, "", "full-path");
    if (path.empty()) {
        throw std::runtime_error("rootfile 缺少 full-path 属性");
    }

    return path;
}

} // namespace

parsed_book parse_archive(zip_archive& archive) {
    const std::string container_content =
        archive.read_entry("META-INF/container.xml");
    const std::string opf_path = find_opf_path(container_content);
    const std::string opf_content = archive.read_entry(opf_path);
    const pugi::xml_document opf_document = parse_xml(opf_content, opf_path);

    const pugi::xml_node package =
        first_child(opf_document, kOpfNamespace, "package");
    if (!package) {
        throw std::runtime_error("OPF 文件中找不到 package 节点");
    }

    const pugi::xml_node metadata_node =
        first_child(package, kOpfNamespace, "metadata");
    const pugi::xml_node manifest_node =
        first_child(package, kOpfNamespace, "manifest");
    const pugi::xml_node spine_node =
        first_child(package, kOpfNamespace, "spine");

    if (!metadata_node || !manifest_node || !spine_node) {
        throw std::runtime_error(
            "OPF 文件缺少 metadata、manifest 或 spine 节点");
    }

    parsed_book book;
    book.opf_path = opf_path;
    book.metadata = parse_metadata(metadata_node);
    book.manifest = parse_manifest(manifest_node, opf_path);
    book.spine = parse_spine(spine_node);

    // EPUB 2 stores the hierarchical table of contents in an NCX resource.
    // It is optional for newer EPUBs, so absence of an NCX is not an error.
    const epub_manifest_item* ncx_item =
        find_ncx_item(book.manifest, book.spine);
    if (ncx_item != nullptr && !ncx_item->archive_path.empty()) {
        const std::string ncx_content =
            archive.read_entry(ncx_item->archive_path);
        book.toc = parse_toc(
            ncx_content, ncx_item->archive_path, book.manifest);
        apply_toc_titles(book.toc, book.manifest);
    }

    return book;
}
