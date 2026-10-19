// The Volcano interface, and the stopping mechanism around it.
//
// Modelled on Passage's: an iterator answers `hasNext`/`next`, and separately
// answers `pause()` with the algebra of the work it has not done. Two
// decisions are taken from there rather than invented:
//
//   * THE STOP IS A THROW. `hasNext()` consults the quota and, when it is
//     spent while a match is still available, throws `PauseException`. The
//     stack unwinds to the root, which then asks the tree what remains. This
//     is `PassageScan.hasNext` throwing `PauseException(op)` and `PassageRoot`
//     catching it. A return code would have to be threaded through every
//     operator and every caller of `hasNext`, and a join has several.
//
//   * NOTHING LEFT IS NOT A PAUSE. `pause()` answers `Done` when the iterator
//     is exhausted, and the absence of a continuation is what ends the
//     sequence. In Passage `pause()` returns `null` in that case and
//     `Pause2Continuation.DONE` is an empty table; here it is one value of the
//     algebra, so a parent composing two branches does not special-case null.
#ifndef PASSAGE_ITERATOR_H
#define PASSAGE_ITERATOR_H

#include <cstddef>
#include <exception>
#include <string>
#include <utility>
#include <vector>

#include "Algebra.h"

namespace passage {

// One solution: variables in the order the pattern writes them.
struct Solution {
  std::vector<std::pair<std::string, std::string>> bindings;
};

// What the engine is allowed to do before it has to pause, and what it has
// done so far. Shared by every iterator in the tree, as Passage shares its
// `PassageExecutionContext`.
struct Context {
  size_t maxSolutions = 1000;
  size_t produced = 0;
  // Set when the stop has already fired, so that unwinding through further
  // `hasNext()` calls does not throw a second time.
  bool paused = false;

  // Passage's `stopping.apply(context)`: here, the quota is spent.
  bool shouldStop() const { return produced >= maxSolutions; }
};

// Thrown by `hasNext()` when the quota is spent and work remains. Carries
// nothing: the root asks the tree for its state, rather than the exception
// carrying it, so that every operator contributes its own part.
class PauseException : public std::exception {
 public:
  const char* what() const noexcept override {
    return "the quota is spent and the execution paused";
  }
};

class Iterator {
 public:
  virtual ~Iterator() = default;

  // Whether a solution is available. May throw `PauseException`.
  virtual bool hasNext() = 0;

  // The next solution. Only valid after `hasNext()` answered true.
  virtual Solution next() = 0;

  // The work this iterator has not done, as continuation algebra, or `Done`
  // when it has none. Must not consume anything.
  virtual Cont pause() = 0;
};

}  // namespace passage

#endif
