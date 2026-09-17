cmake_minimum_required(VERSION 3.15)
project(reader)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
 
# Make sure vcpkg toolchain file is passed at configure time
find_package(ftxui CONFIG REQUIRED)

find_package(libzip REQUIRED)

find_package(pugixml REQUIRED)

add_executable(main
    src/main.cpp
    src/epub.cpp
        src/epub_book.cpp
        src/epub_parser.cpp
        src/zip_archive.cpp
        src/ui/application.cpp
        src/ui/library.cpp
        src/ui/reader.cpp
)

target_include_directories(main PRIVATE include)

target_link_libraries(main
    PRIVATE ftxui::screen
    PRIVATE ftxui::dom
    PRIVATE ftxui::component
    PRIVATE libzip::zip
    PRIVATE pugixml
)
