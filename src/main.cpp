#include "ui/application.hpp"

#include <exception>
#include <iostream>

int main(int argc, char* argv[]) {
    const std::string repository = argc > 1 ? argv[1] : "/home/bbm/workspace/C++/reader/epub";

    try {
        ui::application app(repository);
        return app.run();
    } catch (const std::exception& error) {
        std::cerr << "启动 reader 失败: " << error.what() << '\n';
        return 1;
    }
}
