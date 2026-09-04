cmake_minimum_required(VERSION 3.20)
project(AlignPanel LANGUAGES CXX)

# AviUtl2 は 64bit。x64 でビルドすること。
add_library(AlignPanel SHARED src/AlignPanel.cpp)

target_include_directories(AlignPanel PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include/aviutl2_sdk)

target_compile_features(AlignPanel PRIVATE cxx_std_17)

# Win32 API をワイド文字（W系）で解決する
target_compile_definitions(AlignPanel PRIVATE UNICODE _UNICODE)

if (MSVC)
    # ソースは UTF-8。ワイド文字列リテラルを正しく UTF-16 化するため /utf-8 必須。
    target_compile_options(AlignPanel PRIVATE /utf-8 /W3)
endif()

target_link_libraries(AlignPanel PRIVATE user32 gdi32 comctl32)

# 出力を  TP_CoordAlign.aux2  にする（接頭辞 lib を消し、拡張子を .aux2 に）
set_target_properties(AlignPanel PROPERTIES
    PREFIX ""
    OUTPUT_NAME "TP_CoordAlign"
    SUFFIX ".aux2")
