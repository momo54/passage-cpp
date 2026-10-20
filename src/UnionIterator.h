// The union operator, needed only because a continuation is a union.
//
// A paused nested loop is written `{ inner here } UNION { outer further }`, so
// reading a continuation back means running a union. It runs its branches in
// order, left to exhaustion and then right, which is what makes the offsets
// inside them mean what they meant when they were written.
//
// `pause()` follows the same rule as the join's, for the same reason: the
// branch in flight is paused where it stands, and a branch not yet started is
// re-emitted ENTIRE. That is why this holds its right branch's algebra and not
// only its iterator — a branch that never ran has no iterator to ask.
#ifndef PASSAGE_UNION_ITERATOR_H
#define PASSAGE_UNION_ITERATOR_H

#include <functional>
#include <memory>

#include "Algebra.h"
#include "Iterator.h"

namespace passage {

class UnionIterator : public Iterator {
 public:
  // `right` is built on demand, and `rightAsWritten` is what to print if it
  // has not been built when the execution pauses.
  UnionIterator(std::unique_ptr<Iterator> left,
                std::function<std::unique_ptr<Iterator>()> right,
                Cont rightAsWritten);

  bool hasNext() override;
  Solution next() override;
  Cont pause() override;

 private:
  std::unique_ptr<Iterator> left_;
  std::function<std::unique_ptr<Iterator>()> makeRight_;
  Cont rightAsWritten_;
  std::unique_ptr<Iterator> right_;
  bool leftDone_ = false;
};

}  // namespace passage

#endif
