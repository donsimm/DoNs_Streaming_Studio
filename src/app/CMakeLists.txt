# dss-app: Fenster. Dünn, ohne Logik (CLAUDE.md Regel 9).
qt_add_executable(dss-app main.cpp mainwindow.h mainwindow.cpp)
target_link_libraries(dss-app PRIVATE dss_common dss_warnings Qt6::Widgets)
