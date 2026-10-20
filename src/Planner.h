// Reading a continuation back.
//
// A kernel that writes a query it cannot read is a kernel whose sequences do
// not terminate, so this accepts exactly the shapes `pause()` emits and
// refuses everything else by name:
//
//   a group            a conjunction, written `{ ... }`
//   VALUES ?v { <t> }  a variable the loops above had bound
//   { SELECT ... OFFSET n }   where an outer loop resumes
//   tp1 . tp2 . ...    a basic graph pattern, run as nested loops
//   { ... } UNION { ... }     the two halves of a paused nested loop
//
// Deliberately absent: `BIND`. `Bindings` prints `VALUES` precisely so that
// reading it back is reading terms instead of walking an expression tree.
#ifndef PASSAGE_PLANNER_H
#define PASSAGE_PLANNER_H

#include <memory>

#include "Algebra.h"
#include "Iterator.h"
#include "index/Index.h"
#include "parser/ParsedQuery.h"

namespace passage {

// The operator tree for `query`. Throws `std::runtime_error`, naming what it
// found, for anything outside the fragment above.
std::unique_ptr<Iterator> buildTree(const Index& index,
                                    const ParsedQuery& query,
                                    Context& context);

// The same query as continuation algebra, without running it. Needed for a
// union branch that the execution paused before reaching: it has no iterator
// to ask for its state, so it is re-emitted as written.
Cont queryAsCont(const ParsedQuery& query);

}  // namespace passage

#endif
