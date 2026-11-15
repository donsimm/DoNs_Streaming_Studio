# dss_secrets: Geheimnisse verschlüsselt ablegen mit systemd-creds --user (F1, Regel 12; aus
# ObsSecretStore von P10c herausgelöst in P13). Nur Qt Core, ohne dss_common und ohne protocol.h:
# dss_obs und dss_twitch binden sie ein.
add_library(dss_secrets STATIC
  secretstore.h secretstore.cpp
)
target_include_directories(dss_secrets PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(dss_secrets PUBLIC Qt6::Core PRIVATE dss_warnings)
