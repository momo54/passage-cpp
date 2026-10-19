// A command line around the kernel, so that a continuation sequence can be
// watched rather than described.
//
//   passage --index <basename> --query '<SPARQL>' [--quota N] [--loop]
//
// Without `--loop` it evaluates once and prints the continuation, which is
// what a client would receive. With `--loop` it feeds each continuation back
// in until none comes, which is what a client would do.
#include <iostream>
#include <string>

#include "Continuation.h"
#include "index/Index.h"
#include "util/AllocatorWithLimit.h"

namespace {

void usage() {
  std::cerr << "usage: passage --index <basename> --query '<SPARQL>'"
               " [--quota N] [--loop]\n";
}

}  // namespace

int main(int argc, char** argv) {
  std::string basename;
  std::string query;
  size_t quota = 1000;
  bool loop = false;

  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    auto next = [&]() -> std::string {
      if (i + 1 >= argc) { usage(); std::exit(2); }
      return argv[++i];
    };
    if (arg == "--index") { basename = next(); }
    else if (arg == "--query") { query = next(); }
    else if (arg == "--quota") { quota = std::stoul(next()); }
    else if (arg == "--loop") { loop = true; }
    else { usage(); return 2; }
  }
  if (basename.empty() || query.empty()) { usage(); return 2; }

  try {
    auto allocator = ad_utility::makeUnlimitedAllocator<Id>();
    Index index{allocator};
    index.createFromOnDiskIndex(basename, false);

    size_t evaluations = 0;
    size_t total = 0;
    std::string current = query;
    while (true) {
      passage::Outcome outcome =
          passage::evaluate(index, current, passage::Quota{quota});
      ++evaluations;
      total += outcome.solutions.size();

      std::cout << "== evaluation " << evaluations << ", from offset "
                << outcome.startedAt << ": " << outcome.solutions.size()
                << " solutions\n";
      for (const auto& solution : outcome.solutions) {
        std::cout << "   ";
        for (const auto& [name, value] : solution.bindings) {
          std::cout << name << '=' << value << ' ';
        }
        std::cout << '\n';
      }
      if (!outcome.continuation.has_value()) {
        std::cout << "== done: " << total << " solutions in " << evaluations
                  << " evaluation" << (evaluations == 1 ? "" : "s") << '\n';
        break;
      }
      std::cout << "== continuation:\n   " << *outcome.continuation << '\n';
      if (!loop) { break; }
      current = *outcome.continuation;
    }
  } catch (const std::exception& e) {
    std::cerr << "error: " << e.what() << '\n';
    return 1;
  }
  return 0;
}
