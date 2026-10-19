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
//                                      which a nested loop will need
//   Union          `OpUnion`          — two paused branches side by side
//
// `Bindings` and `Union` are unreachable while the fragment is one triple
// pattern. They are here because leaving them out would shape the design
// around the restriction: a BGP pauses as a union of per-pattern states, each
// carrying the bindings of the loops above it, and that is the next step.
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
  enum struct Kind { Done, Slice, Pattern, Bindings, Union };
  Kind kind;

  // Pattern
  std::optional<SparqlTripleSimple> triple;
  // Slice: the offset to resume at, and what to resume
  size_t offset = 0;
  // Bindings: `?var` bound to the term it took, as SPARQL text
  std::vector<std::pair<std::string, std::string>> bindings;
  // Slice, Bindings: one child; Union: two
  std::vector<Cont> children;
};

Cont done();
Cont pattern(SparqlTripleSimple triple);
Cont slice(Cont child, size_t offset);
Cont bindings(std::vector<std::pair<std::string, std::string>> binds, Cont child);

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
