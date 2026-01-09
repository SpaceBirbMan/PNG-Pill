cmake_minimum_required(VERSION 3.16)
project(PNGPILL LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(SDL3 REQUIRED CONFIG)
find_package(SDL3_image REQUIRED CONFIG)
find_package(SDL3_ttf REQUIRED CONFIG)
find_package(SDL3_ttf REQUIRED CONFIG)

find_package(WebP REQUIRED)
find_package(Libwebsockets REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBWEBSOCKETS REQUIRED IMPORTED_TARGET libwebsockets)

file(GLOB SOURCES CONFIGURE_DEPENDS
    "*.cpp"
    "*.h"
)

add_executable(PNGPILL ${SOURCES})

target_link_libraries(PNGPILL PRIVATE
    SDL3::SDL3
    SDL3_image::SDL3_image
    SDL3_ttf::SDL3_ttf
    WebP::webp
    PkgConfig::LIBWEBSOCKETS
)

