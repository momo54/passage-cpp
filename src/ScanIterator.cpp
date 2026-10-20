#include "ScanIterator.h"

#include <algorithm>

namespace passage {
namespace {

using Component = qlever::ScanCursor::Component;

// The term the loops above, or a `VALUES` clause, fixed for this component.
std::optional<TripleComponent> boundTerm(const TripleComponent& c,
                                         const std::vector<Binding>& bound) {
  if (!c.isVariable()) { return std::nullopt; }
  const std::string& name = c.getVariable().name();
  auto it = std::find_if(bound.begin(), bound.end(),
                         [&name](const Binding& b) { return b.variable == name; });
  return it == bound.end() ? std::nullopt : std::optional{it->term};
}

}  // namespace

ScanIterator::ScanIterator(const Index& index, SparqlTripleSimple pattern,
                           std::vector<Binding> bound, size_t offset,
                           Context& context)
    : pattern_{std::move(pattern)},
      bound_{std::move(bound)},
      context_{context} {
  // What the scan should fix for each component: a variable a loop above has
  // bound becomes that `Id`, a variable nobody bound stays open, and a
  // constant of the query is left as written so that QLever resolves it —
  // including the case of a constant absent from the vocabulary, where the
  // cursor is immediately exhausted. That is the right answer, and the trap
  // the Java backend once fell into by turning it into a wildcard.
  //
  // `TripleComponent` can hold an `Id`, which is what makes substituting a
  // loop's binding free: no text is parsed and no vocabulary is consulted.
  auto resolve =
      [this](const TripleComponent& c) -> std::optional<TripleComponent> {
    if (auto fixed = boundTerm(c, bound_)) { return fixed; }
    if (c.isVariable()) { return std::nullopt; }
    return c;
  };
  // The variables this scan produces are those a loop above has not fixed.
  auto note = [this](const TripleComponent& c, Component which) {
    if (!c.isVariable()) { return; }
    const std::string& name = c.getVariable().name();
    if (boundTerm(c, bound_).has_value()) { return; }
    for (const auto& [_, already] : projected_) {
      if (already == name) { return; }
    }
    projected_.emplace_back(which, name);
  };
  note(pattern_.s_, Component::Subject);
  note(pattern_.p_, Component::Predicate);
  note(pattern_.o_, Component::Object);

  cursor_ = std::make_unique<qlever::ScanCursor>(
      index.getImpl(), resolve(pattern_.s_), resolve(pattern_.p_),
      resolve(pattern_.o_),
      index.deltaTriplesManager().getCurrentLocatedTriplesSharedState(),
      ql::span<const Component>{});
  if (offset > 0) {
    cursor_->skip(offset);
  }
}

bool ScanIterator::hasNext() {
  bool available = cursor_->hasNext();
  // Passage's rule, in its order: only pause when there is something left to
  // continue, and only once.
  //
  // The `available` test is defensive rather than load-bearing: `pause()`
  // answers `Done` on an exhausted cursor, so dropping it changes no
  // observable behaviour, only whether an exception is thrown on the common
  // path where a scan has just finished. Verified by reversing the two and
  // watching the tests stay green.
  if (available && !context_.paused && context_.shouldStop()) {
    context_.paused = true;
    throw PauseException{};
  }
  return available;
}

Solution ScanIterator::next() {
  Solution solution;
  // The variables fixed above this scan are part of the solution, not just of
  // the scan. Leaving them out lost `?human` from every solution of a resumed
  // inner branch, where a `VALUES` had bound it and no join sat above to put
  // it back — the count said three solutions and two carried `?human`.
  for (const Binding& b : bound_) {
    solution.bindings.push_back(b);
  }
  for (const auto& [which, name] : projected_) {
    solution.bindings.push_back(
        Binding{name, cursor_->getString(which),
                TripleComponent{cursor_->getId(which)}});
  }
  cursor_->next();
  // NOT `++context_.produced`. A scan's match is a solution only when the scan
  // is the whole tree; under a join, the outer scan's matches are intermediate
  // bindings. Counting them here spent the quota before the root emitted
  // anything, so a two-pattern BGP with a quota of one returned no solution at
  // all and paused — which breaks the progress condition the whole scheme
  // rests on. The root counts what it emits.
  return solution;
}

Cont ScanIterator::pause() {
  // Nothing left means nothing to continue, which is what ends the sequence.
  if (!cursor_->hasNext()) { return done(); }
  // The bindings of the loops above, as text, so that the resumed query fixes
  // them again. `current()` is the number of matches consumed, hence the
  // offset to resume at: `PassageScan` reads the same from `wrapped.current()`.
  std::vector<std::pair<std::string, std::string>> binds;
  for (const Binding& b : bound_) {
    binds.emplace_back(b.variable, b.text);
  }
  return slice(bindings(std::move(binds), pattern(pattern_)),
               cursor_->current());
}

}  // namespace passage
