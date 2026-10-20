#include "Iterator.h"

#include "libqlever/ScanCursor.h"
#include "parser/TripleComponent.h"
#include "index/ExportIds.h"

namespace passage {

std::string asText(const Index& index, const TripleComponent& term) {
  // A term the parser gave already knows how to write itself.
  if (!term.isId()) { return term.toString(); }
  // One a scan produced is an `Id`, which needs the vocabulary. The same path
  // `ScanCursor::getString` takes.
  static const LocalVocab emptyLocalVocab{};
  auto literalOrIri =
      ql::exportIds::idToLiteralOrIri(index.getImpl(), term.getId(),
                                      emptyLocalVocab);
  if (!literalOrIri.has_value()) { return {}; }
  return std::string{std::move(literalOrIri).value().toStringRepresentation()};
}

}  // namespace passage
