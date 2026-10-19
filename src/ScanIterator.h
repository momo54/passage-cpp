// The scan operator: one triple pattern, over a `qlever::ScanCursor`.
//
// The counterpart of Passage's `PassageScan`, and the only operator this
// kernel has. `pause()` reproduces its shape:
//
//   OpSlice(OpJoin(input.asBindAs(), op), wrapped.current(), limit)
//
// that is, the bindings that led here, the pattern *unmoved*, and an offset
// read off the cursor. The pattern is not rewritten, which is what makes the
// continuation a query the engine can read back rather than an encoding.
#ifndef PASSAGE_SCAN_ITERATOR_H
#define PASSAGE_SCAN_ITERATOR_H

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "Iterator.h"
#include "index/Index.h"
#include "libqlever/ScanCursor.h"
#include "parser/SparqlTriple.h"

namespace passage {

class ScanIterator : public Iterator {
 public:
  // `inputBindings` are the bindings of the loops above this scan, which a
  // continuation has to carry so that the resumed query produces the same
  // solutions. Empty at the root, which is the only case today.
  ScanIterator(const Index& index, SparqlTripleSimple triple, size_t offset,
               Context& context,
               std::vector<std::pair<std::string, std::string>> inputBindings =
                   {});

  bool hasNext() override;
  Solution next() override;
  Cont pause() override;

 private:
  SparqlTripleSimple triple_;
  Context& context_;
  std::vector<std::pair<std::string, std::string>> inputBindings_;
  // The variables to report, and which component of the match each reads.
  std::vector<std::pair<qlever::ScanCursor::Component, std::string>> projected_;
  std::unique_ptr<qlever::ScanCursor> cursor_;
};

}  // namespace passage

#endif
