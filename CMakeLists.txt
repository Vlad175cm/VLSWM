cmake_minimum_required(VERSION 3.20)
project(vlswm VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

find_package(PkgConfig REQUIRED)
find_package(Threads REQUIRED)
find_program(WAYLAND_SCANNER wayland-scanner REQUIRED)

pkg_check_modules(WAYLAND REQUIRED wayland-server)
pkg_check_modules(WLROOTS REQUIRED wlroots-0.20)
pkg_check_modules(XKBCOMMON REQUIRED xkbcommon)
pkg_check_modules(PIXMAN REQUIRED pixman-1)
pkg_check_modules(CAIRO REQUIRED cairo)
pkg_check_modules(PANGO REQUIRED pango)
pkg_check_modules(PANGOCAIRO REQUIRED pangocairo)
pkg_check_modules(LIBINPUT REQUIRED libinput)
pkg_check_modules(WLR_PROTOCOLS wlr-protocols)

set(_wlr_proto_dir "${WLR_PROTOCOLS_DATADIR}")
if(NOT _wlr_proto_dir AND WLR_PROTOCOLS_FOUND)
    pkg_get_variable(_wlr_proto_dir wlr-protocols pkgdatadir)
endif()
if(NOT _wlr_proto_dir)
    foreach(_dir /usr/share/wlr-protocols /usr/local/share/wlr-protocols)
        if(EXISTS ${_dir}/unstable/wlr-layer-shell-unstable-v1.xml)
            set(_wlr_proto_dir ${_dir})
            break()
        endif()
    endforeach()
endif()
if(_wlr_proto_dir)
    set(WLR_PROTOCOLS_DATADIR "${_wlr_proto_dir}" CACHE PATH
        "Directory with wlr-protocols XML files" FORCE)
endif()

set(VLSWM_COMPAT_DIR ${CMAKE_BINARY_DIR}/compat)
foreach(hdr wlr/types/wlr_scene.h wlr/render/color.h)
    set(src "")
    foreach(inc ${WLROOTS_INCLUDE_DIRS})
        if(EXISTS ${inc}/${hdr})
            set(src ${inc}/${hdr})
            break()
        endif()
    endforeach()
    if(src)
        file(READ ${src} content)
        string(REGEX REPLACE "\\[static ([0-9]+)\\]" "[\\1]" patched "${content}")
        if(NOT patched STREQUAL content)
            get_filename_component(dir ${hdr} DIRECTORY)
            file(MAKE_DIRECTORY ${VLSWM_COMPAT_DIR}/${dir})
            file(WRITE ${VLSWM_COMPAT_DIR}/${hdr} "${patched}")
        endif()
    endif()
endforeach()

set(VLSWM_PROTOCOL_DIR ${CMAKE_BINARY_DIR}/protocols)
file(MAKE_DIRECTORY ${VLSWM_PROTOCOL_DIR})

set(VLSWM_PROTOCOL_HEADERS)
foreach(proto
        wlr-layer-shell-unstable-v1)
    set(_system_xml ${WLR_PROTOCOLS_DATADIR}/unstable/${proto}.xml)
    set(_bundled_xml ${CMAKE_SOURCE_DIR}/protocols/${proto}.xml)
    set(xml "")
    if(EXISTS ${_system_xml})
        file(SIZE ${_system_xml} _system_xml_size)
        if(_system_xml_size GREATER 0)
            set(xml ${_system_xml})
        endif()
    endif()
    if(NOT xml)
        if(EXISTS ${_bundled_xml})
            file(SIZE ${_bundled_xml} _bundled_xml_size)
            if(_bundled_xml_size GREATER 0)
                set(xml ${_bundled_xml})
            endif()
        endif()
    endif()
    set(header ${VLSWM_PROTOCOL_DIR}/${proto}-protocol.h)
    set(_proto_paths)
    foreach(_inc ${WLROOTS_INCLUDE_DIRS})
        list(APPEND _proto_paths ${_inc} ${_inc}/wlr/types)
    endforeach()
    find_path(${proto}_HEADER NAMES ${proto}-protocol.h PATHS ${_proto_paths})
    if(xml)
        add_custom_command(
            OUTPUT ${header}
            COMMAND ${WAYLAND_SCANNER} server-header ${xml} ${header}
            DEPENDS ${xml}
            COMMENT "Generating ${proto}-protocol.h")
        list(APPEND VLSWM_PROTOCOL_HEADERS ${header})
    elseif(NOT ${proto}_HEADER)
        message(FATAL_ERROR
            "Missing ${proto}-protocol.h. Install the wlr-protocols package "
            "or a wlroots build that ships the generated headers.")
    endif()
endforeach()

add_custom_target(vlswm-protocols DEPENDS ${VLSWM_PROTOCOL_HEADERS})

add_executable(vlswmstat src/vlswmstat.cpp)
target_compile_definitions(vlswmstat PRIVATE VLSWM_VERSION="${PROJECT_VERSION}")
target_compile_options(vlswmstat PRIVATE -Wall -Wextra)
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

add_dependencies(vlswm vlswm-protocols)

target_include_directories(vlswm PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${VLSWM_COMPAT_DIR}
    ${VLSWM_PROTOCOL_DIR}
    ${WAYLAND_INCLUDE_DIRS}
    ${WLROOTS_INCLUDE_DIRS}
    ${XKBCOMMON_INCLUDE_DIRS}
    ${PIXMAN_INCLUDE_DIRS}
    ${CAIRO_INCLUDE_DIRS}
    ${PANGO_INCLUDE_DIRS}
    ${PANGOCAIRO_INCLUDE_DIRS}
    ${LIBINPUT_INCLUDE_DIRS}
)

target_compile_definitions(vlswm PRIVATE WLR_USE_UNSTABLE VLSWM_VERSION="${PROJECT_VERSION}")

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

include(CheckIPOSupported)
check_ipo_supported(RESULT _ipo_supported OUTPUT _ipo_error)
if(_ipo_supported)
    set_target_properties(vlswm vlswmstat PROPERTIES INTERPROCEDURAL_OPTIMIZATION TRUE)
else()
    message(STATUS "LTO is not supported: ${_ipo_error}")
endif()
