// The continuation algebra, and its printer.
//
// Passage represents a paused execution as a Jena algebra expression and turns
// it back into SPARQL with `OpAsQuery`. QLever has an algebra of its own
// (`ParsedQuery`, `QueryExecutionTree`) but nothing that prints one as a
// query, so this is the one piece that has to be written rather than reused.
//
// The node types are exactly those the paused state of this fragment needs,
// and they are named after their Jena counterparts so that the correspondence
// stays visible:
//
//   Done           `OpTable.empty()`  — nothing left; its absence is what ends
//                                      the sequence
//   Slice          `OpSlice`          — a sub-expression resumed at an offset
//   Pattern        `OpTriple`         — a triple pattern, unmoved
//   Bindings       `OpJoin(bindAs,…)` — the bindings that led to an iterator,
//                                      which a nested loop needs
//   Join           `OpJoin`           — a conjunction, written as a group
//   Union          `OpUnion`          — two paused branches side by side
//
// `Bindings` prints `BIND(<term> AS ?v)`, which is what Passage writes and
// what Example 3.10 of the paper shows. An earlier version printed
// `VALUES ?v { <term> }` because reading a `Values` back is reading plain
// terms while a `Bind` carries a `SparqlExpressionPimpl`. That was a choice
// made for the convenience of the reader, which is the wrong criterion twice
// over: the published form is `BIND`, and refusing `BIND` meant a continuation
// written by *Passage* could not be read here — an incompatibility in the
// one direction that matters. Extracting the constant turned out to be a
// `dynamic_cast` to `IriExpression`, which is what QLever's own
// `getIdOrLocalVocabEntryFromLiteralExpression` does. Both forms are accepted
// on the way in.
#ifndef PASSAGE_ALGEBRA_H
#define PASSAGE_ALGEBRA_H

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "parser/SparqlTriple.h"

namespace passage {

struct Node;
using Cont = std::shared_ptr<const Node>;

struct Node {
  enum struct Kind { Done, Slice, Pattern, Bindings, Join, Union };
  Kind kind;

  // Pattern
  std::optional<SparqlTripleSimple> triple;
  // Slice: the offset to resume at, and what to resume
  size_t offset = 0;
  // Bindings: `?var` bound to the term it took, as SPARQL text
  std::vector<std::pair<std::string, std::string>> bindings;
  // Slice, Bindings: one child; Join and Union: two
  std::vector<Cont> children;
};

Cont done();
Cont pattern(SparqlTripleSimple triple);
Cont slice(Cont child, size_t offset);
Cont bindings(std::vector<std::pair<std::string, std::string>> binds, Cont child);

// `OpJoin`: a conjunction, which prints as the two sides in one group. Used by
// a nested loop's pause for its second branch, where the left side is where
// the outer scan resumes and the right is the inner pattern *entire* — because
// when the outer advances, the inner starts over.
Cont join(Cont left, Cont right);

// `OpUnion` with Passage's rule: two finished branches are finished, and a
// finished branch beside a live one is just the live one. Without that, a
// continuation would accumulate empty `VALUES` clauses as a BGP pauses.
Cont unite(Cont left, Cont right);

bool isDone(const Cont& c);

// The expression as a SPARQL query, which is what a client receives and what
// this kernel can read back.
std::string asSparql(const Cont& c);

}  // namespace passage

#endif
