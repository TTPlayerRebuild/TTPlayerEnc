include(FetchContent)
include("${CMAKE_CURRENT_LIST_DIR}/lame_symbols.cmake")

function(add_lame_backend id content version sha256 major minor type patch)
  FetchContent_Declare(${content}
    URL "https://downloads.sourceforge.net/project/lame/lame/${version}/lame-${version}.tar.gz?download=1"
    URL_HASH "SHA256=${sha256}"
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE SOURCE_SUBDIR libmp3lame)
  FetchContent_MakeAvailable(${content})
  set(root "${${content}_SOURCE_DIR}")
  file(READ "${root}/libmp3lame/version.h" version_header)
  foreach(field IN ITEMS MAJOR MINOR TYPE PATCH)
    string(TOLOWER "${field}" argument)
    if(NOT version_header MATCHES "LAME_${field}_VERSION[ \t]+${${argument}}([ \t\r\n/]|$)")
      message(FATAL_ERROR "Expected official LAME ${version} sources (${field} version mismatch)")
    endif()
  endforeach()

  set(sources VbrTag bitstream encoder fft gain_analysis id3tag lame newmdct presets
    psymodel quantize quantize_pvt reservoir set_get tables takehiro util vbrquantize version)
  list(TRANSFORM sources PREPEND "${root}/libmp3lame/")
  list(TRANSFORM sources APPEND ".c")
  if(version STREQUAL "4.1beta1")
    # Keep /arch:SSE2 for baseline code. Upstream dispatches the wider kernels
    # only after checking both CPUID and OS register-state support (also on XP).
    foreach(vector IN ITEMS xmm_quantize_sub xmm_choose_table xmm_quantize_lines
        xmm_calc_sfb_noise avx2_choose_table avx2_quantize_lines avx512_quantize_lines)
      list(APPEND sources "${root}/libmp3lame/vector/${vector}.c")
    endforeach()
  endif()

  # No changes to downloaded sources; isolate every public/internal C symbol.
  set(namespace_header "#pragma once\n/* Generated LAME ${version} symbol namespace. */\n")
  foreach(symbol IN LISTS TTP_LAME_SYMBOLS)
    string(APPEND namespace_header "#define ${symbol} ttp_lame_${id}_${symbol}\n")
  endforeach()
  set(generated "${CMAKE_CURRENT_BINARY_DIR}/lame-${id}")
  file(MAKE_DIRECTORY "${generated}")
  file(CONFIGURE OUTPUT "${generated}/lame_namespace.h" CONTENT "${namespace_header}" @ONLY)

  # Separate C/C++ targets: Visual Studio cannot represent target-level
  # COMPILE_LANGUAGE options independently in a mixed-language project.
  add_library(ttp_lame_${id} STATIC ${sources})
  target_include_directories(ttp_lame_${id} PRIVATE "${root}/include" "${root}"
    "${root}/libmp3lame")
  # Upstream's MSVC configuration; no CLI, decoder, DLL or assembler dependency.
  target_compile_options(ttp_lame_${id} PRIVATE
    "/FI${root}/configMS.h" "/FI${generated}/lame_namespace.h"
    /Os /Gy /Gw /GF /fp:precise /arch:SSE2 /utf-8 /wd4244 /wd4267 /wd4996)
  target_compile_definitions(ttp_lame_${id} PRIVATE _CRT_SECURE_NO_WARNINGS)

  add_library(ttp_lame_${id}_backend STATIC "${CMAKE_CURRENT_SOURCE_DIR}/src/lame_backend.cpp")
  target_include_directories(ttp_lame_${id}_backend PRIVATE "${root}/include" "${generated}"
    "${CMAKE_CURRENT_SOURCE_DIR}/src" "${CMAKE_CURRENT_SOURCE_DIR}/include")
  target_compile_options(ttp_lame_${id}_backend PRIVATE /W4 /permissive- /EHsc /GR-
    /Os /Gy /Gw /GF /fp:precise /arch:SSE2 /utf-8)
  target_compile_definitions(ttp_lame_${id}_backend PRIVATE _CRT_SECURE_NO_WARNINGS
    UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0501
    TTP_LAME_BACKEND=lame_${id}_backend)
  target_link_libraries(ttp_lame_${id}_backend PRIVATE ttp_lame_${id})
  target_link_libraries(ttp_lame INTERFACE ttp_lame_${id}_backend)
endfunction()

add_library(ttp_lame INTERFACE)
add_lame_backend(3101 ttplayer_lame_3101 3.101
  7578af6eebd578b2bd64e468fac4ae1f03670a7e028166e67f855674b9b6aeac 3 101 2 0)
# Preserve the existing source override name for the default 4.0 backend.
add_lame_backend(40 ttplayer_lame 4.0
  3df5124d5ad3a98312ffd7ba6a9b36230e4f8a3e66d3ce0f425e336c32d216eb 4 0 2 0)
add_lame_backend(41beta1 ttplayer_lame_41beta1 4.1beta1
  e4fa61f7752b1b5cb71eb4c170dab0e713279ec542f844a771b545aa537b45f4 4 1 1 1)
