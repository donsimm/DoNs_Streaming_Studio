# dss-core: Kern (Hintergrundprogramm), ohne Fenster
qt_add_executable(dss-core main.cpp coreservice.h coreservice.cpp)
target_link_libraries(dss-core PRIVATE dss_common dss_warnings Qt6::Network)
# Grösse neuer Bild- und Videoquellen aus der Datei (P09, Schalter DSS_MEDIA_SIZE)
if(DSS_MEDIA_SIZE)
  target_link_libraries(dss-core PRIVATE dss_media)
  target_compile_definitions(dss-core PRIVATE DSS_MEDIA_SIZE=1)
endif()
