cmake_minimum_required(VERSION 3.20)
project(yesymbol VERSION 1.0.0 LANGUAGES C RC)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_executable(yesymbol WIN32
    src/main.c
    src/ui.c
    src/about.c
    src/clipboard.c
    src/storage.c
    src/symbol_data.c
    src/resource.rc
)

target_include_directories(yesymbol PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(yesymbol PRIVATE
    user32
    gdi32
    advapi32
    comctl32
    shlwapi
    shell32
)

if(MSVC)
    target_compile_options(yesymbol PRIVATE /O2 /utf-8 /W4 /wd4100 /wd4244 /wd4267)
    set_property(TARGET yesymbol PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    # resource.rc explicitly embeds manifest resource ID 1.
    target_link_options(yesymbol PRIVATE "/MANIFEST:NO")
else()
    target_compile_options(yesymbol PRIVATE -O2 -Wall -Wextra)
endif()

set_target_properties(yesymbol PROPERTIES
    OUTPUT_NAME "yesymbol"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/dist"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_CURRENT_SOURCE_DIR}/dist"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_CURRENT_SOURCE_DIR}/dist-debug"
)
