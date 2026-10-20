// What the kernel promises, pinned. Each test builds its own index from
// Turtle, so nothing here depends on a fixture that may or may not be on the
// machine: a test that cannot run is a test that says nothing.
#include <gmock/gmock.h>

#include <filesystem>
#include <string>
#include <vector>

#include "../src/Continuation.h"
#include "util/GTestHelpers.h"
#include "util/IndexTestHelpers.h"

using namespace passage;
using ::testing::HasSubstr;

namespace {

// `makeTestIndex(turtle)` names the index after the running test and writes it
// to the working directory, which is how 144 files of index once ended up
// committed. Give it a basename under a temporary directory instead.
Index indexFrom(std::string turtle) {
  auto dir = std::filesystem::temp_directory_path() / "passage-cpp-tests";
  std::filesystem::create_directories(dir);
  auto basename = dir / gtestCurrentTestName();
  return ad_utility::testing::makeTestIndex(basename.string(),
                                            std::move(turtle));
}

// Four subjects on one predicate, plus two triples on another, so that a
// pattern can be bound on each component and still have several matches.
std::string testGraph() {
  return "<s0> <p> <o0> . <s1> <p> <o1> . <s2> <p> <o2> . <s3> <p> <o3> . "
         "<s0> <q> <o0> . <s1> <q> <o1> .";
}

std::string all() { return "SELECT * WHERE { ?s ?p ?o }"; }

// The graph of Figure 2 of the Passage paper, and the join of Example 3.10.
std::string figure2() {
  return "<http://ex.org/H1> <http://ex.org/type> <http://ex.org/Human> . "
         "<http://ex.org/H2> <http://ex.org/type> <http://ex.org/Human> . "
         "<http://ex.org/H3> <http://ex.org/type> <http://ex.org/Human> . "
         "<http://ex.org/H1> <http://ex.org/occupation> <http://ex.org/O1> . "
         "<http://ex.org/H1> <http://ex.org/occupation> <http://ex.org/O2> . "
         "<http://ex.org/H3> <http://ex.org/occupation> <http://ex.org/O3> .";
}

std::string join2() {
  return "SELECT * WHERE { ?human <http://ex.org/type> <http://ex.org/Human> . "
         "?human <http://ex.org/occupation> ?occupation }";
}

std::vector<std::string> flatten(const Index& index,
                                 const std::vector<Solution>& solutions) {
  std::vector<std::string> flat;
  for (const auto& solution : solutions) {
    std::string row;
    for (const auto& b : solution.bindings) {
      row += b.variable + "=" + asText(index, b.term) + " ";
    }
    flat.push_back(row);
  }
  return flat;
}

// Every solution of `query`, by following the continuations until none comes
// back. Returns the solutions and how many evaluations it took, which is the
// sequence Passage would run.
std::pair<std::vector<std::string>, size_t> follow(const Index& index,
                                                   std::string query,
                                                   size_t quota) {
  std::vector<std::string> flat;
  size_t evaluations = 0;
  while (true) {
    Outcome outcome = evaluate(index, query, Quota{quota});
    ++evaluations;
    for (const auto& solution : outcome.solutions) {
      std::string row;
      for (const auto& b : solution.bindings) {
        row += b.variable + "=" + asText(index, b.term) + " ";
      }
      flat.push_back(row);
    }
    if (!outcome.continuation.has_value()) { break; }
    query = *outcome.continuation;
    // `ASSERT_*` may only be used where the function returns void, so failing
    // here is reported and then broken out of by hand. Without this a kernel
    // that stopped making progress would hang the suite instead of failing it.
    if (evaluations >= 1000) {
      ADD_FAILURE() << "the sequence is not terminating";
      break;
    }
  }
  return {flat, evaluations};
}

}  // namespace

// A quota the pattern does not reach means the answer is complete, and the
// absence of a continuation is what says so.
TEST(Continuation, aPatternSmallerThanTheQuotaNeedsNoContinuation) {
  Index index = indexFrom(testGraph());
  Outcome outcome = evaluate(index, all(), Quota{100});
  EXPECT_EQ(outcome.solutions.size(), 6u);
  EXPECT_FALSE(outcome.continuation.has_value());
  EXPECT_EQ(outcome.startedAt, 0u);
}

// A quota the pattern exceeds means a continuation, and it has to name the
// offset to resume at rather than anything else.
TEST(Continuation, aQuotaSplitsThePatternAndNamesWhereToResume) {
  Index index = indexFrom(testGraph());
  Outcome outcome = evaluate(index, all(), Quota{2});
  EXPECT_EQ(outcome.solutions.size(), 2u);
  ASSERT_TRUE(outcome.continuation.has_value());
  EXPECT_THAT(*outcome.continuation, HasSubstr("OFFSET 2"));

  Outcome second = evaluate(index, *outcome.continuation, Quota{2});
  EXPECT_EQ(second.startedAt, 2u);
  EXPECT_EQ(second.solutions.size(), 2u);
  ASSERT_TRUE(second.continuation.has_value());
  EXPECT_THAT(*second.continuation, HasSubstr("OFFSET 4"));
}

// The property the whole design rests on: the sequence of continuations
// enumerates the pattern exactly once. Not "about once" — the same solutions,
// in the same order, as one evaluation that was never interrupted.
TEST(Continuation, theWholeSequenceEnumeratesThePatternExactlyOnce) {
  Index index = indexFrom(testGraph());
  auto [uninterrupted, once] = follow(index, all(), 1000);
  ASSERT_EQ(once, 1u) << "a quota of 1000 should not have paused at all";

  for (size_t quota : {size_t{1}, size_t{2}, size_t{3}, size_t{5}}) {
    auto [pieces, evaluations] = follow(index, all(), quota);
    EXPECT_EQ(pieces, uninterrupted)
        << "with a quota of " << quota << ", in " << evaluations
        << " evaluations";
    // Ceil division, plus the final evaluation that finds nothing left when
    // the count divides exactly.
    EXPECT_GE(evaluations, (uninterrupted.size() + quota - 1) / quota);
  }
}

// The boundary: a quota spent exactly on the last solution yields no
// continuation, because there is nothing left to continue.
//
// NOTE: this does *not* pin the order of the two conditions in
// `ScanIterator::hasNext`, which was the claim this comment made first.
// Checked by reversing them: the test still passes, because `pause()` answers
// `Done` on an exhausted cursor and so catches the case anyway. The ordering
// is defensive — it avoids throwing on the common path where a scan has just
// finished — and the observable behaviour is the same either way.
TEST(Continuation, aQuotaSpentExactlyOnTheLastSolutionIsNotAPause) {
  Index index = indexFrom(testGraph());
  Outcome outcome = evaluate(index, all(), Quota{6});
  EXPECT_EQ(outcome.solutions.size(), 6u);
  EXPECT_FALSE(outcome.continuation.has_value())
      << "the scan was exhausted, so there is nothing to continue";

  // One short, and it is a pause.
  Outcome short_ = evaluate(index, all(), Quota{5});
  EXPECT_EQ(short_.solutions.size(), 5u);
  EXPECT_TRUE(short_.continuation.has_value());
}

// The property, for a join: the sequence of continuations enumerates the join
// exactly once, whatever the quota. This is what closes the loop — the kernel
// reads back the union of a subquery and a `VALUES` that it wrote itself.
//
// It compares SOLUTIONS and not counts, which matters: the first version of
// this compared counts, and a resumed inner branch that had lost `?human`
// from every solution still counted three.
TEST(Continuation, theSequenceEnumeratesAJoinExactlyOnce) {
  Index index = indexFrom(figure2());
  auto [uninterrupted, once] = follow(index, join2(), 1000);
  ASSERT_EQ(once, 1u);
  ASSERT_EQ(uninterrupted.size(), 3u);
  for (const std::string& row : uninterrupted) {
    EXPECT_THAT(row, HasSubstr("?human=")) << "a solution lost a variable";
    EXPECT_THAT(row, HasSubstr("?occupation="));
  }

  for (size_t quota : {size_t{1}, size_t{2}, size_t{3}, size_t{5}}) {
    auto [pieces, evaluations] = follow(index, join2(), quota);
    EXPECT_EQ(pieces, uninterrupted)
        << "with a quota of " << quota << ", in " << evaluations
        << " evaluations";
  }
}

// Data chosen so that a pause lands INSIDE the left branch of a resumed
// union, with solutions still left in it. Figure 2 never reaches that path:
// its inner scan has at most one match left when a continuation resumes, so
// the pause always happens in the right branch instead, and a union that
// forgot to re-emit its unstarted branch went undetected.
//
// Here `<b>` has three objects, so resuming at offset 1 leaves two, and a
// quota of one pauses with the left branch live and the right not yet started.
TEST(Continuation, aPauseInsideTheLeftBranchKeepsTheRightOne) {
  Index index = indexFrom(
      "<a> <p> <b> . <a2> <p> <b2> . "
      "<b> <q> <1> . <b> <q> <2> . <b> <q> <3> . <b2> <q> <9> .");
  std::string q = "SELECT * WHERE { ?s <p> ?m . ?m <q> ?n }";

  auto [whole, once] = follow(index, q, 1000);
  ASSERT_EQ(once, 1u);
  ASSERT_EQ(whole.size(), 4u) << "three objects for <b>, one for <b2>";
  // Every solution carries all three variables, including `?s`, which the
  // inner pattern does not mention and which therefore only survives if the
  // continuation saves it.
  for (const std::string& row : whole) {
    EXPECT_THAT(row, HasSubstr("?s="));
    EXPECT_THAT(row, HasSubstr("?m="));
    EXPECT_THAT(row, HasSubstr("?n="));
  }

  for (size_t quota : {size_t{1}, size_t{2}, size_t{3}, size_t{4}}) {
    auto [pieces, evaluations] = follow(index, q, quota);
    EXPECT_EQ(pieces, whole) << "quota " << quota << ", " << evaluations
                             << " evaluations";
  }
}

// A join of three patterns, so that the nesting is two deep and a paused
// continuation holds a union inside a union.
TEST(Continuation, aThreePatternJoinAlsoRoundTrips) {
  Index index = indexFrom(
      "<a> <p> <b> . <a> <p> <c> . <b> <q> <x> . <c> <q> <y> . "
      "<x> <r> <1> . <y> <r> <2> .");
  std::string q =
      "SELECT * WHERE { ?s <p> ?m . ?m <q> ?n . ?n <r> ?o }";
  auto [whole, once] = follow(index, q, 1000);
  ASSERT_EQ(once, 1u);
  ASSERT_EQ(whole.size(), 2u);
  for (size_t quota : {size_t{1}, size_t{2}, size_t{3}}) {
    auto [pieces, evaluations] = follow(index, q, quota);
    EXPECT_EQ(pieces, whole) << "quota " << quota << ", " << evaluations
                             << " evaluations";
  }
}

// All four shapes, since the permutation the cursor picks differs for each and
// the offsets have to mean the same thing in all of them.
TEST(Continuation, everyShapeOfTriplePatternIsContinued) {
  Index index = indexFrom(testGraph());
  struct Case { std::string query; size_t solutions; };
  std::vector<Case> cases{
      {"SELECT * WHERE { ?s ?p ?o }", 6},
      {"SELECT * WHERE { ?s <p> ?o }", 4},
      {"SELECT * WHERE { <s0> ?p ?o }", 2},
      {"SELECT * WHERE { <s0> <p> ?o }", 1},
      {"SELECT * WHERE { <s0> <p> <o0> }", 1},
  };
  for (const auto& c : cases) {
    auto [pieces, evaluations] = follow(index, c.query, 1);
    EXPECT_EQ(pieces.size(), c.solutions) << c.query;
    auto [whole, once] = follow(index, c.query, 1000);
    EXPECT_EQ(pieces, whole) << c.query;
  }
}

// A two-pattern BGP: the nested loop QLever has no operator for, and the join
// Example 3.10 of the Passage paper computes. H1 has two occupations, H3 one,
// H2 none.
TEST(Continuation, aNestedLoopEnumeratesTheJoin) {
  Index index = indexFrom(figure2());
  Outcome outcome = evaluate(index, join2(), Quota{100});
  ASSERT_EQ(outcome.solutions.size(), 3u);
  EXPECT_FALSE(outcome.continuation.has_value());
  EXPECT_EQ(flatten(index, outcome.solutions),
            (std::vector<std::string>{
                "?human=<http://ex.org/H1> ?occupation=<http://ex.org/O1> ",
                "?human=<http://ex.org/H1> ?occupation=<http://ex.org/O2> ",
                "?human=<http://ex.org/H3> ?occupation=<http://ex.org/O3> "}));
}

// The progress condition, which a join is the first thing to threaten: an
// evaluation must emit at least one solution, or the next one is identical and
// the sequence never terminates.
//
// It failed here first. `ScanIterator::next` counted every match against the
// quota, so the outer scan's intermediate bindings spent it: a quota of one
// returned ZERO solutions and paused. With a single pattern each match is a
// solution, so nothing showed it.
TEST(Continuation, aJoinMakesProgressRatherThanSpendingTheQuotaOnIntermediates) {
  Index index = indexFrom(figure2());
  for (size_t quota : {size_t{1}, size_t{2}, size_t{3}}) {
    Outcome outcome = evaluate(index, join2(), Quota{quota});
    EXPECT_GE(outcome.solutions.size(), 1u)
        << "quota " << quota << " produced nothing, so it cannot terminate";
  }
}

// The paused state of a nested loop is Example 3.10: finish the inner scan
// where it stands, then take the outer one further with the inner pattern
// entire. Checked as text, because this is what a client receives.
TEST(Continuation, aPausedNestedLoopIsExampleThreeTen) {
  Index index = indexFrom(figure2());
  Outcome outcome = evaluate(index, join2(), Quota{1});
  ASSERT_TRUE(outcome.continuation.has_value());
  EXPECT_EQ(*outcome.continuation,
            "SELECT * WHERE { { { SELECT * WHERE { "
            "BIND(<http://ex.org/H1> AS ?human) "
            "?human <http://ex.org/occupation> ?occupation . } OFFSET 1 } }"
            " UNION { { SELECT * WHERE { "
            "?human <http://ex.org/type> <http://ex.org/Human> . } OFFSET 1 }"
            " ?human <http://ex.org/occupation> ?occupation . } }");
}

// A pattern whose subject does not occur in the vocabulary has no solutions,
// and no continuation. This is the trap the Java backend fell into: an unknown
// value became a wildcard and matched everything.
TEST(Continuation, anAbsentValueMatchesNothingRatherThanEverything) {
  Index index = indexFrom(testGraph());
  Outcome outcome =
      evaluate(index, "SELECT * WHERE { <nowhere> ?p ?o }", Quota{100});
  EXPECT_TRUE(outcome.solutions.empty());
  EXPECT_FALSE(outcome.continuation.has_value());
}

// What is outside the fragment is refused by name. A kernel that silently
// ignored a FILTER would answer something that is not the question.
TEST(Continuation, whatIsOutsideTheFragmentIsRefusedByName) {
  Index index = indexFrom(testGraph());
  auto why = [&index](const std::string& query, size_t quota = 10) {
    try {
      evaluate(index, query, Quota{quota});
      return std::string{"no exception"};
    } catch (const std::exception& e) {
      return std::string{e.what()};
    }
  };
  // The fragment is now what a continuation is made of, so a BGP and a UNION
  // of two groups are inside it: the kernel has to read what it writes.
  EXPECT_EQ(why("SELECT * WHERE { ?s <p> ?o . ?o <q> ?x }"), "no exception");
  EXPECT_EQ(why("SELECT * WHERE { { ?s <p> ?o } UNION { ?s <q> ?x } }"),
            "no exception");
  EXPECT_EQ(why("SELECT * WHERE { VALUES ?s { <a> } ?s <p> ?o }"),
            "no exception");
  // What is outside it is still refused, by what it is.
  EXPECT_THAT(why("SELECT * WHERE { ?s <p> ?o OPTIONAL { ?s <q> ?x } }"),
              HasSubstr("not one of their parts"));
  EXPECT_THAT(why("SELECT * WHERE { GRAPH <g> { ?s <p> ?o } }"),
              HasSubstr("GRAPH"));
  EXPECT_THAT(why("SELECT * WHERE { VALUES (?s ?o) { (<a> <b>) } ?s <p> ?o }"),
              HasSubstr("VALUES clause of 2 variables"));
  // An OFFSET over a join would be an offset into its solutions, which a
  // continuation never writes: it writes a union of two offsets instead.
  EXPECT_THAT(why("SELECT * WHERE { ?s <p> ?o . ?o <q> ?x } OFFSET 2"),
              HasSubstr("OFFSET over a basic graph pattern"));
  EXPECT_THAT(why("SELECT * WHERE { ?s ?p ?o } LIMIT 3"), HasSubstr("LIMIT"));
  EXPECT_THAT(why(all(), 0), HasSubstr("never makes progress"));
}
