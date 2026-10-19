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

## Running

```bash
build/passage --index $HOME/qlever-fig2/fig2 \
  --query 'SELECT * WHERE { ?human <http://ex.org/type> <http://ex.org/Human> }' \
  --quota 1 --loop
```

Each evaluation prints its solutions and the continuation that follows it.
Without `--loop` it stops after one, which is what a client receives.

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
