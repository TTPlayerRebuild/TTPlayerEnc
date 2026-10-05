include(FetchContent)
set(LAME_VERSION "4.0")
FetchContent_Declare(ttplayer_lame
  URL "https://downloads.sourceforge.net/project/lame/lame/${LAME_VERSION}/lame-${LAME_VERSION}.tar.gz"
  URL_HASH SHA256=3df5124d5ad3a98312ffd7ba6a9b36230e4f8a3e66d3ce0f425e336c32d216eb
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE SOURCE_SUBDIR libmp3lame)
FetchContent_MakeAvailable(ttplayer_lame)
file(READ "${ttplayer_lame_SOURCE_DIR}/libmp3lame/version.h" version_header)
if(NOT version_header MATCHES "LAME_MAJOR_VERSION +4" OR NOT version_header MATCHES "LAME_MINOR_VERSION +0")
  message(FATAL_ERROR "Expected official LAME 4.0 sources")
endif()
set(sources VbrTag bitstream encoder fft gain_analysis id3tag lame newmdct presets
  psymodel quantize quantize_pvt reservoir set_get tables takehiro util vbrquantize version)
list(TRANSFORM sources PREPEND "${ttplayer_lame_SOURCE_DIR}/libmp3lame/")
list(TRANSFORM sources APPEND ".c")
add_library(ttp_lame STATIC ${sources})
target_include_directories(ttp_lame PUBLIC "${ttplayer_lame_SOURCE_DIR}/include"
  PRIVATE "${ttplayer_lame_SOURCE_DIR}" "${ttplayer_lame_SOURCE_DIR}/libmp3lame")
# Upstream's MSVC configuration; no CLI, mpg123 decoder, DLL or assembler dependency.
target_compile_options(ttp_lame PRIVATE "/FI${ttplayer_lame_SOURCE_DIR}/configMS.h"
  /Os /Gy /Gw /GF /fp:precise /arch:SSE2 /utf-8 /wd4244 /wd4267 /wd4996)
target_compile_definitions(ttp_lame PRIVATE _CRT_SECURE_NO_WARNINGS)
