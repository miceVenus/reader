#ifndef READER_UI_APPLICATION_HPP
#define READER_UI_APPLICATION_HPP

#include "epub.hpp"
#include "ui/library.hpp"
#include "ui/reader.hpp"

#include <ftxui/component/event.hpp>
#include <ftxui/dom/elements.hpp>

#include <optional>
#include <string>
#include <vector>

namespace ui {

class application {
public:
    explicit application(std::string repository);

    int run();

private:
    enum class page {
        library,
        reader,
    };

    ftxui::Element render();
    bool handle_event(ftxui::Event event);
    void open_selected_book();

    std::string repository_;
    epub epub_reader_;
    std::vector<eb_t> books_;
    library library_;
    std::optional<reader> reader_;
    page current_page_ = page::library;
    std::string status_message_;
};

}  // namespace ui

#endif  // READER_UI_APPLICATION_HPP
