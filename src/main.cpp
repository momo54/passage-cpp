// A command line around the kernel, so that a continuation sequence can be
// watched rather than described.
//
//   passage --index <basename> --query '<SPARQL>' [--quota N] [--loop]
//
// Without `--loop` it evaluates once and prints the continuation, which is
// what a client would receive. With `--loop` it feeds each continuation back
// in until none comes, which is what a client would do.
#include <chrono>
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
  bool count = false;

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
    else if (arg == "--count") { count = true; }
    else { usage(); return 2; }
  }
  if (basename.empty() || query.empty()) { usage(); return 2; }

  try {
    auto allocator = ad_utility::makeUnlimitedAllocator<Id>();
    Index index{allocator};
    index.createFromOnDiskIndex(basename, false);

    // Opening the index reads its metadata, which on a ten-million-triple
    // index is hundreds of megabytes and has nothing to do with the query. It
    // is timed separately so that a comparison against another engine
    // compares what the engines do.
    auto opened = std::chrono::steady_clock::now();

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
      // `--count` consumes the solutions without rendering them, because
      // rendering is not what an engine does. Printing 200,000 solutions
      // means 200,000 lines on stdout and three or four vocabulary lookups
      // each — which is what a measurement of this CLI was mostly made of
      // before this flag existed.
      for (const auto& solution : outcome.solutions) {
        if (count) { break; }
        std::cout << "   ";
        for (const auto& b : solution.bindings) {
          // The one place the text is wanted: printing it.
          std::cout << b.variable << '=' << passage::asText(index, b.term)
                    << ' ';
        }
        std::cout << '\n';
      }
      if (!outcome.continuation.has_value()) {
        auto finished = std::chrono::steady_clock::now();
        double ms = std::chrono::duration<double, std::milli>(finished - opened)
                        .count();
        std::cout << "== done: " << total << " solutions in " << evaluations
                  << " evaluation" << (evaluations == 1 ? "" : "s") << ", "
                  << ms << " ms, "
                  << (ms > 0 ? static_cast<size_t>(total / ms * 1000.0) : 0)
                  << " solutions/s\n";
        break;
      }
      std::cout << "== continuation:\n   " << *outcome.continuation << '\n';
      if (!loop) {
        auto finished = std::chrono::steady_clock::now();
        double ms = std::chrono::duration<double, std::milli>(finished - opened)
                        .count();
        std::cout << "== paused: " << total << " solutions in " << evaluations
                  << " evaluation" << (evaluations == 1 ? "" : "s") << ", "
                  << ms << " ms, "
                  << (ms > 0 ? static_cast<size_t>(total / ms * 1000.0) : 0)
                  << " solutions/s\n";
        break;
      }
      current = *outcome.continuation;
    }
  } catch (const std::exception& e) {
    std::cerr << "error: " << e.what() << '\n';
    return 1;
  }
  return 0;
}
