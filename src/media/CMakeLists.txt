# dss_media: Bilder und Videos über libavformat/libavcodec (P02, M1), nur Qt Core (CLAUDE.md
# Regel 9). P09: Grösse neuer Bild- und Videoquellen (Kern, dss-cli) und Dekodieren der Videos
# für den Zeichner im Editor (dss-app).
find_package(PkgConfig REQUIRED)
find_package(Threads REQUIRED) # std::thread der Videos
pkg_check_modules(DSS_FFMPEG REQUIRED IMPORTED_TARGET libavformat libavcodec libavutil libswscale)

add_library(dss_media STATIC
  avsupport.h avsupport.cpp
  mediainfo.h mediainfo.cpp
  videocolor.h videocolor.cpp
  videoplayer.h videoplayer.cpp
)
target_include_directories(dss_media PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(dss_media PUBLIC Qt6::Core Threads::Threads dss_common
                                PRIVATE PkgConfig::DSS_FFMPEG dss_warnings)
# P10a (Punkt 65): Drehung aus den Metadaten von Videos (Display-Matrix) beachten.
# Aus: Videos werden gezeigt, wie sie gespeichert sind (wie bis P09b).
target_compile_definitions(dss_media PRIVATE DSS_VIDEO_ROTATION=$<BOOL:${DSS_VIDEO_ROTATION}>)

# P12b b2: Ton von Videos dekodieren (float, 48 kHz, Stereo) für den Mischer (src/audio). Eigenes
# Ziel, damit Kern und Fenster libswresample erst binden, wenn sie den Ton nutzen (b3, b4).
pkg_check_modules(DSS_SWRESAMPLE REQUIRED IMPORTED_TARGET libswresample)
add_library(dss_audiodecoder STATIC
  audioring.h
  audiodecoder.h audiodecoder.cpp
)
target_include_directories(dss_audiodecoder PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(dss_audiodecoder PUBLIC Qt6::Core Threads::Threads
                                       PRIVATE dss_media PkgConfig::DSS_FFMPEG PkgConfig::DSS_SWRESAMPLE dss_warnings)
