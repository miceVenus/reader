#include "ui/application.hpp"

#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/component/screen_interactive.hpp>

#include <exception>
#include <utility>

namespace ui {

application::application(std::string repository)
    : repository_(std::move(repository)),
      books_(epub_reader_.open_dir(repository_.c_str())),
      library_(epub_reader_, books_, repository_) {}

int application::run() {
    ftxui::ScreenInteractive screen =
        ftxui::ScreenInteractive::Fullscreen();

    auto renderer = ftxui::Renderer([this] {
        return render();
    });

    auto root = ftxui::CatchEvent(
        renderer, [this, &screen](ftxui::Event event) {
            if (event == ftxui::Event::q || event == ftxui::Event::Q) {
                screen.Exit();
                return true;
            }

            // Esc exits from the library page.  From the reader page it is
            // handled by reader::handle_event() and returns to the library.
            if (current_page_ == page::library &&
                event == ftxui::Event::Escape) {
                screen.Exit();
                return true;
            }

            return handle_event(event);
        });

    screen.Loop(root);
    return 0;
}

ftxui::Element application::render() {
    if (current_page_ == page::library) {
        return library_.render(status_message_);
    }

    if (!reader_) {
        return ftxui::text("阅读会话不可用") | ftxui::center;
    }

    return reader_->render();
}

bool application::handle_event(ftxui::Event event) {
    if (current_page_ == page::library) {
        if (event == ftxui::Event::Return && !books_.empty()) {
            try {
                open_selected_book();
                status_message_.clear();
            } catch (const std::exception& error) {
                status_message_ =
                    std::string("读取 EPUB 内容失败: ") + error.what();
            }
            return true;
        }

        return library_.handle_event(event);
    }

    if (!reader_) {
        return false;
    }

    const bool handled = reader_->handle_event(event);
    if (reader_->back_requested()) {
        reader_.reset();
        current_page_ = page::library;
    }
    return handled;
}

void application::open_selected_book() {
    const std::size_t selected_book = library_.selected_book_index();
    if (selected_book >= books_.size()) {
        return;
    }

    reader next_reader(epub_reader_, books_[selected_book], selected_book);
    reader_ = std::move(next_reader);
    current_page_ = page::reader;
}

}  // namespace ui
