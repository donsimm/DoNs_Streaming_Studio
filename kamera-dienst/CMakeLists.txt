# Kamera-Dienst (P15): Python, nur Standardbibliothek; hier nur die Tests in ctest.
find_program(DSS_PYTHON3 python3 REQUIRED)

# HTTP-Schnittstelle, Absenderprüfung, Sperre und dienst.ini mit dem Dienst als eigenem Prozess.
add_test(NAME kd_api
  COMMAND ${DSS_PYTHON3} ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_api.py)
set_tests_properties(kd_api PROPERTIES TIMEOUT 120)

# Gibt ioctl-Nummern und Strukturen aus den Kernel-Headern aus; kd_v4l2 vergleicht sie mit v4l2.py.
add_executable(kamera_ioctl_werte tests/kamera_ioctl_werte.cpp)
target_link_libraries(kamera_ioctl_werte PRIVATE dss_warnings)

# V4L2-Nummern, Suche über by-id mit Attrappen-Ordner, Ein- und Ausstecken, Wartezeit.
add_test(NAME kd_v4l2
  COMMAND ${DSS_PYTHON3} ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_v4l2.py $<TARGET_FILE:kamera_ioctl_werte>)
set_tests_properties(kd_v4l2 PROPERTIES TIMEOUT 120)

# Bildwerte: GET/PUT controls, Reihenfolge Automatik → Wert, gemerkte Werte in kameras.json.
add_test(NAME kd_werte
  COMMAND ${DSS_PYTHON3} ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_werte.py)
set_tests_properties(kd_werte PROPERTIES TIMEOUT 120)

# OBSBOT Tiny 2: UVC-Anfragen, Frame mit CRC, Statusblock, Wecken mit Wartezeit, Schlafen,
# Schlaf durch Neigen, kein «Neustart», HDR, gesperrte Funktionen (Attrappe).
add_test(NAME kd_obsbot
  COMMAND ${DSS_PYTHON3} ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_obsbot.py $<TARGET_FILE:kamera_ioctl_werte>)
set_tests_properties(kd_obsbot PROPERTIES TIMEOUT 120)

# Bewegen: Grenzen bei Dauerbewegung, Totmann, Wecken vor Bewegung, KI-Befehl gesperrt,
# Abbruch ohne Nachsenden, step, home, Richtung umkehren (ersetzbare Uhr und über HTTP).
add_test(NAME kd_bewegen
  COMMAND ${DSS_PYTHON3} ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_bewegen.py)
set_tests_properties(kd_bewegen PROPERTIES TIMEOUT 120)

# Kamera-Presets: speichern aus der Position, Abruf aus dem Schlaf, Grenzen, Bildwerte wahlweise,
# Kennungen, ungültige Datei (ersetzbare Uhr und über HTTP).
add_test(NAME kd_presets
  COMMAND ${DSS_PYTHON3} ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_presets.py)
set_tests_properties(kd_presets PROPERTIES TIMEOUT 120)

# Licht: Ein/Aus, Helligkeit, Temperatur, Begrenzung, stiller Fehler, Zeitüberschreitung,
# Content-Length, Nachführen alle 5 s, Licht-Presets (Attrappen-Lampen auf 127.0.0.1, über HTTP).
add_test(NAME kd_licht
  COMMAND ${DSS_PYTHON3} ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_licht.py)
set_tests_properties(kd_licht PROPERTIES TIMEOUT 120)
