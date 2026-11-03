# dss-core: Kern (Hintergrundprogramm), ohne Fenster
qt_add_executable(dss-core main.cpp coreservice.h coreservice.cpp)
target_link_libraries(dss-core PRIVATE dss_common dss_warnings Qt6::Network)
# Watchdog von systemd (P10a, Punkt 41, Schalter DSS_CORE_WATCHDOG)
target_compile_definitions(dss-core PRIVATE DSS_CORE_WATCHDOG=$<BOOL:${DSS_CORE_WATCHDOG}>
  DSS_CORE_IMPORT_THREAD=$<BOOL:${DSS_CORE_IMPORT_THREAD}>)
# Grösse neuer Bild- und Videoquellen aus der Datei (P09, Schalter DSS_MEDIA_SIZE)
if(DSS_MEDIA_SIZE)
  target_link_libraries(dss-core PRIVATE dss_media)
  target_compile_definitions(dss-core PRIVATE DSS_MEDIA_SIZE=1)
endif()
# Version im Einsatz (P10a): <Präfix>/bin
if(DSS_INSTALLED_EDITION)
  install(TARGETS dss-core RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()
# Overlay-Server (P10, Schalter DSS_CORE_OVERLAY_SERVER). Die Seite (web/) liegt in Arbeit im
# Repo, im Einsatz im Installationsordner (wie Symbole und Schriften, P10a).
if(DSS_CORE_OVERLAY_SERVER)
  target_link_libraries(dss-core PRIVATE dss_server)
  if(DSS_INSTALLED_EDITION)
    set(DSS_WEB_DIR "${DSS_INSTALL_FULL_SHARE_DIR}/web")
    install(FILES ${PROJECT_SOURCE_DIR}/web/overlay.html ${PROJECT_SOURCE_DIR}/web/overlay.js
            DESTINATION ${DSS_INSTALL_SHARE_DIR}/web)
  else()
    set(DSS_WEB_DIR "${PROJECT_SOURCE_DIR}/web")
  endif()
  target_compile_definitions(dss-core PRIVATE DSS_CORE_OVERLAY_SERVER=1 DSS_WEB_DIR="${DSS_WEB_DIR}")
else()
  target_compile_definitions(dss-core PRIVATE DSS_CORE_OVERLAY_SERVER=0)
endif()
