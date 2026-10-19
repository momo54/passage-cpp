// The nested-loop join: for each solution of the left, a fresh scan of the
// right, bound by it.
//
// This is the algorithm Passage uses and QLever does not have. QLever joins
// two *sorted, materialised* inputs — a zipper join, a galloping join, or a
// hash join — and its six permutations exist so that there is always an order
// in which both sides are sorted. None of those can be suspended at an offset
// and resumed, which is why a continuation engine nests loops instead and
// why `ScanCursor` had to be added to the fork.
//
// `pause()` is `Pause2Next.visit(OpJoin)`, transcribed:
//
//   unflattenUnion(Arrays.asList(right, OpJoin.create(left, join.getRight())))
//
// a union of (i) the right where it stands, and (ii) the left taken further
// with the right pattern ENTIRE — because when the outer loop advances, the
// inner one starts over. That union is how a pair of offsets is written in
// SPARQL; it is not a stylistic choice.
#ifndef PASSAGE_JOIN_ITERATOR_H
#define PASSAGE_JOIN_ITERATOR_H

#include <memory>
#include <optional>
#include <vector>

#include "Iterator.h"
#include "ScanIterator.h"
#include "index/Index.h"
#include "parser/SparqlTriple.h"

namespace passage {

class JoinIterator : public Iterator {
 public:
  // `rightOffset` is where the inner scan resumes on the *first* outer
  // solution, which is what a continuation of a paused inner loop carries.
  // Every later outer solution restarts the inner scan at zero.
  JoinIterator(const Index& index, std::unique_ptr<Iterator> left,
               SparqlTripleSimple right, Context& context,
               size_t rightOffset = 0);

  bool hasNext() override;
  Solution next() override;
  Cont pause() override;

 private:
  // Open the inner scan for the outer solution in hand.
  void openRight(size_t offset);

  const Index& index_;
  std::unique_ptr<Iterator> left_;
  SparqlTripleSimple rightPattern_;
  Context& context_;
  size_t firstRightOffset_;
  // The outer solution the inner scan is running under, and the scan itself.
  Solution leftSolution_;
  std::unique_ptr<Iterator> right_;
  bool started_ = false;
};

}  // namespace passage

#endif
