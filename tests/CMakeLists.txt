# Tests mit Qt Test, ausgeführt über ctest. Nur Qt Core und Qt Test, kein Qt Gui.
# https://doc.qt.io/qt-6.8/qttest-index.html
function(dss_add_test name)
  qt_add_executable(${name} ${name}.cpp)
  target_link_libraries(${name} PRIVATE dss_common dss_warnings Qt6::Test)
  add_test(NAME ${name} COMMAND ${name})
endfunction()

dss_add_test(tst_version)
target_compile_definitions(tst_version PRIVATE DSS_EXPECTED_VERSION="${PROJECT_VERSION}")

dss_add_test(tst_unixsignals)

dss_add_test(tst_overlaymodel)

# Testdateien wie im Handtest (CLAUDE.md Regel 7): Bild und Video aus
# prototypes/messung/media_erzeugen.sh, Schriftdatei aus dem System (fc-list).
set(DSS_TEST_MEDIA_DIR "${PROJECT_SOURCE_DIR}/prototypes/media" CACHE PATH "Testdateien (Bild, Video)")
set(DSS_TEST_FONT "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf" CACHE FILEPATH "Schriftdatei für Tests")
dss_add_test(tst_overlaystore)
target_compile_definitions(tst_overlaystore PRIVATE
  DSS_TEST_MEDIA_DIR="${DSS_TEST_MEDIA_DIR}" DSS_TEST_FONT="${DSS_TEST_FONT}")
# Erzeugt die Testdateien, falls sie fehlen. Braucht ffmpeg (installiert seit P01, nicht in scripts/pakete.txt).
add_test(NAME media_vorbereiten
  COMMAND sh -c "test -f '${DSS_TEST_MEDIA_DIR}/alpha.webm' || sh '${PROJECT_SOURCE_DIR}/prototypes/messung/media_erzeugen.sh' '${DSS_TEST_MEDIA_DIR}'")
set_tests_properties(media_vorbereiten PROPERTIES FIXTURES_SETUP dss_media)
set_tests_properties(tst_overlaystore PROPERTIES FIXTURES_REQUIRED dss_media)
