#!/usr/bin/env bash
# Bau jedes Schalters einzeln aus (bzw. DSS_INSTALLED_EDITION ein) und der Zwischen-Commits.
S=$(dirname "$(readlink -f "$0")")
cd $S
git clone -q /home/dominik/DoNs_Streaming_Studio klon || exit 1
echo "Klon: $(git -C klon log --oneline -1)"
build() { # name, quelle, optionen…
    local name=$1 src=$2; shift 2
    local t0=$(date +%s)
    cmake -S $src -B b-$name "$@" > b-$name.log 2>&1 && cmake --build b-$name -j8 >> b-$name.log 2>&1
    local rc=$?
    echo "$name: Exit $rc, Warnungen $(grep -c 'warning:' b-$name.log), $(( $(date +%s) - t0 )) s"
    rm -rf b-$name
}
for c in 0dbba12 0e447aa 439c979; do
    git -C klon worktree add -q --detach ../wt-$c $c
    build commit-$c wt-$c
done
build release klon -DCMAKE_BUILD_TYPE=Release
for o in DSS_BUILD_APP DSS_BUILD_TESTS DSS_BUILD_CLI DSS_MEDIA_SIZE DSS_APP_RENDERER DSS_APP_CANVAS_EDIT DSS_VIDEO_ROTATION DSS_CORE_WATCHDOG DSS_APP_EDITING DSS_APP_TRANSFORM; do
    build aus-$o klon -D$o=OFF
done
build ein-DSS_INSTALLED_EDITION klon -DDSS_INSTALLED_EDITION=ON -DCMAKE_INSTALL_PREFIX=$S/opt-pruefung
echo fertig
