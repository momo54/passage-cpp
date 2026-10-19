// A Passage kernel, for a single triple pattern.
//
// Passage answers a SPARQL query with correct but possibly partial results,
// plus a *continuation query* that returns the missing ones; evaluating the
// continuation yields more results and another continuation, until none comes
// back. The engine therefore never has to finish within a quota — it has to
// make progress within one.
//
// This is that loop for the one case where it is simplest: a query whose body
// is a single triple pattern. The state a continuation carries is then an
// `OFFSET`, which is exactly what `qlever::ScanCursor` reports and accepts.
//
// Almost nothing here is new code. QLever supplies the parser, the index, the
// vocabulary, the `Id` handling, and — in this fork — a scan that can be
// suspended and resumed:
//
//   * `SparqlParser::parseQuery` turns the text into a `ParsedQuery`, from
//     which a single triple pattern and a `LIMIT`/`OFFSET` clause are read, so
//     that a continuation this kernel prints can be fed straight back to it;
//   * `ScanCursor::skip(offset)` resumes without reading what it skips, which
//     is the fork's one patch to QLever;
//   * `ScanCursor::current()` reports where it stopped, which is the state the
//     continuation has to carry.
#ifndef PASSAGE_CONTINUATION_H
#define PASSAGE_CONTINUATION_H

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "index/Index.h"
#include "index/LocatedTriples.h"

namespace passage {

// How much work one evaluation may do before it has to pause. Passage's
// progress condition is that an evaluation produces at least one solution, so
// a quota of zero would never terminate and is rejected.
struct Quota {
  size_t maxSolutions = 1000;
};

// One solution: the variables of the pattern, in the order they occur in it,
// bound to the strings they matched.
struct Solution {
  std::vector<std::pair<std::string, std::string>> bindings;
};

// What one evaluation produced, and how to ask for the rest.
struct Outcome {
  std::vector<Solution> solutions;
  // The query to evaluate next, or nothing when the pattern is exhausted.
  // Absence is what terminates the sequence.
  std::optional<std::string> continuation;
  // The offset this evaluation started from, for the record.
  size_t startedAt = 0;
};

// Evaluate `query` against `index` until the quota is spent or the pattern is
// exhausted. `query` must be a `SELECT` whose body is one triple pattern; it
// may carry an `OFFSET`, which is how a continuation comes back in.
//
// Throws `std::runtime_error` with a readable reason when the query is outside
// that fragment, rather than answering something wrong about it.
Outcome evaluate(const Index& index, const std::string& query,
                 const Quota& quota);

}  // namespace passage

#endif
