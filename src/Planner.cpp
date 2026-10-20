#include "Planner.h"

#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

#include "JoinIterator.h"
#include "ScanIterator.h"
#include "UnionIterator.h"
#include "engine/sparqlExpressions/LiteralExpression.h"
#include "parser/GraphPattern.h"
#include "parser/GraphPatternOperation.h"
#include "parser/SparqlTriple.h"

namespace passage {
namespace {

// What one group contains, after being sorted into the four things this
// fragment allows. Anything else has already thrown by the time this is full.
struct Group {
  std::vector<Binding> values;
  const parsedQuery::Subquery* subquery = nullptr;
  std::vector<SparqlTripleSimple> triples;
  const parsedQuery::Union* union_ = nullptr;
};

[[noreturn]] void refuse(const std::string& what) {
  throw std::runtime_error(
      "this kernel reads the continuations it writes, and " + what +
      " is not one of their parts: a group of VALUES, a subquery with an "
      "OFFSET, a basic graph pattern, and a UNION of two groups");
}

Group readGroup(const parsedQuery::GraphPattern& pattern);

// A plain `{ ... }` nested in a group adds nothing in this fragment, which has
// no operator for which a group is a scope, so it is flattened into its
// parent. The printer emits one around each union branch and another around a
// subquery, so a continuation always has some.
void mergeInto(Group& group, Group child, const std::string& where) {
  for (auto& binding : child.values) {
    group.values.push_back(std::move(binding));
  }
  for (auto& triple : child.triples) {
    group.triples.push_back(std::move(triple));
  }
  if (child.subquery != nullptr) {
    if (group.subquery != nullptr) { refuse("two subqueries in " + where); }
    group.subquery = child.subquery;
  }
  if (child.union_ != nullptr) {
    if (group.union_ != nullptr) { refuse("two unions in " + where); }
    group.union_ = child.union_;
  }
}

Group readGroup(const parsedQuery::GraphPattern& pattern) {
  Group group;
  for (const auto& operation : pattern._graphPatterns) {
    if (const auto* values = std::get_if<parsedQuery::Values>(&operation)) {
      const auto& inline_ = values->_inlineValues;
      // One variable and one row is all `Bindings` ever prints. More would be
      // a `VALUES` someone wrote by hand, and running it would need a table
      // operator this does not have.
      if (inline_._variables.size() != 1 || inline_._values.size() != 1 ||
          inline_._values.front().size() != 1) {
        refuse("a VALUES clause of " +
               std::to_string(inline_._variables.size()) + " variables and " +
               std::to_string(inline_._values.size()) + " rows");
      }
      const TripleComponent& term = inline_._values.front().front();
      group.values.push_back(
          Binding{inline_._variables.front().name(), term});
    } else if (const auto* bind = std::get_if<parsedQuery::Bind>(&operation)) {
      // `BIND(<term> AS ?v)`, which is what this kernel writes and what
      // Passage writes. Only a constant is accepted: a `BIND` of a computed
      // expression is a `BIND` someone wrote by hand, and evaluating it would
      // need the expression machinery this kernel does without.
      //
      // The `dynamic_cast` is the idiom QLever itself uses, in
      // `getIdOrLocalVocabEntryFromLiteralExpression`.
      const sparqlExpression::SparqlExpression* expression =
          bind->_expression.getPimpl();
      if (const auto* iri =
              dynamic_cast<const sparqlExpression::IriExpression*>(expression)) {
        TripleComponent term{iri->value()};
        group.values.push_back(Binding{bind->_target.name(), term});
      } else if (const auto* literal =
                     dynamic_cast<const sparqlExpression::StringLiteralExpression*>(
                         expression)) {
        TripleComponent term{literal->value()};
        group.values.push_back(Binding{bind->_target.name(), term});
      } else {
        refuse("a BIND of something other than a constant (" +
               bind->getDescriptor() + ")");
      }
    } else if (const auto* sub =
                   std::get_if<parsedQuery::Subquery>(&operation)) {
      if (group.subquery != nullptr) { refuse("two subqueries in one group"); }
      group.subquery = sub;
    } else if (const auto* bgp =
                   std::get_if<parsedQuery::BasicGraphPattern>(&operation)) {
      for (const auto& triple : bgp->_triples) {
        group.triples.push_back(triple.getSimple());
      }
    } else if (const auto* u = std::get_if<parsedQuery::Union>(&operation)) {
      if (group.union_ != nullptr) { refuse("two unions in one group"); }
      group.union_ = u;
    } else if (const auto* nested =
                   std::get_if<parsedQuery::GroupGraphPattern>(&operation)) {
      // A `GRAPH <g> { ... }` is a quad pattern, which this fragment does not
      // have; a plain group is flattened.
      if (!std::holds_alternative<std::monostate>(nested->graphSpec_)) {
        refuse("a GRAPH clause");
      }
      mergeInto(group, readGroup(nested->_child), "a nested group");
    } else {
      refuse("that graph pattern");
    }
  }
  return group;
}

// A group is a conjunction, and the order matters: the subquery, if there is
// one, is the outermost loop, because that is the side a paused outer scan
// resumes on. The patterns then nest inside it, left to right.
std::unique_ptr<Iterator> buildGroup(const Index& index,
                                     const parsedQuery::GraphPattern& pattern,
                                     Context& context, size_t offset) {
  Group group = readGroup(pattern);

  std::unique_ptr<Iterator> tree;
  size_t firstPattern = 0;
  if (group.union_ != nullptr) {
    // A union is the outermost loop of its group, and the patterns beside it
    // nest inside — which is what `{ {A} UNION {B} } tp` means, and what
    // Passage writes when it factors a suffix out of a union.
    if (group.subquery != nullptr) {
      refuse("a UNION beside a subquery in one group");
    }
    if (offset > 0) { refuse("an OFFSET on a group that holds a UNION"); }
    const parsedQuery::Union& u = *group.union_;
    auto left = buildGroup(index, u._child1, context, 0);
    auto makeRight = [&index, &u, &context]() {
      return buildGroup(index, u._child2, context, 0);
    };
    // `_child2` as algebra, for the case where the execution pauses before
    // reaching it. A group is a query with no offset of its own.
    ParsedQuery asQuery;
    asQuery._rootGraphPattern = u._child2;
    tree = std::make_unique<UnionIterator>(std::move(left), makeRight,
                                           queryAsCont(asQuery));
  } else if (group.subquery != nullptr) {
    if (offset > 0) {
      refuse("an OFFSET on a group that also holds a subquery");
    }
    tree = buildTree(index, group.subquery->get(), context);
  } else {
    if (group.triples.empty()) { refuse("a group with no triple pattern"); }
    // The query's own offset belongs to the first scan. For several patterns
    // it would have to mean an offset into the join's solutions, which this
    // never writes and cannot resume cheaply, so it is refused above.
    tree = std::make_unique<ScanIterator>(index, group.triples.front(),
                                          group.values, offset, context);
    firstPattern = 1;
  }
  for (size_t i = firstPattern; i < group.triples.size(); ++i) {
    tree = std::make_unique<JoinIterator>(index, std::move(tree),
                                          group.triples[i], context);
  }
  return tree;
}

}  // namespace

std::unique_ptr<Iterator> buildTree(const Index& index,
                                    const ParsedQuery& query,
                                    Context& context) {
  if (query._limitOffset._limit.has_value()) {
    throw std::runtime_error(
        "a LIMIT in the query would be a second quota beside the kernel's own; "
        "pass the quota to the kernel instead");
  }
  size_t offset = query._limitOffset._offset;
  // An offset over several patterns would be an offset into a join's
  // solutions. A continuation never writes one — it writes a union of two
  // offsets instead, which is the whole point — and resuming one would mean
  // replaying the outer loop.
  Group group = readGroup(query._rootGraphPattern);
  if (offset > 0 && group.triples.size() > 1) {
    refuse("an OFFSET over a basic graph pattern of " +
           std::to_string(group.triples.size()) + " patterns");
  }
  return buildGroup(index, query._rootGraphPattern, context, offset);
}

Cont queryAsCont(const ParsedQuery& query) {
  Group group = readGroup(query._rootGraphPattern);

  Cont body = done();
  if (group.union_ != nullptr) {
    // A union is the group's outermost part, and the patterns beside it join
    // onto it below — it no longer returns early, which dropped them.
    ParsedQuery left;
    left._rootGraphPattern = group.union_->_child1;
    ParsedQuery right;
    right._rootGraphPattern = group.union_->_child2;
    body = unite(queryAsCont(left), queryAsCont(right));
  } else if (group.subquery != nullptr) {
    body = queryAsCont(group.subquery->get());
  }
  for (const auto& triple : group.triples) {
    body = join(body, pattern(triple));
  }
  std::vector<std::pair<std::string, std::string>> binds;
  for (const Binding& b : group.values) {
    // A term read out of a query writes itself; no index is needed.
    binds.emplace_back(b.variable, b.term.toString());
  }
  body = bindings(std::move(binds), body);
  size_t offset = query._limitOffset._offset;
  return offset > 0 ? slice(body, offset) : body;
}

}  // namespace passage
