#include "Continuation.h"

#include <memory>
#include <stdexcept>

#include "Algebra.h"
#include "Iterator.h"
#include "global/RuntimeParameters.h"
#include "Planner.h"
#include "parser/ParsedQuery.h"
#include "parser/SparqlParser.h"

namespace passage {

Outcome evaluate(const Index& index, const std::string& query,
                 const Quota& quota) {
  if (quota.maxSolutions == 0) {
    throw std::runtime_error(
        "a quota of zero solutions never makes progress, so the sequence "
        "would not terminate");
  }
  // The parser needs the index\'s encoded-IRI manager, since some IRIs are
  // encoded into the `Id` itself rather than put in the vocabulary.
  //
  // The `ParsedQuery` has to outlive the execution: a union keeps a reference
  // into it, so that the branch it has not run can be re-emitted as written.
  ParsedQuery parsed =
      SparqlParser::parseQuery(&index.encodedIriManager(), query);

  // A few decompressed blocks kept per thread, as `QLeverBackend` sets for the
  // Java engine. QLever leaves this off because its own engine merge-joins and
  // never returns to a block; a continuation engine nests loops, so it opens a
  // scan per outer solution and lands in the same block over and over. On
  // WatDiv 10M that was worth a factor of 58 to the Java backend, and this
  // kernel had been running without it — which would have made any
  // comparison between the two meaningless.
  setRuntimeParameter<&RuntimeParameters::lazyIndexScanBlockCacheSize_>(
      quota.blockCache);

  Context context{quota.maxSolutions, 0, false};
  std::unique_ptr<Iterator> root = buildTree(index, parsed, context);

  Outcome outcome;
  outcome.startedAt = parsed._limitOffset._offset;
  Cont remaining = done();
  try {
    while (root->hasNext()) {
      outcome.solutions.push_back(root->next());
      // The quota counts solutions, which is what the progress condition is
      // about: an evaluation must emit at least one, or the sequence does not
      // terminate. Counting a scan\'s matches instead lets a join spend the
      // quota on intermediate bindings.
      ++context.produced;
    }
    // Ran to exhaustion inside the quota, so there is nothing to continue.
  } catch (const PauseException&) {
    // The stop unwound to here. Now ask the tree what it has left; each
    // operator contributes its own part.
    remaining = root->pause();
  }

  if (!isDone(remaining)) {
    outcome.continuation = asSparql(remaining);
  }
  return outcome;
}

}  // namespace passage
