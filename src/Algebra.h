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
// ONE DELIBERATE DEVIATION. Passage writes the bindings that led to an
// iterator as `BIND(<term> AS ?v)`, because that is what Jena's
// `input.asBindAs()` produces. `Bindings` prints `VALUES ?v { <term> }`
// instead, which means the same thing for one row and is far easier to read
// back: QLever's parser hands a `Values` as plain `TripleComponent`s, while a
// `Bind` carries a `SparqlExpressionPimpl` whose constant would have to be
// dug out of an expression tree. A kernel has to read what it writes.
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
