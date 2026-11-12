# dss_audio: Mischer für einen Ausgang (P12b b2), nur Logik, ohne Gerät und ohne Qt (CLAUDE.md
# Regel 9). Die Quellen kommen über den Ring aus dss_audiodecoder (src/media/audioring.h). Die
# Ausgabe über PipeWire folgt in b3.
add_library(dss_audio STATIC
  mixer.h mixer.cpp
)
target_include_directories(dss_audio PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(dss_audio PUBLIC dss_audiodecoder PRIVATE dss_warnings)
