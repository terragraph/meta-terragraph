/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

static void
usage() {
  printf("monotonic-touch [-c] [-o sec] [-t] [-x] file ...\n");
  printf("   -c do not create any files\n");
  printf("   -o offset seconds (positive)\n");
  printf("   -t print monotonic seconds\n");
  printf("   -x do not make files older\n");
}

static void
errExit(const char* syscall, const char* file) {
  if (file) {
    fprintf(
        stderr,
        "%s(\"%s\") - %s (%d)\n",
        syscall,
        file,
        strerror(errno),
        errno);
  } else {
    fprintf(stderr, "%s - %s (%d)\n", syscall, strerror(errno), errno);
  }
  exit(1);
}

static void
sanityCheckOffset(const char* s) {
  while (*s) {
    if (!isdigit(*s++)) {
      fprintf(stderr, "Invalid offset.\n");
      usage();
      exit(1);
    }
  }
}

// Touch file(s) with a monotonic time that is unaffected by NTP adjustments.
int
main(int argc, char** argv) {
  int create = 1;
  int printTime = 0;
  int offset = 0;
  int makeNewer = 0;
  int i;
  int c;
  struct timespec times[2];

  while ((c = getopt(argc, argv, "co:tx")) != -1) {
    switch (c) {
      case 'c':
        create = 0;
        break;
      case 'o':
        sanityCheckOffset(optarg);
        offset = atoi(optarg);
        break;
      case 't':
        printTime = 1;
        break;
      case 'x':
        makeNewer = 1;
        break;
      default:
        usage();
        return 1;
        break;
    }
  }

  if (!printTime && optind >= argc) {
    fprintf(stderr, "Missing filename(s)\n");
    usage();
    return 1;
  }

  if (0 != clock_gettime(CLOCK_MONOTONIC_RAW, times)) {
    errExit("clock_gettime", 0);
  }
  times[0].tv_sec += offset;
  times[1] = times[0];

  if (printTime) {
    printf("%u\n", (unsigned)times[0].tv_sec);
  }

  for (i = optind; i < argc; i++) {
    const char* file = argv[i];
    struct stat statbuf;
    const int exists = (stat(file, &statbuf) == 0);

    if (!exists && errno != ENOENT) {
      errExit("stat", file); // unexpected stat error
    }

    if (!create && !exists) {
      continue; // skip, not an error - same behaviour as "touch"
    }

    if (exists && makeNewer && statbuf.st_mtim.tv_sec >= times[0].tv_sec) {
      continue; // skip, don't make file older
    }

    // set monotonic timestamp
    if (exists) {
      if (0 != utimensat(AT_FDCWD, file, times, 0)) {
        errExit("utimensat", file);
      }
    } else {
      // only open a file when necessary for creating it
      int fd = open(file, O_CREAT | O_WRONLY, S_IRWXU | S_IRWXG | S_IRWXO);
      if (fd == -1) {
        errExit("open", file);
      }
      if (0 != futimens(fd, times)) {
        errExit("futimens", file);
      }
      if (close(fd) != 0) {
        errExit("close", file);
      }
    }
  }

  return 0;
}
