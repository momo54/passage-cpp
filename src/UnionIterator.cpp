#include "UnionIterator.h"

namespace passage {

UnionIterator::UnionIterator(std::unique_ptr<Iterator> left,
                             std::function<std::unique_ptr<Iterator>()> right,
                             Cont rightAsWritten)
    : left_{std::move(left)},
      makeRight_{std::move(right)},
      rightAsWritten_{std::move(rightAsWritten)} {}

bool UnionIterator::hasNext() {
  if (!leftDone_) {
    // A `PauseException` from the left passes through unchanged.
    if (left_->hasNext()) { return true; }
    leftDone_ = true;
  }
  // The right branch is built only once the left is spent, so that a pause
  // during the left does not have to describe a branch that never started.
  if (right_ == nullptr) { right_ = makeRight_(); }
  return right_->hasNext();
}

Solution UnionIterator::next() {
  return leftDone_ ? right_->next() : left_->next();
}

Cont UnionIterator::pause() {
  if (!leftDone_) {
    // Paused inside the left branch: it keeps its state, and the right has to
    // be re-emitted whole, since it has not run.
    return unite(left_->pause(), rightAsWritten_);
  }
  // Paused inside the right branch, so the left is finished and contributes
  // nothing. `unite` would drop it anyway.
  return right_ == nullptr ? rightAsWritten_ : right_->pause();
}

}  // namespace passage
