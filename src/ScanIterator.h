// The scan operator: one triple pattern, over a `qlever::ScanCursor`.
//
// The counterpart of Passage's `PassageScan`, and its `pause()` reproduces the
// same shape:
//
//   OpSlice(OpJoin(input.asBindAs(), op), wrapped.current(), limit)
//
// the bindings that led here, the pattern *unmoved*, and an offset read off
// the cursor. The pattern is not rewritten, which is what makes a continuation
// a query the engine can read back rather than an encoding of its state.
#ifndef PASSAGE_SCAN_ITERATOR_H
#define PASSAGE_SCAN_ITERATOR_H

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "Iterator.h"
#include "index/Index.h"
#include "libqlever/ScanCursor.h"
#include "parser/SparqlTriple.h"

namespace passage {

class ScanIterator : public Iterator {
 public:
  // `bound` are the variables of this pattern that the loops above have
  // already fixed: their `Id` binds the scan, their text goes into the
  // continuation. Empty for a scan at the root.
  ScanIterator(const Index& index, SparqlTripleSimple pattern,
               std::vector<Binding> bound, size_t offset, Context& context);

  bool hasNext() override;
  Solution next() override;
  Cont pause() override;

  // Reopen this scan on new bindings, without rebuilding the operator.
  //
  // This is Volcano's contract and it was half-missing: a tree is built once
  // and its operators are reopened, where `JoinIterator` used to allocate a
  // fresh `ScanIterator` — and with it a fresh `ScanCursor` and a fresh
  // projection — for every outer solution. Ten thousand allocations where
  // the model says none, worth about 5 µs each.
  //
  // The bound variables must be the same ones, only their terms differ, which
  // is what a nested loop does: the projection is therefore kept.
  void reset(std::vector<Binding> bound, size_t offset);

 private:
  // Open or reopen the cursor for the bindings in hand.
  void openCursor(size_t offset);

  // Kept for `pause()`, which resolves the saved bindings to text.
  const Index& index_;
  // The pattern as the query writes it, which is what `pause()` prints.
  SparqlTripleSimple pattern_;
  std::vector<Binding> bound_;
  Context& context_;
  // The variables this scan itself produces: those not already bound.
  std::vector<std::pair<qlever::ScanCursor::Component, std::string>> projected_;
  // `std::optional` and not `unique_ptr`: `reset` then reuses this storage
  // instead of going back to the allocator, which is the point.
  std::optional<qlever::ScanCursor> cursor_;
};

}  // namespace passage

#endif
