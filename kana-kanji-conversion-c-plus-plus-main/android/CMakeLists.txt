cmake_minimum_required(VERSION 3.20)
project(kana_kanji_android LANGUAGES CXX)

# -----------------------------
# Global settings
# -----------------------------
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# SHARED に静的ライブラリをリンクするため、PIC を全体で有効化
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# -----------------------------
# Paths
# -----------------------------
set(REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/..")
set(SRC_ROOT  "${REPO_ROOT}/src")

# -----------------------------
# Dictionary builder libs (same as original)
#   ※ Android では mozc_fetch や CLI は作らない
# -----------------------------

# louds_utf16
add_library(louds_utf16 STATIC
  ${SRC_ROOT}/dictionary_builder/louds_builder/prefix_tree/prefix_tree_utf16.cpp
  ${SRC_ROOT}/dictionary_builder/louds_builder/prefix_tree_with_term_id/prefix_tree_with_term_id_utf16.cpp
  ${SRC_ROOT}/dictionary_builder/louds_builder/louds/louds_utf16_writer.cpp
  ${SRC_ROOT}/dictionary_builder/louds_builder/louds/louds_converter_utf16.cpp
  ${SRC_ROOT}/dictionary_builder/louds_builder/louds/louds_utf16_reader.cpp
  ${SRC_ROOT}/dictionary_builder/louds_builder/louds_with_term_id/louds_with_term_id_utf16.cpp
  ${SRC_ROOT}/dictionary_builder/louds_builder/louds_with_term_id/louds_converter_with_term_id_utf16.cpp
  ${SRC_ROOT}/dictionary_builder/louds_builder/louds_with_term_id/louds_with_term_id_reader_utf16.cpp
)
target_include_directories(louds_utf16 PUBLIC
  ${SRC_ROOT}/dictionary_builder/louds_builder
)

# connection_id
add_library(connection_id STATIC
  ${SRC_ROOT}/dictionary_builder/connection_id/connection_id_builder.cpp
)
target_include_directories(connection_id PUBLIC
  ${SRC_ROOT}/dictionary_builder
)

# token_array
add_library(token_array STATIC
  ${SRC_ROOT}/dictionary_builder/token_array/token_array.cpp
)
target_include_directories(token_array PUBLIC
  ${SRC_ROOT}/dictionary_builder
  ${SRC_ROOT}/dictionary_builder/louds_builder
)
target_link_libraries(token_array PUBLIC louds_utf16)

# -----------------------------
# GraphBuilder
# -----------------------------
add_library(graph_builder STATIC
  ${SRC_ROOT}/graph_builder/graph.cpp
)
target_include_directories(graph_builder PUBLIC
  ${SRC_ROOT}
  ${SRC_ROOT}/dictionary_builder
  ${SRC_ROOT}/dictionary_builder/louds_builder
)
target_link_libraries(graph_builder PUBLIC
  token_array
  louds_utf16
)

# -----------------------------
# Path algorithm
# -----------------------------
add_library(path_algorithm STATIC
  ${SRC_ROOT}/path_algorithm/find_path.cpp
)
target_include_directories(path_algorithm PUBLIC
  ${SRC_ROOT}
)
target_link_libraries(path_algorithm PUBLIC
  graph_builder
)

# -----------------------------
# The Android .so (shared library)
# -----------------------------
# ここに JNI ブリッジを必ず含める（nativeInit/nativeConvert の実装）
add_library(kana_kanji SHARED
  ${CMAKE_CURRENT_LIST_DIR}/kana_kanji_entry.cpp
)

target_include_directories(kana_kanji PRIVATE
  ${SRC_ROOT}
  ${SRC_ROOT}/dictionary_builder
  ${SRC_ROOT}/dictionary_builder/louds_builder
)

target_link_libraries(kana_kanji PRIVATE
  path_algorithm
  graph_builder
  token_array
  connection_id
  louds_utf16
)

find_library(log-lib log)
target_link_libraries(kana_kanji PRIVATE ${log-lib})
