# dss_media: Bilder und Videos über libavformat/libavcodec (P02, M1), nur Qt Core (CLAUDE.md
# Regel 9). P09: Grösse neuer Bild- und Videoquellen (Kern, dss-cli) und Dekodieren der Videos
# für den Zeichner im Editor (dss-app).
find_package(PkgConfig REQUIRED)
pkg_check_modules(DSS_FFMPEG REQUIRED IMPORTED_TARGET libavformat libavcodec libavutil libswscale)

add_library(dss_media STATIC
  avsupport.h avsupport.cpp
  mediainfo.h mediainfo.cpp
)
target_include_directories(dss_media PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(dss_media PUBLIC Qt6::Core PRIVATE PkgConfig::DSS_FFMPEG dss_warnings)
