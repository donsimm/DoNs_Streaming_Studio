# Tests mit Qt Test, ausgeführt über ctest. Nur Qt Core und Qt Test, kein Qt Gui
# (ausser den Tests des Fensters unter DSS_BUILD_APP).
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

dss_add_test(tst_canvasview)

dss_add_test(tst_connectionstate)

dss_add_test(tst_windowsize)

# Testdateien wie im Handtest (CLAUDE.md Regel 7): Bild und Video aus
# prototypes/messung/media_erzeugen.sh, Schriftdatei aus dem System (fc-list).
set(DSS_TEST_MEDIA_DIR "${PROJECT_SOURCE_DIR}/prototypes/media" CACHE PATH "Testdateien (Bild, Video)")
set(DSS_TEST_FONT "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf" CACHE FILEPATH "Schriftdatei für Tests")
dss_add_test(tst_overlaystore)
target_compile_definitions(tst_overlaystore PRIVATE
  DSS_TEST_MEDIA_DIR="${DSS_TEST_MEDIA_DIR}" DSS_TEST_FONT="${DSS_TEST_FONT}"
  DSS_FORMAT_DOC="${PROJECT_SOURCE_DIR}/docs/06-overlay-format.md")
# Erzeugt die Testdateien, falls sie fehlen. Braucht ffmpeg (installiert seit P01, nicht in scripts/pakete.txt).
add_test(NAME media_vorbereiten
  COMMAND sh -c "test -f '${DSS_TEST_MEDIA_DIR}/alpha.webm' || sh '${PROJECT_SOURCE_DIR}/prototypes/messung/media_erzeugen.sh' '${DSS_TEST_MEDIA_DIR}'")
set_tests_properties(media_vorbereiten PROPERTIES FIXTURES_SETUP dss_media)
set_tests_properties(tst_overlaystore PROPERTIES FIXTURES_REQUIRED dss_media)

# Schrift des Fensters (P06): lädt src/app/schriften/ ohne Fenster (Plattform offscreen).
# Braucht Qt Gui, deshalb nur mit dem Fenster (DSS_BUILD_APP).
if(DSS_BUILD_APP)
  qt_add_executable(tst_fonts tst_fonts.cpp ${PROJECT_SOURCE_DIR}/src/app/fonts.cpp)
  target_include_directories(tst_fonts PRIVATE ${PROJECT_SOURCE_DIR}/src/app)
  target_compile_definitions(tst_fonts PRIVATE DSS_FONT_DIR="${PROJECT_SOURCE_DIR}/src/app/schriften")
  target_link_libraries(tst_fonts PRIVATE dss_warnings Qt6::Gui Qt6::Test)
  add_test(NAME tst_fonts COMMAND tst_fonts)
  set_tests_properties(tst_fonts PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

  # Symbole einfarbig (P06, Punkt 28), in der X11-Sitzung; ohne X11 übersprungen (77).
  qt_add_executable(tst_symbols tst_symbols.cpp ${PROJECT_SOURCE_DIR}/src/app/symbols.cpp)
  target_include_directories(tst_symbols PRIVATE ${PROJECT_SOURCE_DIR}/src/app)
  target_compile_definitions(tst_symbols PRIVATE DSS_SYMBOL_DIR="${PROJECT_SOURCE_DIR}/src/app/symbole")
  target_link_libraries(tst_symbols PRIVATE dss_warnings Qt6::Widgets Qt6::Test)
  add_test(NAME tst_symbols COMMAND tst_symbols)
  set_tests_properties(tst_symbols PROPERTIES SKIP_RETURN_CODE 77)
endif()

# Bildschirmfoto des Fensters in der X11-Sitzung (P05). Nur mit dem Fenster (DSS_BUILD_APP).
# Ohne X11-Sitzung meldet ctest den Test als übersprungen (Exit-Code 77).
if(DSS_BUILD_APP)
  find_program(DSS_PYTHON3 python3 REQUIRED)
  add_test(NAME bildschirmfoto
    COMMAND ${DSS_PYTHON3} ${CMAKE_CURRENT_SOURCE_DIR}/bildschirmfoto_pruefen.py
            $<TARGET_FILE:dss-app> ${CMAKE_CURRENT_BINARY_DIR}/bildschirmfoto)
  set_tests_properties(bildschirmfoto PROPERTIES SKIP_RETURN_CODE 77)
endif()
