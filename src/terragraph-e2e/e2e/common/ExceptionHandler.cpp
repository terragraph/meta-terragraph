/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "ExceptionHandler.h"

#include <cxxabi.h>
#include <execinfo.h>
#include <glog/logging.h>
#include <sstream>
#include <unistd.h>

namespace {
// Maximum frames to print from the call stack
const size_t kMaxFrames{20};

// Beginning stack frames to skip (e.g. called by stack trace code itself)
const size_t kSkipFrames{5};
} // namespace

namespace facebook {
namespace terragraph {

void
ExceptionHandler::install() {
  std::set_terminate([]() {
    // Print the current exception (if possible)
    std::exception_ptr eptr = std::current_exception();
    if (eptr != 0) {
      try {
        std::rethrow_exception(eptr);
      } catch (std::exception &ex) {
        LOG(ERROR) << "*** Terminated due to exception: ***\n  " << ex.what();
      } catch (...) {
        LOG(ERROR) << "*** Terminated due to unknown exception ***";
      }
    } else {
      LOG(ERROR) << "*** Terminated due to unknown reason ***";
    }

    // Print stack trace
    printStackTrace();

    // Raise SIGABRT
    std::abort();
  });
}

void
ExceptionHandler::printStackTrace() {
  // Get stack addresses
  void *addrList[kMaxFrames + 1];
  size_t size = backtrace(addrList, sizeof(addrList) / sizeof(void*));
  if (size <= kSkipFrames) {
    return;
  }

  // Get string representations of stack addresses
  char **symbols = backtrace_symbols(addrList, size);
  if (symbols == nullptr) {
    // This is likely a malloc() error, so we could call a similar function
    // backtrace_symbols_fd() to directly write the stack trace to fd without
    // demangling names.
    return;
  }

  // Print each stack frame
  std::ostringstream output;
  for (size_t i = kSkipFrames; i < size; i++) {
    // Find parentheses and +address offset surrounding the mangled name
    // ./module(function+0x15c) [0x8048a6d]
    char* beginName = nullptr;
    char* beginOffset = nullptr;
    char* endOffset = nullptr;
    for (char *p = symbols[i]; *p; p++) {
      if (*p == '(') {
        beginName = p;
      } else if (*p == '+') {
        beginOffset = p;
      } else if (*p == ')' && (beginOffset || beginName)) {
        endOffset = p;
      }
    }

    if (beginName && endOffset && (beginName < endOffset)) {
      // Mangled name is in [beginName, beginOffset)
      // Caller offset is in [beginOffset, endOffset)
      *beginName++ = '\0';
      *endOffset++ = '\0';
      if (beginOffset) {
        *beginOffset++ = '\0';
      }

      // Demangle the name
      int status;
      char *name = abi::__cxa_demangle(beginName, nullptr, 0, &status);

      // Print the formatted line
      output << "  " << symbols[i] << " ("
             << ((name != nullptr && status == 0) ? name : beginName);
      if (beginOffset) {
        output << " +" << beginOffset;
      }
      output << ")" << endOffset;

      if (name != nullptr) {
        free(name);
      }
    } else {
      // Failed to parse line, print the raw string
      output << "  " << symbols[i];
    }

    output << "\n";
  }
  free(symbols);

  LOG(ERROR) << "*** Stack trace: ***\n" << output.str();
}

} // namespace terragraph
} // namespace facebook
