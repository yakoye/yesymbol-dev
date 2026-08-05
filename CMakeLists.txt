cmake_minimum_required(VERSION 3.20)
project(yesymbol VERSION 1.1.0 LANGUAGES C CXX RC)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(yesymbol WIN32
    src/main.c
    src/ui.c
    src/emoji_renderer.c
    src/about.c
    src/clipboard.c
    src/storage.c
    src/symbol_data.c
    src/emoji_image_data.c
    src/resource.rc
)

# Windows SDK 10.0.26100 dwrite.h contains C++-only declarations/macros.
# Compile only the isolated DirectWrite bridge as C++ while the application,
# UI, storage, clipboard and generated symbol database remain C11.
set_source_files_properties(src/emoji_renderer.c PROPERTIES LANGUAGE CXX)

target_include_directories(yesymbol PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(yesymbol PRIVATE
    user32
    gdi32
    msimg32
    d2d1
    dwrite
    windowscodecs
    ole32
    advapi32
    comctl32
    shlwapi
    shell32
)

if(MSVC)
    target_compile_options(yesymbol PRIVATE /O2 /utf-8 /W4 /wd4100 /wd4244 /wd4267
        $<$<COMPILE_LANGUAGE:CXX>:/GR->)
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

add_executable(common_v3_check
    tests/common_v3_check.c
    src/storage.c
)
target_include_directories(common_v3_check PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(common_v3_check PRIVATE advapi32)
if(MSVC)
    target_compile_options(common_v3_check PRIVATE /utf-8 /W4 /wd4100 /wd4244 /wd4267)
    set_property(TARGET common_v3_check PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

add_executable(rapid_input_receiver WIN32 tests/rapid_input_receiver.c)
target_link_libraries(rapid_input_receiver PRIVATE user32 gdi32)
if(MSVC)
    target_compile_options(rapid_input_receiver PRIVATE /utf-8 /W4)
    set_property(TARGET rapid_input_receiver PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
