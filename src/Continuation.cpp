#include "Continuation.h"

#include <sstream>
#include <stdexcept>
#include <variant>

#include "libqlever/ScanCursor.h"
#include "parser/GraphPatternOperation.h"
#include "parser/ParsedQuery.h"
#include "parser/SparqlParser.h"
#include "parser/SparqlTriple.h"
#include "parser/TripleComponent.h"

namespace passage {
namespace {

using Component = qlever::ScanCursor::Component;

// The one triple pattern of `query`, plus the offset it resumes from.
struct Pattern {
  SparqlTripleSimple triple;
  size_t offset = 0;
};

// Read the fragment this kernel handles out of a `ParsedQuery`, and refuse
// anything else by name rather than by silently ignoring it.
Pattern readPattern(const ParsedQuery& parsed) {
  const auto& patterns = parsed._rootGraphPattern._graphPatterns;
  if (patterns.size() != 1) {
    throw std::runtime_error(
        "this kernel handles one triple pattern; the query has " +
        std::to_string(patterns.size()) + " graph patterns");
  }
  const auto* bgp =
      std::get_if<parsedQuery::BasicGraphPattern>(&patterns.front());
  if (bgp == nullptr) {
    throw std::runtime_error(
        "this kernel handles one triple pattern; the query's body is some "
        "other graph pattern (OPTIONAL, UNION, SERVICE, a subquery, ...)");
  }
  if (bgp->_triples.size() != 1) {
    throw std::runtime_error(
        "this kernel handles one triple pattern; this basic graph pattern has " +
        std::to_string(bgp->_triples.size()) +
        ". A BGP needs a nested loop over several cursors, which is the next "
        "step and not this one");
  }
  if (parsed._limitOffset._limit.has_value()) {
    throw std::runtime_error(
        "a LIMIT in the query would be a second quota next to the kernel's "
        "own; pass the quota to the kernel instead");
  }
  return Pattern{bgp->_triples.front().getSimple(),
                 parsed._limitOffset._offset};
}

// `std::nullopt` for a variable, the component itself for a constant, which is
// what `ScanCursor` means by "any".
std::optional<TripleComponent> boundOrAny(const TripleComponent& c) {
  return c.isVariable() ? std::nullopt : std::optional{c};
}

// The pattern as SPARQL again, so that the continuation is a query and not a
// private encoding of one. `TripleComponent::toString` already renders both a
// variable and an IRI the way a query writes them.
std::string asSparql(const SparqlTripleSimple& t, size_t offset) {
  std::ostringstream out;
  out << "SELECT * WHERE { " << t.s_.toString() << ' ' << t.p_.toString()
      << ' ' << t.o_.toString() << " } OFFSET " << offset;
  return out.str();
}

}  // namespace

Outcome evaluate(const Index& index, const std::string& query,
                 const Quota& quota) {
  if (quota.maxSolutions == 0) {
    throw std::runtime_error(
        "a quota of zero solutions never makes progress, so the sequence "
        "would not terminate");
  }
  Pattern pattern = readPattern(SparqlParser::parseQuery(query));

  // The variables to report, in the order the pattern writes them. A pattern
  // may repeat one (`?x ?p ?x`); the first occurrence is the one reported.
  std::vector<std::pair<Component, std::string>> projected;
  auto note = [&projected](const TripleComponent& c, Component which) {
    if (!c.isVariable()) { return; }
    const std::string& name = c.getVariable().name();
    for (const auto& [_, already] : projected) {
      if (already == name) { return; }
    }
    projected.emplace_back(which, name);
  };
  note(pattern.triple.s_, Component::Subject);
  note(pattern.triple.p_, Component::Predicate);
  note(pattern.triple.o_, Component::Object);

  auto locatedTriples =
      index.deltaTriplesManager().getCurrentLocatedTriplesSharedState();
  qlever::ScanCursor cursor{index.getImpl(),
                            boundOrAny(pattern.triple.s_),
                            boundOrAny(pattern.triple.p_),
                            boundOrAny(pattern.triple.o_),
                            locatedTriples,
                            {}};
  // Where the continuation said to resume. Not reading what it skips over is
  // the whole reason this is affordable at depth.
  if (pattern.offset > 0) {
    cursor.skip(pattern.offset);
  }

  Outcome outcome;
  outcome.startedAt = pattern.offset;
  while (cursor.hasNext() && outcome.solutions.size() < quota.maxSolutions) {
    Solution solution;
    for (const auto& [which, name] : projected) {
      solution.bindings.emplace_back(name, cursor.getString(which));
    }
    outcome.solutions.push_back(std::move(solution));
    cursor.next();
  }

  // `current()` is the number of matches consumed, which is the offset the
  // next evaluation resumes from. A cursor that still has a match is what says
  // a continuation is needed; one that does not ends the sequence.
  if (cursor.hasNext()) {
    outcome.continuation = asSparql(pattern.triple, cursor.current());
  }
  return outcome;
}

}  // namespace passage
