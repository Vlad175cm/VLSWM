cmake_minimum_required(VERSION 3.20)
project(vlswm VERSION 0.3.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

find_package(PkgConfig REQUIRED)
find_package(Threads REQUIRED)

pkg_check_modules(WAYLAND REQUIRED wayland-server)
pkg_check_modules(WLROOTS REQUIRED wlroots-0.20)
pkg_check_modules(XKBCOMMON REQUIRED xkbcommon)
pkg_check_modules(PIXMAN REQUIRED pixman-1)
pkg_check_modules(CAIRO REQUIRED cairo)
pkg_check_modules(PANGO REQUIRED pango)
pkg_check_modules(PANGOCAIRO REQUIRED pangocairo)
pkg_check_modules(LIBINPUT REQUIRED libinput)

add_executable(vlswm
    src/main.cpp
    src/server.cpp
    src/output.cpp
    src/view.cpp
    src/launcher.cpp
    src/theme.cpp
    src/ini.cpp
    src/animation.cpp
)

target_include_directories(vlswm PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${WAYLAND_INCLUDE_DIRS}
    ${WLROOTS_INCLUDE_DIRS}
    ${XKBCOMMON_INCLUDE_DIRS}
    ${PIXMAN_INCLUDE_DIRS}
    ${CAIRO_INCLUDE_DIRS}
    ${PANGO_INCLUDE_DIRS}
    ${PANGOCAIRO_INCLUDE_DIRS}
    ${LIBINPUT_INCLUDE_DIRS}
)

target_compile_definitions(vlswm PRIVATE WLR_USE_UNSTABLE)

target_link_libraries(vlswm PRIVATE
    ${WAYLAND_LIBRARIES}
    ${WLROOTS_LIBRARIES}
    ${XKBCOMMON_LIBRARIES}
    ${PIXMAN_LIBRARIES}
    ${CAIRO_LIBRARIES}
    ${PANGO_LIBRARIES}
    ${PANGOCAIRO_LIBRARIES}
    ${LIBINPUT_LIBRARIES}
    Threads::Threads
    m
)

target_compile_options(vlswm PRIVATE -Wall -Wextra -Wno-unused-parameter)
target_link_options(vlswm PRIVATE -rdynamic)

foreach(dir ${WAYLAND_LIBRARY_DIRS} ${WLROOTS_LIBRARY_DIRS})
    target_link_directories(vlswm PRIVATE ${dir})
endforeach()
