# Kamera-Dienst (P15): Python, nur Standardbibliothek; hier nur die Tests in ctest.
find_program(DSS_PYTHON3 python3 REQUIRED)

# HTTP-Schnittstelle, Absenderprüfung, Sperre und dienst.ini mit dem Dienst als eigenem Prozess.
add_test(NAME kd_api
  COMMAND ${DSS_PYTHON3} ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_api.py)
set_tests_properties(kd_api PROPERTIES TIMEOUT 120)
