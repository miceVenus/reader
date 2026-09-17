#ifndef READER_UI_LIBRARY_HPP
#define READER_UI_LIBRARY_HPP

#include "epub.hpp"

#include <ftxui/component/event.hpp>
#include <ftxui/dom/elements.hpp>

#include <cstddef>
#include <string>
#include <vector>

namespace ui {

// The library page and its interaction state belong to one component.  The
// application only asks it to render and forwards events to it.
class library {
public:
    library(epub& epub_reader,
            const std::vector<eb_t>& books,
            std::string repository);

    ftxui::Element render(const std::string& status_message) const;
    bool handle_event(ftxui::Event event);

    std::size_t selected_book_index() const noexcept;

private:
    epub& epub_reader_;
    const std::vector<eb_t>& books_;
    std::string repository_;
    std::size_t selected_book_ = 0;
};

}  // namespace ui

#endif  // READER_UI_LIBRARY_HPP
