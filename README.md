# passage-cpp

A [Passage](https://github.com/passage-org/passage) kernel in C++, for a single
triple pattern, built on QLever.

Passage answers a SPARQL query with correct but possibly partial results, plus
a **continuation query** that returns the missing ones. Evaluating the
continuation yields more results and another continuation, until none comes
back. The engine therefore never has to finish within a quota — it has to make
*progress* within one, which is what makes it work against endpoints that cut
queries off.

This is that loop for the case where it is simplest, and only that case: a
query whose body is one triple pattern. The state a continuation carries is
then an `OFFSET`.

## Everything at once

The sections below take it apart; this does the whole of it, and is the only
path that has been run end to end on a machine with nothing installed:

```bash
git clone https://github.com/momo54/passage-cpp.git
cd passage-cpp
.devcontainer/install-deps.sh          # Ubuntu; apt, no devcontainer features
.devcontainer/build-and-test.sh
```

It initialises the submodule, configures, builds the kernel, the indexer and
both test suites, loads the six triples of Figure 2, runs a continuation
sequence and runs the twelve tests, printing the elapsed time at each step. It
picks its own parallelism from `/proc/meminfo`; `JOBS=n` overrides and
`WORK=<dir>` moves the demo index, which otherwise goes to `~/passage-demo`.

Verified in a fresh `ubuntu:24.04` container that clones from GitHub: 159 s of
configure, then the build, then the sequence and twelve green tests. That
container showed 7 GB, so it built with two jobs; a 16 GB Codespace gets four.

In a Codespace it is the same two commands, `install-deps.sh` having already
run as `onCreateCommand`:

```bash
gh codespace create -R momo54/passage-cpp -m standardLinux32gb
# then, inside
.devcontainer/build-and-test.sh
```

## Why it exists

Passage is written in Java and reaches QLever through a JNI bridge. Measured on
a nested-loop join over WatDiv 10M, that bridge costs a factor of 1.68 — and
against QLever's *own* engine, which merge-joins instead of nesting loops, the
Java engine is about ten times slower. The first of those two numbers is what
a C++ kernel would recover; the second is the price of being resumable and a
rewrite does not touch it.

So this is a trial, not a plan: it asks what a Passage kernel looks like when
it sits inside QLever rather than above it, and how much of QLever it can
simply use.

## What it uses rather than reimplements

Almost everything. The kernel is ~130 lines because QLever supplies:

| | |
| --- | --- |
| `SparqlParser::parseQuery` | the query text, including the `OFFSET` a continuation carries, so a continuation this kernel prints is a query it can read back |
| `Index`, `IndexImpl` | the six permutations, the vocabulary, the delta triples |
| `qlever::ScanCursor` | a scan that can be suspended and resumed: `skip(offset)` positions without reading what it skips, `current()` reports where it stopped |
| `TripleComponent` | parsing and printing a variable, an IRI or a literal |

`ScanCursor` is the one piece upstream QLever does not have; it lives in the
fork this repository pins as a submodule, together with the patch that makes a
deep `OFFSET` cost a metadata walk instead of a scan.

## Building

```bash
git clone https://github.com/momo54/passage-cpp.git
cd passage-cpp
git submodule update --init qlever
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target passage -j $(( $(nproc) < 4 ? $(nproc) : 4 ))
```

**Not `-j $(nproc)`.** QLever's heaviest translation units —
`sparqlExpressions`, and `RelationalExpressions.cpp` above all — take two to
three gigabytes each, so one job per core exhausts anything smaller than a
workstation and the OOM killer stops the compiler:

```
c++: fatal error: Killed signal terminated program cc1plus
```

which looks like a compiler bug and is not one. Budget about three gigabytes a
job: measured here, `nproc` jobs against 7 GB dies at 85% and two does not;
and four jobs in 16 GB is what built the Passage fork's JNI bridge, which
compiles the same QLever, in a Codespace. This repository has been verified in
an Ubuntu 24.04 container, not yet in a Codespace. If you would rather not
think about it, `.devcontainer/build-and-test.sh` computes it for you; see
*Everything at once* above.

`-DCHEAPER_COMPILATION=ON` is the other way out, trading run-time performance
for compile time and memory.

Expect **two to three minutes before anything compiles**: QLever fetches
abseil, antlr, re2, range-v3, spatialjoin, s2 and opentelemetry at configure
time. It is not a slow start to the build, it is the download.

**A clone is enough; a fresh `git init` with no commit is not.** QLever fetches
`spatialjoin`, whose CMake reads git information through
`GetGitRevisionDescription.cmake`, which walks up to the nearest `.git` —
under `build/_deps/` that is this repository's. With no commit the branch has
no ref file to read, and configuring ends on `file failed to open for reading:
.../git-data/head-ref`. Commit first.

On macOS, Apple Clang needs the flags the Passage fork's build script uses, and
a Homebrew `abseil` has to be unlinked or it shadows the one QLever pins:

```bash
brew unlink abseil        # reversible with `brew link abseil`
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0 -DUSE_PARALLEL=false \
  -D_NO_TIMING_TESTS=ON -DCOMPILER_SUPPORTS_MARCH_NATIVE=FALSE \
  -DICU_ROOT="$(brew --prefix icu4c)" \
  -DOPENSSL_ROOT_DIR="$(brew --prefix openssl@3)" \
  -DCMAKE_CXX_FLAGS=-DMETA_NO_STD_FORWARD_DECLARATIONS
```

QLever is added with `EXCLUDE_FROM_ALL`, so its tests and benchmarks are
defined but not built — only what a target here links. That is also why
`qlever-index`, which loads data, has to be asked for by name.

Two blocks in `CMakeLists.txt` exist because almost everything QLever
configures is scoped to its own directory, and a project that adds it as a
subdirectory is its *parent*. The `find_package` calls are repeated here so
that `OpenSSL::SSL` and its kin exist in this scope; and QLever's
`INCLUDE_DIRECTORIES` and `LINK_DIRECTORIES` properties are read off its
directory and applied to the targets. Without the first, generating fails on
`Target "passage" links to OpenSSL::SSL but the target was not found`; without
the second, compiling fails on `'range/v3/all.hpp' file not found` and then
linking on `ld: library 'jemalloc' not found`.

## Loading data

A query needs an index, and an index is built from RDF by `qlever-index`,
QLever's own indexer. It is not built by default here — QLever is added with
`EXCLUDE_FROM_ALL` — so ask for it:

```bash
cmake --build build --target qlever-index -j 4   # see the note on -j above
```

Then, in a directory of your choosing, the six triples of Figure 2 of the
Passage paper:

```bash
mkdir -p ~/passage-demo && cd ~/passage-demo
cat > fig2.ttl <<'TTL'
<http://ex.org/H1> <http://ex.org/type> <http://ex.org/Human> .
<http://ex.org/H2> <http://ex.org/type> <http://ex.org/Human> .
<http://ex.org/H3> <http://ex.org/type> <http://ex.org/Human> .
<http://ex.org/H1> <http://ex.org/occupation> <http://ex.org/O1> .
<http://ex.org/H1> <http://ex.org/occupation> <http://ex.org/O2> .
<http://ex.org/H3> <http://ex.org/occupation> <http://ex.org/O3> .
TTL
~/passage-cpp/build/qlever/qlever-index -i fig2 -f fig2.ttl -F ttl -p true
```

`-i` is the basename every index file gets, `-F` the format (`ttl`, `nt`, `nq`,
`trig`), and `-p true` precomputes the patterns. It ends on
`INFO: Index build completed` and leaves `fig2.index.pso`,
`fig2.meta-data.json` and a dozen more beside them. **The basename, not a file,
is what `--index` takes.**

## Running

```bash
~/passage-cpp/build/passage --index ~/passage-demo/fig2 \
  --query 'SELECT * WHERE { ?human <http://ex.org/type> <http://ex.org/Human> }' \
  --quota 1 --loop
```

which prints, verbatim:

```
== evaluation 1, from offset 0: 1 solutions
   ?human=<http://ex.org/H1>
== continuation:
   SELECT * WHERE { ?human <http://ex.org/type> <http://ex.org/Human> } OFFSET 1
== evaluation 2, from offset 1: 1 solutions
   ?human=<http://ex.org/H2>
== continuation:
   SELECT * WHERE { ?human <http://ex.org/type> <http://ex.org/Human> } OFFSET 2
== evaluation 3, from offset 2: 1 solutions
   ?human=<http://ex.org/H3>
== done: 3 solutions in 3 evaluations
```

That is the whole idea in twelve lines: three answers, each with the query that
asks for the rest, and a third evaluation that returns no continuation —
which is what ends the sequence. Without `--loop` it stops after the first,
which is what a client actually receives.

## Testing

```bash
cmake --build build --target ContinuationTest AlgebraTest -j 4
./build/ContinuationTest
```

Thirty-six tests, in three suites, each building its own index from Turtle
under `$TMPDIR` so that none can be skipped for want of data.
`AlgebraTest`'s seven check the paused-state algebra and how it prints, down to
the exact SPARQL of Example 3.10.

`PassageCasesTest` runs Passage's own `BGPTest` cases against this kernel,
with its `triples9` data: three addresses, Alice owning three animals, three
species. They are worth more than the tests written here because their
expected answers were not computed here — Passage checks them against
Blazegraph evaluating the same query.

**It is not the same suite, and the count is worth being exact about.**
`BGPTest.java` has twelve test methods; the 181 tests its CI reports are those
twelve parameterised over execution contexts. Four are `@Disabled`, wanting
WDBench or WatDiv. Of the eight that remain:

| | |
| --- | --- |
| `a_tp_with_an_unknown_value`, `a_bgp_with_an_unknown_value` | run here |
| `offset_triple_pattern_return_no_match`, `issue_with_the_first_offset` | run here, the second one verbatim — it is a continuation written by hand in that suite |
| `a_literal_at_predicate_position` | run here: a single-row `VALUES` is inside the fragment |
| `weird_string_in_query` | cannot run as written anywhere — it names a Blazegraph of DBpedia at an absolute path on one author's laptop. Its substance, a literal carrying its own quotes, is tested against data of our own |
| `an_unknown_value_at_first_but_then_known` and its mirror | outside the fragment: a `VALUES` of several rows needs a table operator. A test records the refusal rather than leaving it unmentioned |

`UnionTest`'s three live cases run here too, which was not planned: a `UNION`
written by hand was outside the fragment until the planner learned to read
one, and it had to learn because a paused join *is* a union. They pass at
every quota, including the case whose two branches bind different variables
— the left `?p ?a ?s`, the right only `?p` — and the case whose right
branch is a join.

What is still untouched is `FilterTest`, 114 tests: `FILTER` is not in this
fragment, and adding it means expression evaluation, which is the one part of
QLever this kernel has so far managed to use without entering.

They found a real defect on the first run. A join of three patterns pauses
with its outer side *already a union*, so `pause()` produced a union joined
with a pattern — a shape the planner refuses, which means the kernel printed
a continuation it could not read and a sequence over three patterns could not
terminate. `join` now distributes over union, which is the choice made
everywhere else here: Passage factors a common suffix out of a union, this
pushes it into each branch. The three-pattern test written here had missed it,
because its data never made the outer union form.

Two more tests exist because a test that cannot fail says nothing. The sequence
is compared by its SOLUTIONS rather than by their number, since a resumed
inner branch that had lost `?human` from every solution still counted three.
And `aPauseInsideTheLeftBranchKeepsTheRightOne` uses data where an inner scan
has three matches, because Figure 2 never pauses *inside* the left branch of a
resumed union — with that data, a union that forgot to re-emit its unstarted
branch returned three solutions out of four and called itself done. The one that matters checks that the
sequence of continuations enumerates the pattern *exactly once* — the same
solutions in the same order as an evaluation that was never interrupted, for
quotas of 1, 2, 3 and 5.

## What it is not

* **A BGP resumes, and that is as far as it goes.** Several patterns are a
  nested loop; pausing one produces the continuation of Example 3.10 of the
  paper — the inner scan where it stands carrying the outer bindings, UNION
  the outer scan taken further with the inner pattern entire — and the kernel
  reads that back, so the sequence terminates on the same solutions in the same
  order whatever the quota. The union is how a *pair* of offsets is written in
  SPARQL, and the reader therefore accepts exactly what the writer emits: a
  group, `VALUES`, a subquery with an `OFFSET`, a basic graph pattern, a
  `UNION` of two groups, and a plain nested group. Anything else is refused by
  what it is.
* **No divergence from Passage's form, deliberately.** A paused join keeps the
  common suffix *outside* the union, as `Pause2Next.visit(OpJoin)` does —
  `{ {A} UNION {B} } tp`, not the suffix pushed into each branch — and
  nested unions print flattened, as `FlattenUnflatten.unflattenUnion` does.
  An earlier version distributed the suffix, which says the same thing in
  different text, and different text is a divergence: a continuation should
  read the same whichever implementation produced it.
  `PassageCasesTest.passagesOwnContinuationVerbatim` runs the continuation from
  Passage's own suite, character for character.
* **The saved bindings are `BIND`, as in the paper.** An earlier version wrote
  `VALUES ?v { <t> }` because reading a `Values` back is reading plain terms
  while a `Bind` carries an expression. That was convenience for the author of
  the reader, which is the wrong criterion twice: Example 3.10 of the paper
  shows `BIND`, and refusing `BIND` meant a continuation written by *Passage*
  could not be read here — an incompatibility in the one direction that
  matters. Extracting the constant is a `dynamic_cast` to `IriExpression`, the
  idiom QLever uses itself. Both forms are read; only `BIND` is written.
* **Not an engine.** No `FILTER`, no `OPTIONAL`, no `UNION` in the query, no
  aggregation. Anything outside the fragment is refused by name.

* **Not faster than Passage. Measured, not assumed.** On the same index, the
  same query, the same left-deep plan and the same patched QLever, this kernel
  runs the two-pattern join at 1.35 M solutions/s and the Java engine's warm
  plateau is 1.34—1.51 M. The same speed. A profile says why: 67% of the run is
  9,937 cold inner-cursor starts at ~10.7 us each, almost all of it the main
  thread asleep on a condition variable waiting for QLever's decompression pool
  to hand back the first block — and a bare C++ loop over `ScanCursor` pays the
  identical 10.4 us for the same starts. The Volcano protocol here costs ~130 ns
  per solution, mostly the `Solution` vector's allocation. Rewriting the engine
  in C++ does not touch the dominant cost, because that cost is a per-scan
  thread hand-off inside QLever. `journal.md` in the Passage repository has the
  full table and how to reproduce it.
* **Not a replacement.** Passage's `Backend` interface exists so that the
  engine can outlive one store — Blazegraph is unmaintained, which is the whole
  motivation. A kernel written against QLever's internals gives that up.
