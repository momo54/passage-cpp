#include "Algebra.h"

#include <sstream>
#include <stdexcept>

namespace passage {
namespace {

Cont make(Node node) { return std::make_shared<const Node>(std::move(node)); }

// The body of a query, without the `SELECT * WHERE { }` around it. A slice is
// the one node that cannot be written inside a body: `OFFSET` belongs to the
// query, so a nested slice becomes a subquery, which is exactly what Passage's
// continuations look like.
void writeBody(const Cont& c, std::ostringstream& out) {
  switch (c->kind) {
    case Node::Kind::Done:
      // An empty `VALUES` clause, as `OpTable.empty()` prints. Reachable only
      // when the whole continuation is done, which callers avoid by testing
      // `isDone` first, but printing something valid is better than nothing.
      out << "VALUES () { }";
      return;
    case Node::Kind::Pattern: {
      const auto& t = *c->triple;
      out << t.s_.toString() << ' ' << t.p_.toString() << ' '
          << t.o_.toString() << " .";
      return;
    }
    case Node::Kind::Bindings:
      // `VALUES` rather than `BIND`, so that reading this back is reading data
      // instead of an expression; see the header.
      for (const auto& [variable, term] : c->bindings) {
        out << "VALUES " << variable << " { " << term << " } ";
      }
      writeBody(c->children.front(), out);
      return;
    case Node::Kind::Join:
      writeBody(c->children.front(), out);
      out << ' ';
      writeBody(c->children.back(), out);
      return;
    case Node::Kind::Union:
      out << "{ ";
      writeBody(c->children.front(), out);
      out << " } UNION { ";
      writeBody(c->children.back(), out);
      out << " }";
      return;
    case Node::Kind::Slice:
      // A slice below the root has to be parenthesised as a subquery, since
      // `OFFSET` applies to a query and not to a group.
      out << "{ SELECT * WHERE { ";
      writeBody(c->children.front(), out);
      out << " } OFFSET " << c->offset << " }";
      return;
  }
  throw std::logic_error("unhandled continuation node");
}

}  // namespace

Cont done() { return make(Node{Node::Kind::Done, {}, 0, {}, {}}); }

Cont pattern(SparqlTripleSimple triple) {
  return make(Node{Node::Kind::Pattern, std::move(triple), 0, {}, {}});
}

Cont slice(Cont child, size_t offset) {
  return make(Node{Node::Kind::Slice, {}, offset, {}, {std::move(child)}});
}

Cont bindings(std::vector<std::pair<std::string, std::string>> binds,
              Cont child) {
  if (binds.empty()) { return child; }
  return make(Node{Node::Kind::Bindings, {}, 0, std::move(binds),
                   {std::move(child)}});
}

Cont join(Cont left, Cont right) {
  if (isDone(left)) { return right; }
  if (isDone(right)) { return left; }
  // A join over a union is DISTRIBUTED, not kept: `(a ∪ b) ⋈ p` becomes
  // `(a ⋈ p) ∪ (b ⋈ p)`. Join distributes over union, so this says the same
  // thing, and it is the choice this kernel has already made everywhere else
  // — Passage's algebra factors a common suffix out of a union, and here the
  // suffix goes into each branch instead, so the reader needs no operator for
  // a union joined with a pattern.
  //
  // Not cosmetic: a join of three patterns pauses with its outer side already
  // a union, so without this the kernel printed a continuation its own planner
  // refused, and a sequence over three patterns could not terminate. Found by
  // Passage's own `BGPTest` cases, where Alice's three animals make the inner
  // scan deep enough to reach it.
  if (left->kind == Node::Kind::Union) {
    return unite(join(left->children.front(), right),
                 join(left->children.back(), right));
  }
  if (right->kind == Node::Kind::Union) {
    return unite(join(left, right->children.front()),
                 join(left, right->children.back()));
  }
  return make(Node{Node::Kind::Join, {}, 0, {},
                   {std::move(left), std::move(right)}});
}

Cont unite(Cont left, Cont right) {
  if (isDone(left) && isDone(right)) { return done(); }
  if (isDone(left)) { return right; }
  if (isDone(right)) { return left; }
  return make(Node{Node::Kind::Union, {}, 0, {},
                   {std::move(left), std::move(right)}});
}

bool isDone(const Cont& c) { return c->kind == Node::Kind::Done; }

std::string asSparql(const Cont& c) {
  std::ostringstream out;
  // At the root a slice is written as the query's own `OFFSET`, which is what
  // makes the continuation of one triple pattern read as the original query
  // with an offset rather than as a subquery wrapped in one.
  if (c->kind == Node::Kind::Slice) {
    out << "SELECT * WHERE { ";
    writeBody(c->children.front(), out);
    out << " } OFFSET " << c->offset;
    return out.str();
  }
  out << "SELECT * WHERE { ";
  writeBody(c, out);
  out << " }";
  return out.str();
}

}  // namespace passage
