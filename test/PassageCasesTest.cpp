// Passage's own test cases, run against this kernel.
//
// The data and the expected answers are transcribed from
// `passage-volcano/src/test/java/fr/gdd/passage/volcano/executions/BGPTest.java`
// and `passage-commons/.../InMemoryStatements.java#triples9`. They matter
// because they were not computed here: Passage checks them against Blazegraph
// evaluating the same query, so they are an outside opinion on what these
// queries mean.
//
// Every case is also run through the continuation sequence at several quotas,
// which is the part Passage's own tests do by parameterising over execution
// contexts. A case that answers correctly in one evaluation and wrongly in
// three is the failure this kernel is most likely to have.
#include <gmock/gmock.h>

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

#include "../src/Continuation.h"
#include "util/GTestHelpers.h"
#include "util/IndexTestHelpers.h"

using namespace passage;
using ::testing::UnorderedElementsAreArray;

namespace {

// `InMemoryStatements.triples9`, verbatim: three addresses, Alice owning three
// animals, three species.
std::string triples9() {
  return "<http://Alice> <http://address> <http://nantes> . "
         "<http://Bob>   <http://address> <http://paris>  . "
         "<http://Carol> <http://address> <http://nantes> . "
         "<http://Alice> <http://own>     <http://cat> . "
         "<http://Alice> <http://own>     <http://dog> . "
         "<http://Alice> <http://own>     <http://snake> . "
         "<http://cat>   <http://species> <http://feline> . "
         "<http://dog>   <http://species> <http://canine> . "
         "<http://snake> <http://species> <http://reptile> .";
}

Index indexFrom(std::string turtle) {
  auto dir = std::filesystem::temp_directory_path() / "passage-cpp-tests";
  std::filesystem::create_directories(dir);
  auto basename = dir / gtestCurrentTestName();
  return ad_utility::testing::makeTestIndex(basename.string(),
                                            std::move(turtle));
}

// Every solution of `query`, by following the continuations, as sorted
// `?var=<term>` rows so that two runs can be compared whatever the order
// within a solution.
std::vector<std::string> follow(const Index& index, std::string query,
                                size_t quota) {
  std::vector<std::string> rows;
  size_t evaluations = 0;
  while (true) {
    Outcome outcome = evaluate(index, query, Quota{quota});
    ++evaluations;
    for (const auto& solution : outcome.solutions) {
      std::vector<std::string> parts;
      for (const auto& b : solution.bindings) {
        parts.push_back(b.variable + "=" + asText(index, b.term));
      }
      std::sort(parts.begin(), parts.end());
      std::string row;
      for (const auto& part : parts) { row += part + " "; }
      rows.push_back(row);
    }
    if (!outcome.continuation.has_value()) { break; }
    query = *outcome.continuation;
    if (evaluations >= 1000) {
      ADD_FAILURE() << "the sequence is not terminating";
      break;
    }
  }
  return rows;
}

// One case, run whole and then at every quota that can split it.
void check(const Index& index, const std::string& query,
           const std::vector<std::string>& expected) {
  std::vector<std::string> whole = follow(index, query, 1000);
  EXPECT_THAT(whole, UnorderedElementsAreArray(expected)) << query;
  for (size_t quota : {size_t{1}, size_t{2}, size_t{3}, size_t{5}}) {
    EXPECT_THAT(follow(index, query, quota),
                UnorderedElementsAreArray(expected))
        << "quota " << quota << ", " << query;
  }
}

}  // namespace

// `BGPTest.simple_triple_pattern`: three people have an address.
TEST(PassageCases, onePattern) {
  Index index = indexFrom(triples9());
  check(index, "SELECT * WHERE { ?p <http://address> ?c }",
        {"?c=<http://nantes> ?p=<http://Alice> ",
         "?c=<http://paris> ?p=<http://Bob> ",
         "?c=<http://nantes> ?p=<http://Carol> "});
}

// `BGPTest.unknown_predicate`: a predicate absent from the vocabulary matches
// nothing. The trap is answering *everything* instead, which the Java backend
// once did.
TEST(PassageCases, anUnknownPredicateMatchesNothing) {
  Index index = indexFrom(triples9());
  check(index, "SELECT * WHERE { ?p <http://does_not_exist> ?c }", {});
}

// `BGPTest.unknown_predicate_in_a_bgp`: and nothing again when it is one
// pattern of a join. Note `?c` occurs in both patterns, so the inner scan is
// bound on two components at once.
TEST(PassageCases, aJoinWithAnUnknownPredicateMatchesNothing) {
  Index index = indexFrom(triples9());
  check(index,
        "SELECT * WHERE { ?p <http://address> ?c . ?p <http://does_not_exist> ?c }",
        {});
}

// `BGPTest`, the two- and three-pattern joins: Alice owns three animals, each
// of a species.
TEST(PassageCases, aTwoPatternJoin) {
  Index index = indexFrom(triples9());
  check(index,
        "SELECT * WHERE { ?p <http://address> <http://nantes> . "
        "?p <http://own> ?a }",
        {"?a=<http://cat> ?p=<http://Alice> ",
         "?a=<http://dog> ?p=<http://Alice> ",
         "?a=<http://snake> ?p=<http://Alice> "});
}

TEST(PassageCases, aThreePatternJoin) {
  Index index = indexFrom(triples9());
  check(index,
        "SELECT * WHERE { ?p <http://address> <http://nantes> . "
        "?p <http://own> ?a . ?a <http://species> ?s }",
        {"?a=<http://cat> ?p=<http://Alice> ?s=<http://feline> ",
         "?a=<http://dog> ?p=<http://Alice> ?s=<http://canine> ",
         "?a=<http://snake> ?p=<http://Alice> ?s=<http://reptile> "});
}

// `BGPTest.offset_triple_pattern_return_no_match`, a continuation written by
// hand in Passage's suite: the second person from Nantes is Carol, and she
// owns nothing, so the join is empty. This is a continuation shape, read back.
TEST(PassageCases, aHandWrittenContinuationThatMatchesNothing) {
  Index index = indexFrom(triples9());
  check(index,
        "SELECT * WHERE { { { SELECT * WHERE { ?p <http://address> <http://nantes> } "
        "OFFSET 1 } ?p <http://own> ?a } ?a <http://species> ?s }",
        {});
}

// `BGPTest.issue_with_the_first_offset`, the other hand-written continuation.
//
// `BGPTest.issue_with_the_first_offset`, VERBATIM: the trailing
// `?a <species> ?s` sits *outside* the union, applying to both branches, which
// is how Passage's algebra factors a common suffix. Reading it needs a join
// whose left side is a union — nothing special to execute, since
// `JoinIterator` takes an iterator there, but the planner used to refuse it.
//
// Expected answer is Passage's: cat is already produced, so only dog and snake
// remain.
TEST(PassageCases, passagesOwnContinuationVerbatim) {
  Index index = indexFrom(triples9());
  check(index,
        "SELECT  * WHERE {   { {"
        "  SELECT  * WHERE {"
        "    BIND(<http://Alice> AS ?p)"
        "    ?p  <http://own>  ?a"
        " } OFFSET  1 } }"
        " UNION"
        " { { SELECT * WHERE { ?p  <http://address>  <http://nantes> } OFFSET  1 }"
        "        ?p  <http://own>  ?a }"
        "    ?a  <http://species>  ?s"
        " }",
        {"?a=<http://dog> ?p=<http://Alice> ?s=<http://canine> ",
         "?a=<http://snake> ?p=<http://Alice> ?s=<http://reptile> "});
}

// The same answer with the suffix distributed into each branch, which is what
// this kernel used to write. Both forms mean the same thing, and both are now
// read; only the factored one is written, because that is Passage's.
TEST(PassageCases, theSameContinuationWithTheSuffixDistributed) {
  Index index = indexFrom(triples9());
  check(index,
        "SELECT * WHERE { "
        "{ { SELECT * WHERE { BIND(<http://Alice> AS ?p) "
        "?p <http://own> ?a } OFFSET 1 } ?a <http://species> ?s } "
        "UNION "
        "{ { SELECT * WHERE { ?p <http://address> <http://nantes> } OFFSET 1 } "
        "?p <http://own> ?a . ?a <http://species> ?s } }",
        {"?a=<http://dog> ?p=<http://Alice> ?s=<http://canine> ",
         "?a=<http://snake> ?p=<http://Alice> ?s=<http://reptile> "});
}

// Both forms of a saved binding are read, so that a continuation written by
// Passage is readable here and the other way round. `BIND` is what the paper
// shows and what this kernel writes; `VALUES` says the same for one row and
// costs nothing to keep accepting.
TEST(PassageCases, aSavedBindingIsReadAsBindOrAsValues) {
  Index index = indexFrom(triples9());
  const std::vector<std::string> expected{
      "?a=<http://cat> ?p=<http://Alice> ",
      "?a=<http://dog> ?p=<http://Alice> ",
      "?a=<http://snake> ?p=<http://Alice> "};
  check(index,
        "SELECT * WHERE { BIND(<http://Alice> AS ?p) ?p <http://own> ?a }",
        expected);
  check(index,
        "SELECT * WHERE { VALUES ?p { <http://Alice> } ?p <http://own> ?a }",
        expected);
}

// A `BIND` of anything but a constant is outside the fragment, and refused by
// what it is rather than evaluated wrongly.
TEST(PassageCases, aComputedBindIsRefused) {
  Index index = indexFrom(triples9());
  try {
    evaluate(index,
             "SELECT * WHERE { ?p <http://own> ?a BIND(STR(?a) AS ?t) }",
             Quota{10});
    ADD_FAILURE() << "a computed BIND should be refused";
  } catch (const std::exception& e) {
    EXPECT_THAT(std::string{e.what()},
                ::testing::HasSubstr("BIND of something other than a constant"));
  }
}

// `BGPTest.a_literal_at_predicate_position`: a literal bound to the predicate
// matches nothing, since no triple has a literal predicate. Passage expects
// zero and so does this.
TEST(PassageCases, aLiteralAtPredicatePositionMatchesNothing) {
  Index index = indexFrom(triples9());
  check(index, "SELECT * WHERE { VALUES ?predicate { \"12\" } ?p ?predicate ?c }",
        {});
}

// The substance of `BGPTest.weird_string_in_query`, which cannot be run as
// written: it names a Blazegraph of DBpedia at an absolute path on one
// author's laptop. What it is about is a literal carrying quotes of its own,
// in object position, so that is tested here against data of our own.
TEST(PassageCases, aLiteralWithQuotesInIt) {
  Index index = indexFrom(
      "<http://ship> <http://motto> "
      "\"\\\"By Sea, Land, and Air We Prosper\\\"\" . "
      "<http://other> <http://motto> \"plain\" .");
  std::vector<std::string> rows =
      follow(index, "SELECT * WHERE { ?s ?p ?o }", 1000);
  ASSERT_EQ(rows.size(), 2u);
  // And the literal is matched when it is the constant of the pattern, which
  // is what the original asserts: a non-empty result.
  std::vector<std::string> matched = follow(
      index,
      "SELECT * WHERE { ?s ?p "
      "\"\\\"By Sea, Land, and Air We Prosper\\\"\" }",
      1000);
  EXPECT_EQ(matched.size(), 1u) << "the quoted literal should match its triple";
}

// Passage's `UnionTest`, the three live cases. A `UNION` written by hand was
// outside this fragment until the planner learned to read one, which it had to
// do because a paused join *is* a union. So these come for free — but "for
// free" is a claim, and these are what check it.

// `UnionTest.execute_a_simple_union`: three triples from each branch.
TEST(PassageCases, aSimpleUnion) {
  Index index = indexFrom(triples9());
  check(index,
        "SELECT * WHERE { { ?p <http://own> ?a } UNION "
        "{ ?p <http://address> ?a } }",
        {"?a=<http://cat> ?p=<http://Alice> ",
         "?a=<http://dog> ?p=<http://Alice> ",
         "?a=<http://snake> ?p=<http://Alice> ",
         "?a=<http://nantes> ?p=<http://Alice> ",
         "?a=<http://paris> ?p=<http://Bob> ",
         "?a=<http://nantes> ?p=<http://Carol> "});
}

// `UnionTest.union_with_a_bgp`: the right branch is a join, so this is a
// union over a scan and a nested loop. Six solutions, Alice six times —
// three from the left, and three more from the right, where only Alice both
// has an address and owns something.
TEST(PassageCases, aUnionWhoseBranchIsAJoin) {
  Index index = indexFrom(triples9());
  check(index,
        "SELECT * WHERE { { ?p <http://own> ?a } UNION "
        "{ ?p <http://address> ?address . ?p <http://own> ?a } }",
        {"?a=<http://cat> ?p=<http://Alice> ",
         "?a=<http://dog> ?p=<http://Alice> ",
         "?a=<http://snake> ?p=<http://Alice> ",
         "?a=<http://cat> ?address=<http://nantes> ?p=<http://Alice> ",
         "?a=<http://dog> ?address=<http://nantes> ?p=<http://Alice> ",
         "?a=<http://snake> ?address=<http://nantes> ?p=<http://Alice> "});
}

// `UnionTest.create_an_simple_union_with_bgp_inside`: five solutions, and the
// two branches bind DIFFERENT variables — the left has `?p ?a ?s`, the right
// only `?p`. A solution is whatever its branch produced, which is what SPARQL
// says and what this kernel does by carrying bindings per solution rather than
// by a fixed row shape.
TEST(PassageCases, aUnionWhoseBranchesBindDifferentVariables) {
  Index index = indexFrom(triples9());
  check(index,
        "SELECT * WHERE { { ?p <http://own> ?a . ?a <http://species> ?s } "
        "UNION { ?p <http://address> <http://nantes> } }",
        {"?a=<http://cat> ?p=<http://Alice> ?s=<http://feline> ",
         "?a=<http://dog> ?p=<http://Alice> ?s=<http://canine> ",
         "?a=<http://snake> ?p=<http://Alice> ?s=<http://reptile> ",
         "?p=<http://Alice> ",
         "?p=<http://Carol> "});
}

// The two cases of `BGPTest` that this fragment does not cover, recorded here
// rather than left unmentioned: a `VALUES` of SEVERAL rows, which Passage
// handles as a general operator and which would need a table operator to run.
// Refused by what it is, not answered wrongly.
//
// `an_unknown_value_at_first_but_then_known` and its mirror expect three
// solutions each, from the `<http://address>` row of the table; here the
// expectation is a readable refusal.
TEST(PassageCases, aMultiRowValuesIsOutsideTheFragment) {
  Index index = indexFrom(triples9());
  try {
    evaluate(index,
             "SELECT * WHERE { VALUES ?predicate "
             "{ <http://does_not_exist> <http://address> } ?p ?predicate ?c }",
             Quota{10});
    ADD_FAILURE() << "a VALUES of two rows should be refused";
  } catch (const std::exception& e) {
    EXPECT_THAT(std::string{e.what()},
                ::testing::HasSubstr("VALUES clause of 1 variables and 2 rows"));
  }
}

// `?s ?p ?o` over the whole graph, which is nine triples.
TEST(PassageCases, everythingIsNineTriples) {
  Index index = indexFrom(triples9());
  std::vector<std::string> whole = follow(index, "SELECT * WHERE { ?s ?p ?o }", 1000);
  EXPECT_EQ(whole.size(), 9u);
  for (size_t quota : {size_t{1}, size_t{2}, size_t{4}, size_t{9}}) {
    EXPECT_THAT(follow(index, "SELECT * WHERE { ?s ?p ?o }", quota),
                UnorderedElementsAreArray(whole))
        << "quota " << quota;
  }
}
