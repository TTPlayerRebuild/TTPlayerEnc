# Add a memory-only variant of the pinned 3.93.1 tag writer. Preserve the original
# file-based functions and upstream source tree; only the generated TU changes.
# The two derived functions retain VbrTag.c's LGPL notice in that translation unit.
function(prepare_lame_3931_tag root output)
  file(READ "${root}/libmp3lame/VbrTag.c" original)
  string(REPLACE "\r\n" "\n" original "${original}")
  foreach(name IN ITEMS PutLameVBR PutVbrTag)
    string(FIND "${original}" "int ${name}(" start)
    if(start LESS 0)
      message(FATAL_ERROR "Expected LAME 3.93.1 ${name} implementation")
    endif()
    string(SUBSTRING "${original}" ${start} -1 tail)
    string(FIND "${tail}" "\n}\n" end)
    if(end LESS 0)
      message(FATAL_ERROR "Expected LAME 3.93.1 ${name} function end")
    endif()
    math(EXPR length "${end}+3")
    string(SUBSTRING "${tail}" 0 ${length} ${name})
  endforeach()

  string(REPLACE "int PutLameVBR(lame_global_flags *gfp, FILE *fpStream,"
    "static int PutLameVBRMemory(lame_global_flags *gfp, long stream_size," memory_lame "${PutLameVBR}")
  string(REPLACE "fseek(fpStream, 0, SEEK_END);" "" memory_lame "${memory_lame}")
  string(REPLACE "nFilesize = ftell(fpStream);" "nFilesize = stream_size;" memory_lame "${memory_lame}")

  string(REPLACE "int PutVbrTag(lame_global_flags *gfp,FILE *fpStream,int nVbrScale)"
    "size_t lame_get_lametag_frame(lame_global_flags *gfp, unsigned char *buffer, size_t capacity, const unsigned char first_header[4], unsigned long stream_size)"
    memory_tag "${PutVbrTag}")
  string(REPLACE "    unsigned char id3v2Header[10];" "" memory_tag "${memory_tag}")
  string(REPLACE "if (gfc->VBR_seek_table.pos <= 0)\n\treturn -1;"
    "if (!gfp->bWriteVbrTag || gfp->TotalFrameSize <= 0 || gfc->VBR_seek_table.pos <= 0) return 0;\n    if (!buffer || capacity < (size_t)gfp->TotalFrameSize) return (size_t)gfp->TotalFrameSize;"
    memory_tag "${memory_tag}")
  string(FIND "${memory_tag}" "/* Seek to end of file*/" io_start)
  string(FIND "${memory_tag}" "/* the default VBR header." io_end)
  if(io_start LESS 0 OR io_end LESS io_start)
    message(FATAL_ERROR "Expected LAME 3.93.1 tag input I/O block")
  endif()
  string(SUBSTRING "${memory_tag}" 0 ${io_start} before)
  string(SUBSTRING "${memory_tag}" ${io_end} -1 after)
  set(memory_tag "${before}lFileSize = (long)stream_size;\n    id3v2TagSize = 0;\n    memcpy(pbtStreamBuffer, first_header, 4);\n\n    ${after}")
  string(REPLACE "PutLameVBR(gfp, fpStream," "PutLameVBRMemory(gfp, lFileSize," memory_tag "${memory_tag}")
  string(FIND "${memory_tag}" "/*Seek to the beginning of the stream */" io_start)
  if(io_start LESS 0)
    message(FATAL_ERROR "Expected LAME 3.93.1 tag output I/O block")
  endif()
  string(SUBSTRING "${memory_tag}" 0 ${io_start} before)
  set(memory_tag "${before}memcpy(buffer, pbtStreamBuffer, (size_t)gfp->TotalFrameSize);\n    return (size_t)gfp->TotalFrameSize;\n}\n")
  # Fail closed if a source override changes any of the required transforms.
  if(memory_lame MATCHES "fpStream[,)]" OR memory_tag MATCHES "fpStream[,)]|return -1" OR
      NOT memory_tag MATCHES "capacity <" OR NOT memory_lame MATCHES "static int PutLameVBRMemory")
    message(FATAL_ERROR "Unsupported LAME 3.93.1 VBR tag source layout")
  endif()
  file(CONFIGURE OUTPUT "${output}" CONTENT
    "${original}\n/* TTPlayer: memory-only tag adapter derived from the LGPL functions above. */\n${memory_lame}\n${memory_tag}\nsize_t ttp_lame_3931_tag_size(lame_global_flags *gfp) { return gfp->bWriteVbrTag ? (size_t)gfp->TotalFrameSize : 0; }\n" @ONLY)
endfunction()
