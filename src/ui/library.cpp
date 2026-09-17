#include "ui/library.hpp"

#include <ftxui/dom/table.hpp>
#include <ftxui/screen/string.hpp>

#include <string>
#include <utility>
#include <vector>

namespace ui {
namespace {

std::string join(const std::vector<std::string>& values,
                 const std::string& separator) {
    std::string result;
    for (const std::string& value : values) {
        if (value.empty()) {
            continue;
        }

        if (!result.empty()) {
            result += separator;
        }
        result += value;
    }
    return result;
}

std::string book_authors(const epub_metadata& metadata) {
    std::vector<std::string> authors;

    for (const epub_creator& creator : metadata.creators) {
        if (creator.role == "aut") {
            authors.push_back(creator.value);
        }
    }

    if (authors.empty()) {
        for (const epub_creator& creator : metadata.creators) {
            if (creator.role.empty()) {
                authors.push_back(creator.value);
            }
        }
    }

    if (authors.empty()) {
        for (const epub_creator& creator : metadata.creators) {
            authors.push_back(creator.value);
        }
    }

    const std::string result = join(authors, " & ");
    return result.empty() ? "未知" : result;
}

std::string display_or_unknown(const std::string& value) {
    return value.empty() ? "未知" : value;
}

std::string truncate_for_table(const std::string& value, int max_width) {
    if (ftxui::string_width(value) <= max_width) {
        return value;
    }

    const std::string suffix = "...";
    const int available_width = max_width - ftxui::string_width(suffix);
    if (available_width <= 0) {
        return suffix;
    }

    std::string result;
    int current_width = 0;
    for (const std::string& glyph : ftxui::Utf8ToGlyphs(value)) {
        const int glyph_width = ftxui::string_width(glyph);
        if (current_width + glyph_width > available_width) {
            break;
        }

        result += glyph;
        current_width += glyph_width;
    }

    return result + suffix;
}

std::vector<std::vector<std::string>> make_book_rows(
    epub& epub_reader, const std::vector<eb_t>& books, int width) {
    std::vector<std::vector<std::string>> rows;
    rows.push_back({"序号", "书名", "作者", "语言", "章节"});
    rows.reserve(books.size() + 1);

    for (std::size_t index = 0; index < books.size(); ++index) {
        const epub_metadata& metadata = epub_reader.metadata(books[index]);
        const epub_spine& spine = epub_reader.spine(books[index]);

        auto index_str = std::to_string(index + 1);
        auto chap_size_str = std::to_string(spine.items.size());
        auto lang_str = display_or_unknown(metadata.language);
        width -= (index_str.size() + lang_str.size() + chap_size_str.size());
        auto unt_title_str = display_or_unknown(metadata.title);
        auto unt_author_str = book_authors(metadata);

        std::string title_str;
        std::string author_str;

        if (unt_author_str.size() < unt_title_str.size()) {
            author_str = truncate_for_table(unt_author_str, width / 2);
            width -= author_str.size();
            title_str = truncate_for_table(unt_title_str, width);
        } else {
            title_str = truncate_for_table(unt_title_str, width / 2);
            width -= author_str.size();
            author_str = truncate_for_table(unt_author_str, width);
        }

        rows.push_back({
            index_str,
            title_str,
            author_str,
            lang_str,
            chap_size_str,
        });
    }

    return rows;
}

ftxui::Element render_book_table(
    const std::vector<std::vector<std::string>>& rows,
    std::size_t selected_book) {
    ftxui::Table table(rows);
    table.SelectAll().Border(ftxui::LIGHT);
    table.SelectAll().Separator(ftxui::LIGHT);

    const int selected_row = static_cast<int>(selected_book) + 1;
    if (selected_row > 0 && selected_row < static_cast<int>(rows.size())) {
        auto selected = table.SelectRow(selected_row);
        const ftxui::Color selected_background =
            ftxui::Color::RGB(57, 68, 74);
        selected.DecorateCells(ftxui::bgcolor(selected_background));
        selected.DecorateCells(
            ftxui::color(ftxui::Color::RGB(218, 224, 226)));
        selected.DecorateSeparatorVertical(
            ftxui::bgcolor(selected_background));
    }

    return table.Render();
}

}  // namespace

library::library(epub& epub_reader,
                 const std::vector<eb_t>& books,
                 std::string repository)
    : epub_reader_(epub_reader),
      books_(books),
      repository_(std::move(repository)) {}

ftxui::Element library::render(const std::string& status_message) const {
    const ftxui::Dimensions terminal_size = ftxui::Terminal::Size();
    const std::vector<std::vector<std::string>> rows =
        make_book_rows(epub_reader_, books_, terminal_size.dimx);

    ftxui::Element table = render_book_table(rows, selected_book_);
    if (books_.empty()) {
        table = ftxui::vbox({
            table,
            ftxui::text("仓库中没有 EPUB 文件") | ftxui::dim |
                ftxui::center,
        });
    }

    ftxui::Element status = ftxui::emptyElement();
    if (!status_message.empty()) {
        status = ftxui::text(status_message) |
                 ftxui::color(ftxui::Color::RGB(220, 170, 120)) |
                 ftxui::center;
    }

    return ftxui::vbox({
               ftxui::text("Reader · EPUB 书库") |
                   ftxui::bold | ftxui::center,
               ftxui::text("目录: " + repository_) |
                   ftxui::dim | ftxui::center,
               ftxui::separator(),
               table | ftxui::flex,
               ftxui::separator(),
               status,
               ftxui::text("↑/↓ 选择 · Enter 阅读 · q 或 Esc 退出") |
                   ftxui::dim | ftxui::center,
           }) |
           ftxui::color(ftxui::Color::RGB(218, 224, 226)) |
           ftxui::bgcolor(ftxui::Color::RGB(25, 30, 32));
}

bool library::handle_event(ftxui::Event event) {
    if (event == ftxui::Event::ArrowUp || event == ftxui::Event::k) {
        if (selected_book_ > 0) {
            --selected_book_;
        }
        return true;
    }

    if (event == ftxui::Event::ArrowDown || event == ftxui::Event::j) {
        if (!books_.empty() && selected_book_ + 1 < books_.size()) {
            ++selected_book_;
        }
        return true;
    }

    return false;
}

std::size_t library::selected_book_index() const noexcept {
    return selected_book_;
}

}  // namespace ui
