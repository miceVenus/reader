#ifndef READER_UI_READER_HPP
#define READER_UI_READER_HPP

#include "epub.hpp"

#include <ftxui/component/event.hpp>
#include <ftxui/dom/elements.hpp>

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace ui {

// The reader owns the data and interaction state required by the reader
// screen.  It is intentionally one component instead of a page plus a
// separate session object.
class reader {
public:
    reader(epub& epub_reader, eb_t book, std::size_t book_index);

    ftxui::Element render();
    bool handle_event(ftxui::Event event);

    std::size_t book_index() const noexcept;
    bool back_requested() const noexcept;

private:
    struct line_range {
        std::size_t begin = 0;
        std::size_t end = 0;
    };

    struct toc_entry {
        std::string label;
        std::size_t source_offset = 0;
        std::size_t page = 0;
        bool has_target = false;
    };

    enum class focus {
        navigation,
        content,
    };

    void rebuild_layout(int content_width, int content_height);
    ftxui::Element render_current_page() const;
    ftxui::Element render_toc_panel(std::size_t visible_rows) const;
    std::size_t page_for_offset(std::size_t offset) const;
    void update_toc_pages();
    void update_active_toc();
    void jump_to_selected_toc();
    void ensure_toc_visible(std::size_t visible_rows);

    std::size_t book_index_ = 0;
    std::string title_;
    std::vector<toc_entry> toc_entries_;
    std::string flattened_content_;
    std::vector<line_range> lines_;
    std::vector<std::size_t> page_starts_;
    int layout_width_ = 0;
    int layout_height_ = 0;
    std::size_t current_page_ = 0;
    std::size_t selected_toc_ = 0;
    std::optional<std::size_t> active_toc_;
    std::size_t toc_scroll_offset_ = 0;
    focus focus_ = focus::navigation;
    bool back_requested_ = false;
};

}  // namespace ui

#endif  // READER_UI_READER_HPP
