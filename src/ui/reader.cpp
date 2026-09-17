#include "ui/reader.hpp"

#include <algorithm>
#include <ftxui/screen/string.hpp>

#include <functional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ui {
namespace {

struct chapter_location {
    std::string id;
    std::string archive_path;
    std::size_t begin = 0;
    std::size_t end = 0;
};

struct flattened_document {
    std::string text;
    std::vector<chapter_location> chapters;
};

const chapter_location* find_chapter(
    const epub_toc_item& toc_item,
    const std::vector<chapter_location>& chapters) {
    if (!toc_item.manifest_id.empty()) {
        for (const chapter_location& chapter : chapters) {
            if (chapter.id == toc_item.manifest_id) {
                return &chapter;
            }
        }
    }

    if (!toc_item.archive_path.empty()) {
        for (const chapter_location& chapter : chapters) {
            if (chapter.archive_path == toc_item.archive_path) {
                return &chapter;
            }
        }
    }

    return nullptr;
}

std::size_t find_fragment_offset(const std::string& text,
                                 const chapter_location& chapter,
                                 const std::string& fragment) {
    if (fragment.empty()) {
        return chapter.begin;
    }

    // This is only a temporary anchor lookup until XHTML is parsed into a
    // document model.  It handles the common id/name forms and otherwise
    // falls back to the beginning of the chapter resource.
    const std::vector<std::string> patterns = {
        "id=\"" + fragment + "\"",
        "id='" + fragment + "'",
        "name=\"" + fragment + "\"",
        "name='" + fragment + "'",
    };

    for (const std::string& pattern : patterns) {
        const std::size_t position = text.find(pattern, chapter.begin);
        if (position != std::string::npos &&
            position + pattern.size() <= chapter.end) {
            return position;
        }
    }

    return chapter.begin;
}

flattened_document flatten_book_content(epub& epub_reader, eb_t book) {
    const std::vector<epub_content_item>& content = epub_reader.read(book);
    const epub_manifest& manifest = epub_reader.manifest(book);

    flattened_document document;
    for (const epub_content_item& content_item : content) {
        const auto manifest_iterator =
            manifest.id_to_index.find(content_item.id);

        std::string chapter_title;
        if (manifest_iterator != manifest.id_to_index.end() &&
            manifest_iterator->second < manifest.items.size()) {
            chapter_title = manifest.items[manifest_iterator->second].title;
        }

        if (!document.text.empty()) {
            document.text += "\n\n";
        }
        if (!chapter_title.empty()) {
            document.text += "========== " + chapter_title +
                             " ==========\n\n";
        }

        chapter_location chapter;
        chapter.id = content_item.id;
        chapter.archive_path = content_item.archive_path;
        chapter.begin = document.text.size();
        document.text += content_item.source;
        chapter.end = document.text.size();
        document.chapters.push_back(std::move(chapter));
    }

    if (document.text.empty()) {
        document.text = "暂无可读取内容";
    }

    return document;
}

std::string display_or_unknown(const std::string& value) {
    return value.empty() ? "未知" : value;
}

struct reading_dimensions {
    int width = 1;
    int height = 1;
};

reading_dimensions get_reading_dimensions() {
    const ftxui::Dimensions terminal_size = ftxui::Terminal::Size();

    // The reading area is surrounded by the fixed-width TOC panel and two
    // borders.  Vertically, the outer title/separators/footer take four rows;
    // the reading panel header/separator/border take another four rows.
    constexpr int toc_panel_width = 32;
    constexpr int toc_panel_border_width = 2;
    constexpr int reading_panel_border_width = 2;
    constexpr int fixed_vertical_rows = 8;

    return {
        std::max(1, terminal_size.dimx - toc_panel_width -
                            toc_panel_border_width -
                            reading_panel_border_width),
        std::max(1, terminal_size.dimy - fixed_vertical_rows),
    };
}

}  // namespace

reader::reader(epub& epub_reader, eb_t book, std::size_t book_index)
    : book_index_(book_index), title_(epub_reader.metadata(book).title) {
    const epub_toc& toc = epub_reader.toc(book);
    flattened_document document = flatten_book_content(epub_reader, book);
    flattened_content_ = std::move(document.text);

    std::function<void(const std::vector<epub_toc_item>&, int)> append =
        [&](const std::vector<epub_toc_item>& items, int depth) {
            for (const epub_toc_item& item : items) {
                toc_entry entry;
                std::string title = item.title;
                if (title.empty()) {
                    title = item.href.empty() ? item.archive_path : item.href;
                }
                if (title.empty()) {
                    title = "未命名目录项";
                }
                entry.label =
                    std::string(static_cast<std::size_t>(depth * 2), ' ') +
                    title;

                const chapter_location* chapter =
                    find_chapter(item, document.chapters);
                if (chapter != nullptr) {
                    entry.source_offset = find_fragment_offset(
                        flattened_content_, *chapter, item.fragment);
                    entry.has_target = true;
                }

                toc_entries_.push_back(std::move(entry));
                append(item.children, depth + 1);
            }
        };
    append(toc.items, 0);
}

void reader::rebuild_layout(int content_width, int content_height) {
    const bool had_layout = !page_starts_.empty() && !lines_.empty();
    std::size_t anchor_offset = 0;
    if (had_layout) {
        const std::size_t page =
            std::min(current_page_, page_starts_.size() - 1);
        const std::size_t line = page_starts_[page];
        if (line < lines_.size()) {
            anchor_offset = lines_[line].begin;
        }
    }

    lines_.clear();
    page_starts_.clear();

    std::size_t line_begin = 0;
    while (true) {
        const std::size_t newline = flattened_content_.find('\n', line_begin);
        std::size_t line_end = newline == std::string::npos
                                   ? flattened_content_.size()
                                   : newline;

        // Treat CRLF as one line ending and do not render the CR character.
        if (line_end > line_begin &&
            flattened_content_[line_end - 1] == '\r') {
            --line_end;
        }

        const std::string_view source_line(
            flattened_content_.data() + line_begin,
            line_end - line_begin);

        if (source_line.empty()) {
            lines_.push_back({line_begin, line_end});
        } else {
            std::size_t wrapped_line_begin = line_begin;
            std::size_t glyph_offset = line_begin;
            int current_width = 0;

            for (const std::string& glyph :
                 ftxui::Utf8ToGlyphs(source_line)) {
                const int glyph_width = ftxui::string_width(glyph);

                if (current_width > 0 &&
                    current_width + glyph_width > content_width) {
                    lines_.push_back({wrapped_line_begin, glyph_offset});
                    wrapped_line_begin = glyph_offset;
                    current_width = 0;
                }

                glyph_offset += glyph.size();
                current_width += glyph_width;
            }

            lines_.push_back({wrapped_line_begin, line_end});
        }

        if (newline == std::string::npos) {
            break;
        }
        line_begin = newline + 1;
    }

    if (lines_.empty()) {
        lines_.push_back({0, 0});
    }

    const std::size_t page_height =
        static_cast<std::size_t>(std::max(1, content_height));
    for (std::size_t line = 0; line < lines_.size(); line += page_height) {
        page_starts_.push_back(line);
    }

    layout_width_ = content_width;
    layout_height_ = content_height;

    if (!had_layout) {
        current_page_ = 0;
    } else {
        const auto iterator = std::upper_bound(
            lines_.begin(), lines_.end(), anchor_offset,
            [](std::size_t offset, const line_range& line) {
                return offset < line.begin;
            });
        const std::size_t anchor_line =
            iterator == lines_.begin()
                ? 0
                : static_cast<std::size_t>(
                      std::distance(lines_.begin(), iterator - 1));
        current_page_ = std::min(anchor_line / page_height,
                                 page_starts_.size() - 1);
    }

    update_toc_pages();
    update_active_toc();
}

ftxui::Element reader::render_current_page() const {
    ftxui::Elements page_lines;

    if (page_starts_.empty() || lines_.empty()) {
        page_lines.push_back(ftxui::text("暂无可读取内容"));
        return ftxui::vbox(std::move(page_lines));
    }

    const std::size_t page =
        std::min(current_page_, page_starts_.size() - 1);
    const std::size_t first_line = page_starts_[page];
    const std::size_t last_line =
        page + 1 < page_starts_.size() ? page_starts_[page + 1]
                                       : lines_.size();

    page_lines.reserve(last_line - first_line);
    for (std::size_t line = first_line; line < last_line; ++line) {
        const line_range& range = lines_[line];
        page_lines.push_back(ftxui::text(
            flattened_content_.substr(range.begin, range.end - range.begin)));
    }

    return ftxui::vbox(std::move(page_lines));
}

ftxui::Element reader::render_toc_panel(std::size_t visible_rows) const {
    ftxui::Elements entries;
    const std::size_t visible = std::max<std::size_t>(1, visible_rows);

    if (toc_entries_.empty()) {
        entries.push_back(ftxui::text("未找到 NCX 目录") | ftxui::dim);
    } else {
        const std::size_t first =
            std::min(toc_scroll_offset_, toc_entries_.size() - 1);
        const std::size_t last =
            std::min(toc_entries_.size(), first + visible);

        entries.reserve(last - first);
        for (std::size_t index = first; index < last; ++index) {
            ftxui::Element entry = ftxui::text(toc_entries_[index].label);
            const bool is_active =
                active_toc_.has_value() && *active_toc_ == index;
            const bool is_selected =
                focus_ == focus::navigation && selected_toc_ == index;

            if (is_active) {
                entry = entry |
                        ftxui::bgcolor(ftxui::Color::RGB(44, 54, 59));
            }
            if (is_selected) {
                entry = entry |
                        ftxui::bgcolor(ftxui::Color::RGB(57, 68, 74)) |
                        ftxui::color(ftxui::Color::RGB(218, 224, 226));
            }
            entries.push_back(std::move(entry));
        }
    }

    std::string title = focus_ == focus::navigation ? "目录 [当前]" : "目录";
    if (toc_scroll_offset_ > 0) {
        title += " ↑";
    }
    if (toc_scroll_offset_ + visible < toc_entries_.size()) {
        title += " ↓";
    }

    const ftxui::Color border_color =
        focus_ == focus::navigation ? ftxui::Color::RGB(57, 68, 74)
                                    : ftxui::Color::RGB(75, 82, 85);

    return ftxui::vbox({
               ftxui::text(title) | ftxui::bold,
               ftxui::separator(),
               ftxui::vbox(std::move(entries)) | ftxui::flex,
           }) |
           ftxui::size(ftxui::WIDTH, ftxui::EQUAL, 32) |
           ftxui::borderStyled(border_color);
}

std::size_t reader::page_for_offset(std::size_t offset) const {
    if (page_starts_.empty() || lines_.empty()) {
        return 0;
    }

    const auto iterator = std::upper_bound(
        lines_.begin(), lines_.end(), offset,
        [](std::size_t value, const line_range& line) {
            return value < line.begin;
        });
    const std::size_t line =
        iterator == lines_.begin()
            ? 0
            : static_cast<std::size_t>(
                  std::distance(lines_.begin(), iterator - 1));
    const std::size_t page_height =
        static_cast<std::size_t>(std::max(1, layout_height_));
    return std::min(line / page_height, page_starts_.size() - 1);
}

void reader::update_toc_pages() {
    for (toc_entry& entry : toc_entries_) {
        if (entry.has_target) {
            entry.page = page_for_offset(entry.source_offset);
        }
    }
}

void reader::update_active_toc() {
    active_toc_.reset();
    if (toc_entries_.empty() || page_starts_.empty()) {
        return;
    }

    const std::size_t page =
        std::min(current_page_, page_starts_.size() - 1);
    std::size_t best_page = 0;
    std::size_t best_offset = 0;
    bool found = false;

    for (std::size_t index = 0; index < toc_entries_.size(); ++index) {
        const toc_entry& entry = toc_entries_[index];
        if (!entry.has_target || entry.page > page) {
            continue;
        }

        if (!found || entry.page > best_page ||
            (entry.page == best_page && entry.source_offset >= best_offset)) {
            active_toc_ = index;
            best_page = entry.page;
            best_offset = entry.source_offset;
            found = true;
        }
    }
}

void reader::jump_to_selected_toc() {
    if (selected_toc_ >= toc_entries_.size() ||
        !toc_entries_[selected_toc_].has_target || page_starts_.empty()) {
        return;
    }

    current_page_ = std::min(toc_entries_[selected_toc_].page,
                             page_starts_.size() - 1);
    update_active_toc();
}

void reader::ensure_toc_visible(std::size_t visible_rows) {
    if (toc_entries_.empty()) {
        toc_scroll_offset_ = 0;
        return;
    }

    const std::size_t visible = std::max<std::size_t>(1, visible_rows);
    const std::size_t anchor =
        focus_ == focus::navigation
            ? std::min(selected_toc_, toc_entries_.size() - 1)
            : active_toc_.has_value()
                  ? *active_toc_
                  : std::min(selected_toc_, toc_entries_.size() - 1);

    if (toc_entries_.size() <= visible) {
        toc_scroll_offset_ = 0;
        return;
    }

    if (anchor < toc_scroll_offset_) {
        toc_scroll_offset_ = anchor;
    } else if (anchor >= toc_scroll_offset_ + visible) {
        toc_scroll_offset_ = anchor - visible + 1;
    }

    const std::size_t max_offset = toc_entries_.size() - visible;
    toc_scroll_offset_ = std::min(toc_scroll_offset_, max_offset);
}

ftxui::Element reader::render() {
    const reading_dimensions dimensions = get_reading_dimensions();
    if (dimensions.width != layout_width_ ||
        dimensions.height != layout_height_) {
        rebuild_layout(dimensions.width, dimensions.height);
    }
    ensure_toc_visible(static_cast<std::size_t>(dimensions.height));

    const bool navigation_focused = focus_ == focus::navigation;
    const ftxui::Color border_color =
        navigation_focused ? ftxui::Color::RGB(75, 82, 85)
                           : ftxui::Color::RGB(57, 68, 74);

    auto reading_panel =
        ftxui::vbox({
            ftxui::text(navigation_focused ? "阅读" : "阅读 [当前]") |
                ftxui::bold,
            ftxui::separator(),
            render_current_page() | ftxui::flex,
        }) |
        ftxui::borderStyled(border_color) | ftxui::flex;

    const std::string page_indicator =
        "第 " + std::to_string(current_page_ + 1) + " / " +
        std::to_string(page_starts_.size()) + " 页";

    return ftxui::vbox({
               ftxui::text("Reader · " + display_or_unknown(title_)) |
                   ftxui::bold | ftxui::center,
               ftxui::separator(),
               ftxui::hbox({
                   render_toc_panel(
                       static_cast<std::size_t>(dimensions.height)),
                   std::move(reading_panel),
               }) |
                   ftxui::flex,
               ftxui::separator(),
               ftxui::text(
                   "←/→ 翻页 · " + page_indicator +
                   " · 目录 ↑/↓选择 ←跳转 · Space 返回目录 · Esc 返回书库 · q 退出") |
                   ftxui::dim | ftxui::center,
           }) |
           ftxui::color(ftxui::Color::RGB(218, 224, 226)) |
           ftxui::bgcolor(ftxui::Color::RGB(25, 30, 32));
}

bool reader::handle_event(ftxui::Event event) {
    if (event == ftxui::Event::Escape) {
        back_requested_ = true;
        return true;
    }

    if (event == ftxui::Event::Character(' ')) {
        if (focus_ == focus::content) {
            focus_ = focus::navigation;
            if (active_toc_.has_value()) {
                selected_toc_ = *active_toc_;
            }
        }
        return true;
    }

    if (focus_ == focus::navigation && event == ftxui::Event::ArrowRight) {
        if (selected_toc_ < toc_entries_.size() &&
            toc_entries_[selected_toc_].has_target &&
            !page_starts_.empty()) {
            current_page_ = std::min(toc_entries_[selected_toc_].page,
                                     page_starts_.size() - 1);
            update_active_toc();
        }
        focus_ = focus::content;
        return true;
    }

    if (event == ftxui::Event::ArrowUp || event == ftxui::Event::k) {
        if (focus_ == focus::navigation && selected_toc_ > 0) {
            --selected_toc_;
            jump_to_selected_toc();
        }
        return true;
    }

    if (event == ftxui::Event::ArrowDown || event == ftxui::Event::j) {
    if (focus_ == focus::navigation &&
        selected_toc_ + 1 < toc_entries_.size()) {
            ++selected_toc_;
            jump_to_selected_toc();
        }
        return true;
    }

    if (focus_ == focus::content && event == ftxui::Event::ArrowLeft) {
        if (current_page_ > 0) {
            --current_page_;
            update_active_toc();
        }
        return true;
    }

    if (focus_ == focus::content && event == ftxui::Event::ArrowRight) {
        if (current_page_ + 1 < page_starts_.size()) {
            ++current_page_;
            update_active_toc();
        }
        return true;
    }

    return false;
}

std::size_t reader::book_index() const noexcept {
    return book_index_;
}

bool reader::back_requested() const noexcept {
    return back_requested_;
}

}  // namespace ui
