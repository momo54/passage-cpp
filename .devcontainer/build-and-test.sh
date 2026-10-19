#!/usr/bin/env bash
#
# The whole README, end to end, so that what it claims is checked rather than
# believed: configure, build, build the indexer, load six triples, run a
# continuation sequence, run the tests.
#
# Not an `onCreateCommand`: QLever is a long C++ build and a container that
# takes that long to create is worse than a script run once, deliberately.
set -euo pipefail

cd "$(dirname "$0")/.."
START=$(date +%s)
step() { echo; echo "===== $* ($(( $(date +%s) - START ))s)"; }

step "the pinned QLever"
git submodule update --init qlever
echo "pinned at $(git -C qlever rev-parse --short HEAD)"

step "configure"
# No macOS flags, and no `-DCMAKE_POSITION_INDEPENDENT_CODE=ON` either: that
# one is needed for the Passage fork's shared JNI library, and everything here
# is an executable.
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release

step "build the kernel, the indexer and the tests"
# `JOBS` overrides, because QLever has translation units that take a couple of
# gigabytes and one job per core can exhaust a small machine. The compiler is
# then killed by the OOM killer, which reads as `c++: fatal error: Killed
# signal terminated program cc1plus` and is not a compiler bug.
JOBS=${JOBS:-$( (command -v nproc >/dev/null 2>&1 && nproc) || echo 4 )}
cmake --build build --target passage qlever-index ContinuationTest AlgebraTest -j "$JOBS"

step "load six triples"
WORK=${WORK:-$HOME/passage-demo}
mkdir -p "$WORK"
cat > "$WORK/fig2.ttl" <<'TTL'
<http://ex.org/H1> <http://ex.org/type> <http://ex.org/Human> .
<http://ex.org/H2> <http://ex.org/type> <http://ex.org/Human> .
<http://ex.org/H3> <http://ex.org/type> <http://ex.org/Human> .
<http://ex.org/H1> <http://ex.org/occupation> <http://ex.org/O1> .
<http://ex.org/H1> <http://ex.org/occupation> <http://ex.org/O2> .
<http://ex.org/H3> <http://ex.org/occupation> <http://ex.org/O3> .
TTL
(cd "$WORK" && "$OLDPWD/build/qlever/qlever-index" -i fig2 -f fig2.ttl -F ttl -p true)

step "a continuation sequence"
# Expect three evaluations: OFFSET 1, OFFSET 2, then no continuation, which is
# what ends the sequence.
./build/passage --index "$WORK/fig2" \
    --query 'SELECT * WHERE { ?human <http://ex.org/type> <http://ex.org/Human> }' \
    --quota 1 --loop

step "the tests"
./build/ContinuationTest
./build/AlgebraTest

step "done"
