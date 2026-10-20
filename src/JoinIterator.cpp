#include "JoinIterator.h"

namespace passage {

JoinIterator::JoinIterator(const Index& index, std::unique_ptr<Iterator> left,
                           SparqlTripleSimple right, Context& context,
                           size_t rightOffset)
    : index_{index},
      left_{std::move(left)},
      rightPattern_{std::move(right)},
      context_{context},
      firstRightOffset_{rightOffset} {}

void JoinIterator::openRight(size_t offset) {
  right_ = std::make_unique<ScanIterator>(index_, rightPattern_,
                                          leftSolution_.bindings, offset,
                                          context_);
}

bool JoinIterator::hasNext() {
  while (true) {
    // The inner scan first: a nested loop finishes what it is in before
    // advancing the outer one.
    if (right_ != nullptr && right_->hasNext()) { return true; }
    // The inner scan is spent, so take the outer one further. This is where a
    // `PauseException` from the outer scan passes through, unchanged.
    if (!left_->hasNext()) { return false; }
    leftSolution_ = left_->next();
    // Only the first inner scan resumes at an offset; the rest start over,
    // which is exactly what the second branch of `pause()` encodes.
    openRight(started_ ? 0 : firstRightOffset_);
    started_ = true;
  }
}

Solution JoinIterator::next() {
  Solution solution = right_->next();
  // The inner scan already carries the outer bindings it was given, since
  // those are variables it shares with the outer pattern. What is left to add
  // are the outer variables the inner pattern does not mention, which would
  // otherwise be dropped from the solution.
  for (const Binding& outer : leftSolution_.bindings) {
    bool already = false;
    for (const Binding& mine : solution.bindings) {
      if (mine.variable == outer.variable) { already = true; break; }
    }
    if (!already) { solution.bindings.push_back(outer); }
  }
  return solution;
}

Cont JoinIterator::pause() {
  // (i) The inner scan where it stands, carrying the outer bindings that made
  // it. `ScanIterator::pause` answers `Done` when it is exhausted.
  Cont inner = right_ == nullptr ? done() : right_->pause();
  // (ii) The outer scan taken further, with the inner pattern entire.
  Cont outer = left_->pause();
  if (isDone(outer)) { return inner; }
  return unite(inner, join(outer, pattern(rightPattern_)));
}

}  // namespace passage
