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
