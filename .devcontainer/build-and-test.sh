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
# The parallelism is chosen from MEMORY, not from cores. QLever's heaviest
# translation units — `sparqlExpressions`, and `RelationalExpressions.cpp`
# above all — take between two and three gigabytes each, so one job per core
# exhausts a small machine and the OOM killer stops the compiler. That reads as
#
#   c++: fatal error: Killed signal terminated program cc1plus
#
# which looks like a compiler bug and is not one.
#
# The anchor is a measurement, not arithmetic: the Passage fork's JNI bridge
# built in a Codespace with four cores and 16GB, at `nproc` = 4 jobs, so four
# gigabytes a job compiles QLever's heaviest units. Three is the budget below,
# slightly tighter, and it reproduces that four on a 16GB Codespace.
#
# What NOT to trust: the memory a container was started with. `docker run
# --memory=12g` on Docker Desktop still shows its VM's `MemTotal` here, 7GB,
# and `--cpus=6` is a CFS quota that leaves `nproc` reading 8. Reading
# `/proc/meminfo` is therefore right and reading the flags would be wrong: six
# jobs against that 7GB is what died, at 1.2 gigabytes a job.
#
# `JOBS=n` overrides, and `-DCHEAPER_COMPILATION=ON` on the configure line is
# the other way out — it uses type erasure to cut compile time and memory, at
# the cost of run-time performance, so it is not the default here.
cores=$( (command -v nproc >/dev/null 2>&1 && nproc) || echo 4 )
if [ -r /proc/meminfo ]; then
  mem_gb=$(( $(awk '/MemTotal/ {print $2}' /proc/meminfo) / 1024 / 1024 ))
elif command -v sysctl >/dev/null 2>&1; then
  mem_gb=$(( $(sysctl -n hw.memsize) / 1024 / 1024 / 1024 ))
else
  mem_gb=8
fi
by_memory=$(( mem_gb / 3 ))
[ "$by_memory" -ge 1 ] || by_memory=1
JOBS=${JOBS:-$(( cores < by_memory ? cores : by_memory ))}
echo "== $cores cores, ${mem_gb}GB, so -j $JOBS (three gigabytes a job)"
cmake --build build --target passage qlever-index ContinuationTest AlgebraTest PassageCasesTest -j "$JOBS"

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
./build/PassageCasesTest

step "done"
