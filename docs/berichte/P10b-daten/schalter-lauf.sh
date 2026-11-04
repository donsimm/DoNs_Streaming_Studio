#!/usr/bin/env bash
# P10b: Bau jedes Schalters einzeln aus (bzw. DSS_INSTALLED_EDITION ein) und Release, in einem
# frischen Klon. Release, Debug und der neue Schalter DSS_APP_MEDIA_DOCK zusätzlich mit ctest,
# nacheinander (ohne -j: die Fenstertests ziehen mit der Maus und vertragen keine zweiten
# Fenster). Aufruf: schalter-lauf.sh ARBEITSORDNER
W=$(readlink -m "$1"); mkdir -p "$W"; cd "$W" || exit 1
git clone -q /home/dominik/DoNs_Streaming_Studio klon || exit 1
echo "Klon: $(git -C klon log --oneline -1), Beginn $(date '+%F %T')"
build() { # name, testen (0/1), optionen…
    local name=$1 test=$2; shift 2
    local t0=$(date +%s)
    cmake -S klon -B b-$name -DCMAKE_CXX_FLAGS="-Wall -Wextra" "$@" > b-$name.log 2>&1 && cmake --build b-$name -j8 >> b-$name.log 2>&1
    local rc=$? res=""
    if [[ $rc == 0 && $test == 1 ]]; then
        mkdir -p "$W/fenster-config-$name"
        (cd b-$name && XDG_CONFIG_HOME="$W/fenster-config-$name" ctest > ../t-$name.log 2>&1)
        res=", ctest: $(grep -E 'tests passed|tests failed' t-$name.log | head -1)"
        grep -E '\*\*\*Failed|Failed  ' t-$name.log | sed 's/^/    /'
    fi
    echo "$name: Exit $rc, Warnungen $(grep -c 'warning:' b-$name.log), $(( $(date +%s) - t0 )) s$res"
    rm -rf b-$name
}
build release 1 -DCMAKE_BUILD_TYPE=Release
build debug 1
build aus-DSS_APP_MEDIA_DOCK 1 -DDSS_APP_MEDIA_DOCK=OFF
for o in DSS_BUILD_APP DSS_BUILD_TESTS DSS_BUILD_CLI DSS_MEDIA_SIZE DSS_APP_RENDERER DSS_APP_CANVAS_EDIT DSS_VIDEO_ROTATION DSS_CORE_WATCHDOG DSS_APP_EDITING DSS_APP_TRANSFORM DSS_CORE_IMPORT_THREAD DSS_APP_SETTINGS_DIALOG DSS_APP_CANVAS_IMAGE DSS_CORE_OVERLAY_SERVER DSS_APP_BROWSER_SOURCE; do
    build aus-$o 0 -D$o=OFF
done
build ein-DSS_INSTALLED_EDITION 0 -DDSS_INSTALLED_EDITION=ON -DCMAKE_INSTALL_PREFIX=$W/opt-pruefung
rm -rf klon
echo "fertig $(date '+%F %T')"
