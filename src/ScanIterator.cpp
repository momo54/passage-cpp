#include "ScanIterator.h"

namespace passage {
namespace {

using Component = qlever::ScanCursor::Component;

// `std::nullopt` is what `ScanCursor` means by "any".
std::optional<TripleComponent> boundOrAny(const TripleComponent& c) {
  return c.isVariable() ? std::nullopt : std::optional{c};
}

}  // namespace

ScanIterator::ScanIterator(
    const Index& index, SparqlTripleSimple triple, size_t offset,
    Context& context,
    std::vector<std::pair<std::string, std::string>> inputBindings)
    : triple_{std::move(triple)},
      context_{context},
      inputBindings_{std::move(inputBindings)} {
  // The variables in the order the pattern writes them. A pattern may repeat
  // one (`?x ?p ?x`); the first occurrence is the one reported.
  auto note = [this](const TripleComponent& c, Component which) {
    if (!c.isVariable()) { return; }
    const std::string& name = c.getVariable().name();
    for (const auto& [_, already] : projected_) {
      if (already == name) { return; }
    }
    projected_.emplace_back(which, name);
  };
  note(triple_.s_, Component::Subject);
  note(triple_.p_, Component::Predicate);
  note(triple_.o_, Component::Object);

  cursor_ = std::make_unique<qlever::ScanCursor>(
      index.getImpl(), boundOrAny(triple_.s_), boundOrAny(triple_.p_),
      boundOrAny(triple_.o_),
      index.deltaTriplesManager().getCurrentLocatedTriplesSharedState(),
      ql::span<const Component>{});
  // Where the continuation said to resume. Not reading what it skips over is
  // the fork's patch to QLever, and the reason a deep offset is affordable.
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
  for (const auto& [which, name] : projected_) {
    solution.bindings.emplace_back(name, cursor_->getString(which));
  }
  cursor_->next();
  ++context_.produced;
  return solution;
}

Cont ScanIterator::pause() {
  // Nothing left means nothing to continue, which is what ends the sequence.
  if (!cursor_->hasNext()) { return done(); }
  // `current()` is the number of matches consumed, hence the offset to resume
  // at: the same quantity `PassageScan` reads from `wrapped.current()`.
  return slice(bindings(inputBindings_, pattern(triple_)), cursor_->current());
}

}  // namespace passage
