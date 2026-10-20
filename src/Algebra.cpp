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
      // `BIND`, as Passage writes it and as the paper shows it. `VALUES` is
      // still accepted when reading; see the header.
      for (const auto& [variable, term] : c->bindings) {
        out << "BIND(" << term << " AS " << variable << ") ";
      }
      writeBody(c->children.front(), out);
      return;
    case Node::Kind::Join:
      writeBody(c->children.front(), out);
      out << ' ';
      writeBody(c->children.back(), out);
      return;
    case Node::Kind::Union: {
      // Flattened, as Passage's `FlattenUnflatten.unflattenUnion` does: three
      // branches print as `{A} UNION {B} UNION {C}` and not as a nested
      // pair. `UNION` is associative, so this is the same query — but it is
      // the same text as well, which is the point.
      std::vector<Cont> branches;
      auto collect = [&branches](const Cont& node, auto&& self) -> void {
        if (node->kind == Node::Kind::Union) {
          self(node->children.front(), self);
          self(node->children.back(), self);
        } else {
          branches.push_back(node);
        }
      };
      collect(c, collect);
      for (size_t i = 0; i < branches.size(); ++i) {
        if (i > 0) { out << " UNION "; }
        out << "{ ";
        writeBody(branches[i], out);
        out << " }";
      }
      return;
    }
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
  // A join over a union is KEPT, not distributed, because that is the form
  // Passage writes: `Pause2Next.visit(OpJoin)` builds
  // `OpJoin.create(left, join.getRight())` with `left` already a union when
  // three patterns are nested, and Jena does not push the right side into the
  // branches. An earlier version here distributed it, which said the same
  // thing in different text — and different text is a divergence, since a
  // continuation should read the same whichever implementation produced it.
  //
  // Nothing is needed to execute it: `JoinIterator`'s left side is an
  // iterator, so a union serves there as well as a scan does.
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
