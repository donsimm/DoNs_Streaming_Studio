# dss-core: Kern (Hintergrundprogramm), ohne Fenster
qt_add_executable(dss-core main.cpp)
target_link_libraries(dss-core PRIVATE dss_common dss_warnings Qt6::Network)
