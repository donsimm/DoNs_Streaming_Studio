# dss_audio: Mischer für einen Ausgang (P12b b2), nur Logik, ohne Gerät und ohne Qt (CLAUDE.md
# Regel 9). Die Quellen kommen über den Ring aus dss_audiodecoder (src/media/audioring.h).
add_library(dss_audio STATIC
  mixer.h mixer.cpp
  pageclock.h pageclock.cpp
)
target_include_directories(dss_audio PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(dss_audio PUBLIC dss_audiodecoder PRIVATE dss_warnings)

# P12b b4: Schnittstelle eines Ausgangs (audiooutput.h) und Ersatzausgang ohne Gerät (Testoption
# --ersatzausgang des Kerns, tst_kernton), Qt Core, ohne libpipewire. P14 t: Töne des Chat-Bots
# (actionsound.h) über einen eigenen Ausgang.
add_library(dss_audiooutput STATIC
  audiooutput.h audiooutput.cpp
  nulloutput.h nulloutput.cpp
  actionsound.h actionsound.cpp
)
target_include_directories(dss_audiooutput PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(dss_audiooutput PUBLIC dss_audio Qt6::Core PRIVATE dss_warnings)

# P12b b3: Ausgabe des Mischers über PipeWire (pw_thread_loop, pw_stream), nur Qt Core. Nur mit
# Schalter DSS_CORE_AUDIO; ohne ihn bindet kein Programm libpipewire.
if(DSS_CORE_AUDIO)
  find_package(PkgConfig REQUIRED)
  # GLOBAL: tst_tonausgang legt mit libpipewire selbst virtuelle Senken an
  pkg_check_modules(DSS_PIPEWIRE REQUIRED IMPORTED_TARGET GLOBAL libpipewire-0.3)
  add_library(dss_pwoutput STATIC
    pwoutput.h pwoutput.cpp
  )
  target_include_directories(dss_pwoutput PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
  target_link_libraries(dss_pwoutput PUBLIC dss_audiooutput Qt6::Core PRIVATE PkgConfig::DSS_PIPEWIRE dss_warnings)
endif()
