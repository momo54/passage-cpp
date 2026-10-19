// The continuation algebra on its own.
//
// `evaluate` cannot reach `Bindings`, `Union` or a nested `Slice` while the
// fragment is a single triple pattern, so without these tests that part of
// `Algebra.cpp` would be code nobody has ever run. They are the shapes a BGP
// will pause into, and they are checked against what Passage prints.
#include <gmock/gmock.h>

#include "../src/Algebra.h"
#include "parser/TripleComponent.h"

using namespace passage;

namespace {

SparqlTripleSimple tp(std::string s, std::string p, std::string o) {
  auto term = [](const std::string& t) -> TripleComponent {
    if (t.starts_with('?')) { return TripleComponent{Variable{t}}; }
    return TripleComponent{TripleComponent::Iri::fromIriref(t)};
  };
  return SparqlTripleSimple{term(s), term(p), term(o)};
}

}  // namespace

// `DONE` is a value of the algebra, so that composing branches never has to
// test for null the way Passage's `pause()` returning `null` forces.
TEST(Algebra, doneIsAValueAndCollapsesUnderUnion) {
  EXPECT_TRUE(isDone(done()));
  EXPECT_TRUE(isDone(unite(done(), done())));

  Cont live = slice(pattern(tp("?s", "<p>", "?o")), 3);
  EXPECT_FALSE(isDone(live));
  // A finished branch beside a live one is just the live one. Without this a
  // BGP would accumulate empty `VALUES` clauses as its patterns run out.
  EXPECT_EQ(asSparql(unite(done(), live)), asSparql(live));
  EXPECT_EQ(asSparql(unite(live, done())), asSparql(live));
}

// At the root, a slice is the query's own `OFFSET`: a continuation of one
// triple pattern has to read as the original query resumed, not as a subquery
// wrapped in one, or it would not be the same query to a reader.
TEST(Algebra, aSliceAtTheRootIsTheQuerysOwnOffset) {
  EXPECT_EQ(asSparql(slice(pattern(tp("?s", "<p>", "?o")), 7)),
            "SELECT * WHERE { ?s <p> ?o . } OFFSET 7");
}

// Below the root it cannot be, since `OFFSET` applies to a query and not to a
// group, so it becomes a subquery. This is the shape a BGP's second pattern
// pauses into.
TEST(Algebra, aSliceBelowTheRootBecomesASubquery) {
  Cont inner = slice(pattern(tp("?s", "<q>", "?x")), 2);
  Cont outer = slice(unite(inner, pattern(tp("?s", "<p>", "?o"))), 5);
  EXPECT_EQ(asSparql(outer),
            "SELECT * WHERE { { { SELECT * WHERE { ?s <q> ?x . } OFFSET 2 } }"
            " UNION { ?s <p> ?o . } } OFFSET 5");
}

// The bindings that led to an iterator, which a nested loop has to carry so
// that the resumed query produces the same solutions. Passage writes them as
// `input.asBindAs()`.
TEST(Algebra, bindingsAreWrittenAsBind) {
  Cont c = bindings({{"?s", "<http://ex.org/H1>"}},
                    pattern(tp("?s", "<occupation>", "?o")));
  EXPECT_EQ(asSparql(c),
            "SELECT * WHERE { BIND(<http://ex.org/H1> AS ?s)"
            " ?s <occupation> ?o . }");
  // No bindings is not an empty BIND, it is the child itself.
  Cont bare = pattern(tp("?s", "<p>", "?o"));
  EXPECT_EQ(asSparql(bindings({}, bare)), asSparql(bare));
}

// Two live branches side by side.
TEST(Algebra, aUnionOfTwoLiveBranchesIsAUnion) {
  Cont c = unite(pattern(tp("?s", "<p>", "?o")), pattern(tp("?s", "<q>", "?x")));
  EXPECT_EQ(asSparql(c),
            "SELECT * WHERE { { ?s <p> ?o . } UNION { ?s <q> ?x . } }");
}
