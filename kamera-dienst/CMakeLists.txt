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
