# dss-cli: Werkzeug ohne Fenster für den Handtest des Overlay-Modells (P04)
qt_add_executable(dss-cli main.cpp)
target_link_libraries(dss-cli PRIVATE dss_common dss_warnings)
