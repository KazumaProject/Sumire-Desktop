cmake_minimum_required(VERSION 3.20)

project(jawiki_louds_cpp
  VERSION 0.1.0
  LANGUAGES CXX
)

# -----------------------------
# Global settings
# -----------------------------
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# -----------------------------
# Threads (pthread)
# -----------------------------
find_package(Threads REQUIRED)

# ---- mozc fetch tool ----
option(BUILD_MOZC_FETCH "Build mozc_dic_fetch (requires a fully linkable libcurl)" OFF)
if(BUILD_MOZC_FETCH)
  find_package(CURL REQUIRED)
  add_executable(mozc_dic_fetch
    src/dictionary_builder/mozc_fetch/mozc_fetch.cpp
  )
  target_link_libraries(mozc_dic_fetch PRIVATE CURL::libcurl)
endif()

# -----------------------------
# Dictionary builder libs
# -----------------------------
add_library(louds_utf16 STATIC
  src/dictionary_builder/louds_builder/prefix_tree/prefix_tree_utf16.cpp
  src/dictionary_builder/louds_builder/prefix_tree_with_term_id/prefix_tree_with_term_id_utf16.cpp
  src/dictionary_builder/louds_builder/louds/louds_utf16_writer.cpp
  src/dictionary_builder/louds_builder/louds/louds_converter_utf16.cpp
  src/dictionary_builder/louds_builder/louds/louds_utf16_reader.cpp
  src/dictionary_builder/louds_builder/louds_with_term_id/louds_with_term_id_utf16.cpp
  src/dictionary_builder/louds_builder/louds_with_term_id/louds_converter_with_term_id_utf16.cpp
  src/dictionary_builder/louds_builder/louds_with_term_id/louds_with_term_id_reader_utf16.cpp
)

target_include_directories(louds_utf16 PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/src/dictionary_builder/louds_builder
)

# ConnectionIdBuilder
add_library(connection_id STATIC
  src/dictionary_builder/connection_id/connection_id_builder.cpp
)
target_include_directories(connection_id PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/src/dictionary_builder
)

# TokenArray
add_library(token_array STATIC
  src/dictionary_builder/token_array/token_array.cpp
)
target_include_directories(token_array PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/src/dictionary_builder
  ${CMAKE_CURRENT_SOURCE_DIR}/src/dictionary_builder/louds_builder
)
target_link_libraries(token_array PUBLIC louds_utf16)

# -----------------------------
# GraphBuilder (NEW)
# -----------------------------
add_library(graph_builder STATIC
  src/graph_builder/graph.cpp
)
target_include_directories(graph_builder PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${CMAKE_CURRENT_SOURCE_DIR}/src/dictionary_builder
  ${CMAKE_CURRENT_SOURCE_DIR}/src/dictionary_builder/louds_builder
)
target_link_libraries(graph_builder PUBLIC
  token_array
  louds_utf16
)

# -----------------------------
# Path algorithm (NEW)
# -----------------------------
add_library(path_algorithm STATIC
  src/path_algorithm/find_path.cpp
)
target_include_directories(path_algorithm PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_link_libraries(path_algorithm PUBLIC
  graph_builder
)

# -----------------------------
# Dictionary builder executable
# -----------------------------
add_executable(dictionary_builder
  src/dictionary_builder/dictionary_builder.cpp
)
target_link_libraries(dictionary_builder PRIVATE
  louds_utf16
  connection_id
)

# -----------------------------
# Tries + TokenArray builder
# -----------------------------
add_executable(tries_token_builder
  src/dictionary_builder/tries_token_builder.cpp
)
target_link_libraries(tries_token_builder PRIVATE
  louds_utf16
  token_array
)

# -----------------------------
# CLI
# -----------------------------
add_executable(cps_cli cli/louds/cps_cli.cpp)
target_link_libraries(cps_cli PRIVATE louds_utf16)

add_executable(termid_cli cli/louds/termid_cli.cpp)
target_link_libraries(termid_cli PRIVATE louds_utf16)

add_executable(prefix_predict_cli cli/kana_kanji/prefix_predict_cli.cpp)
target_link_libraries(prefix_predict_cli PRIVATE token_array)

add_executable(astar_bunsetsu_parallel_cli cli/kana_kanji/astar_bunsetsu_parallel_cli.cpp)
target_link_libraries(astar_bunsetsu_parallel_cli PRIVATE
  louds_utf16
  token_array
  connection_id
  graph_builder
  path_algorithm
  Threads::Threads
)

# N-best kana-kanji conversion CLI (NEW)
add_executable(astar_bunsetsu_cli cli/kana_kanji/astar_bunsetsu_cli.cpp)
target_link_libraries(astar_bunsetsu_cli PRIVATE
  louds_utf16
  token_array
  connection_id
  graph_builder
  path_algorithm
)

# -----------------------------
# Zenz / llama.cpp (NEW)
# -----------------------------
option(ENABLE_ZENZ "Build zenz_convert_cli (llama.cpp)" ON)

if(ENABLE_ZENZ)
  # Reduce build time/noise
  set(LLAMA_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  set(LLAMA_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(LLAMA_BUILD_SERVER OFF CACHE BOOL "" FORCE)

  add_subdirectory(third_party/llama.cpp EXCLUDE_FROM_ALL)

  # ---- Fix: build llama.cpp targets with C++17 to avoid char8_t issues on g++-10 ----
  # (C++20 makes u8"" -> char8_t*, but this llama.cpp revision assumes const char*)
  foreach(tgt
      llama
      llama_static
      llama-lib
      ggml
      ggml-base
      ggml-cpu
      ggml-cuda
      ggml-metal
      ggml-vulkan
      common
      llava
  )
    if(TARGET ${tgt})
      set_target_properties(${tgt} PROPERTIES
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED YES
        CXX_EXTENSIONS OFF
      )
    endif()
  endforeach()

  # pick llama library target name
  set(_LLAMA_LIB_TARGET "")
  if(TARGET llama)
    set(_LLAMA_LIB_TARGET llama)
  elseif(TARGET llama_static)
    set(_LLAMA_LIB_TARGET llama_static)
  elseif(TARGET llama-lib)
    set(_LLAMA_LIB_TARGET llama-lib)
  endif()

  if(_LLAMA_LIB_TARGET STREQUAL "")
    message(FATAL_ERROR "ENABLE_ZENZ is ON but no llama target was found after add_subdirectory(third_party/llama.cpp).")
  endif()

  add_executable(zenz_convert_cli
    cli/zenz/zenz_convert_cli.cpp
  )

  target_compile_features(zenz_convert_cli PRIVATE cxx_std_20)

  target_include_directories(zenz_convert_cli PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/llama.cpp/include
  )

  target_link_libraries(zenz_convert_cli PRIVATE
    ${_LLAMA_LIB_TARGET}
    Threads::Threads
  )

  # ==========================================================
  # NEW: Fused CLI (A* + Zenz in parallel, prepend Zenz result)
  # ==========================================================
  add_executable(astar_zenz_fuse_cli
    cli/kana_kanji/astar_zenz_fuse_cli.cpp
  )

  target_compile_features(astar_zenz_fuse_cli PRIVATE cxx_std_20)

  # llama.h include
  target_include_directories(astar_zenz_fuse_cli PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/llama.cpp/include
  )

  # Link: same as astar_bunsetsu_cli + llama + pthread
  target_link_libraries(astar_zenz_fuse_cli PRIVATE
    louds_utf16
    token_array
    connection_id
    graph_builder
    path_algorithm
    ${_LLAMA_LIB_TARGET}
    Threads::Threads
  )
endif()
