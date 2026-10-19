#include "Continuation.h"

#include <memory>
#include <stdexcept>
#include <variant>

#include "Algebra.h"
#include "Iterator.h"
#include "JoinIterator.h"
#include "ScanIterator.h"
#include "parser/GraphPatternOperation.h"
#include "parser/ParsedQuery.h"
#include "parser/SparqlParser.h"
#include "parser/SparqlTriple.h"
#include "parser/TripleComponent.h"

namespace passage {
namespace {

// The patterns of a query's basic graph pattern, plus the offset it resumes
// from. More than one makes a nested loop.
struct Plan {
  std::vector<SparqlTripleSimple> triples;
  size_t offset = 0;
};

// Read the fragment this kernel handles, and refuse anything else by name
// rather than ignoring it silently.
Plan plan(const ParsedQuery& parsed) {
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
  if (bgp->_triples.empty()) {
    throw std::runtime_error("the query's body has no triple pattern");
  }
  if (parsed._limitOffset._limit.has_value()) {
    throw std::runtime_error(
        "a LIMIT in the query would be a second quota beside the kernel's own; "
        "pass the quota to the kernel instead");
  }
  std::vector<SparqlTripleSimple> triples;
  for (const auto& triple : bgp->_triples) {
    triples.push_back(triple.getSimple());
  }
  return Plan{std::move(triples), parsed._limitOffset._offset};
}

}  // namespace

Outcome evaluate(const Index& index, const std::string& query,
                 const Quota& quota) {
  if (quota.maxSolutions == 0) {
    throw std::runtime_error(
        "a quota of zero solutions never makes progress, so the sequence "
        "would not terminate");
  }
  // The parser needs the index's encoded-IRI manager, since some IRIs are
  // encoded into the `Id` itself rather than put in the vocabulary.
  Plan p = plan(SparqlParser::parseQuery(&index.encodedIriManager(), query));

  Context context{quota.maxSolutions, 0, false};
  // The operator tree: a left-deep chain of nested loops, one scan per
  // pattern. The query's `OFFSET` belongs to the first scan, which is the
  // outermost loop; a continuation that resumes an inner loop carries its
  // offset in a subquery instead, which is why the chain below gives the
  // offset to the leftmost scan and zero to the others.
  std::unique_ptr<Iterator> root = std::make_unique<ScanIterator>(
      index, p.triples.front(), std::vector<Binding>{}, p.offset, context);
  for (size_t i = 1; i < p.triples.size(); ++i) {
    root = std::make_unique<JoinIterator>(index, std::move(root),
                                          p.triples[i], context);
  }

  Outcome outcome;
  outcome.startedAt = p.offset;
  Cont remaining = done();
  try {
    while (root->hasNext()) {
      outcome.solutions.push_back(root->next());
      // The quota counts solutions, which is what the progress condition is
      // about: an evaluation must emit at least one, or the sequence does not
      // terminate. Counting a scan's matches instead lets a join spend the
      // quota on intermediate bindings.
      ++context.produced;
    }
    // Ran to exhaustion inside the quota, so there is nothing to continue.
    // `pause()` would answer the same; asking it would be a second scan of
    // the cursor's state for no reason.
  } catch (const PauseException&) {
    // The stop unwound to here. Now ask the tree what it has left — this is
    // where a join would contribute one state per branch.
    remaining = root->pause();
  }

  if (!isDone(remaining)) {
    outcome.continuation = asSparql(remaining);
  }
  return outcome;
}

}  // namespace passage
