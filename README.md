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
git submodule update --init qlever
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target passage -j $(nproc)
```

On macOS the configure line needs the flags the fork's own build script uses
(`ICU_ROOT`, `OPENSSL_ROOT_DIR`, `-DMETA_NO_STD_FORWARD_DECLARATIONS`, and a
Homebrew `abseil` unlinked); see `passage-qlever/build-native.sh` in the
Passage fork, which has them in one place.

QLever is added with `EXCLUDE_FROM_ALL`, so its tests and benchmarks are
defined but not built — only what `passage` links.

## Loading data

A query needs an index, and an index is built from RDF by `qlever-index`,
QLever's own indexer. It is not built by default here {D} QLever is added with
`EXCLUDE_FROM_ALL` {D} so ask for it:

```bash
cmake --build build --target qlever-index -j $(nproc)
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
asks for the rest, and a third evaluation that returns no continuation {D}
which is what ends the sequence. Without `--loop` it stops after the first,
which is what a client actually receives.

## Testing

```bash
cmake --build build --target ContinuationTest -j $(nproc)
./build/ContinuationTest
```

Six tests, each building its own index from Turtle under `$TMPDIR`, so none of
them can be skipped for want of data. The one that matters checks that the
sequence of continuations enumerates the pattern *exactly once* {D} the same
solutions in the same order as an evaluation that was never interrupted, for
quotas of 1, 2, 3 and 5.

## What it is not

* **Not a BGP.** One pattern only. A basic graph pattern needs a nested loop
  over several cursors and a continuation that carries one offset per pattern,
  which is the next step and the first interesting one.
* **Not an engine.** No `FILTER`, no `OPTIONAL`, no `UNION`, no aggregation. A
  query outside the fragment is refused by name rather than answered wrongly.
* **Not faster than Passage, as far as anyone has measured.** Nothing here has
  been benchmarked against the Java engine. The numbers quoted above were
  measured on the Java side, and they are the reason to try, not a result of
  trying.
* **Not a replacement.** Passage's `Backend` interface exists so that the
  engine can outlive one store — Blazegraph is unmaintained, which is the whole
  motivation. A kernel written against QLever's internals gives that up.
