#include "../src/kwordexp_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <wordexp.h>
#ifdef REPLACE_SYSTEM_ALLOC
#include <gc.h>
#endif

int main(int argc, char **argv) {
#ifdef REPLACE_SYSTEM_ALLOC
  GC_INIT();
#endif
  int mode_we = 0;
  while (1) {
    int opt = getopt(argc, argv, "whv");
    if (opt == -1)
      break;
    switch (opt) {
    case 'w':
      mode_we = 1;
      break;
    case 'h':
      printf("Usage: %s [-w] [-h] [-v] [word ...]\n", argv[0]);
      printf("  -w: use wordexp\n");
      printf("  -h: show this help\n");
      printf("  -v: show version\n");
      return 0;
      break;
    case 'v':
      printf("Version: 1.0\n");
      return 0;
      break;
    default:
      exit(EXIT_FAILURE);
    }
  }

  if (mode_we) {
    for (int i = optind; i < argc; i++) {
      printf("argv[%d]=%s\n", i, argv[i]);
      wordexp_t we;
      wordexp(argv[i], &we, 0);
      printf("we_wordc: %zu\n", we.we_wordc);
      for (size_t j = 0; j < we.we_wordc; j++) {
        printf("we_wordv[%zu]=%s\n", j, we.we_wordv[j]);
      }
      wordfree(&we);
    }
  } else {
    for (int i = optind; i < argc; i++) {
      printf("argv[%d]=%s\n", i, argv[i]);
      kwordexp_t kwe;
      kwe.kwe_argc = argc;
      kwe.kwe_argv = argv;
      kwe.kwe_data = NULL;
      kwe.kwe_wordc = 0;
      kwe.kwe_wordv = NULL;
      kwe.kwe_last_arg = NULL;
      kwe.kwe_last_bgpid = 0;
      kwe.kwe_last_status = 0;
      kwe.kwe_exec = NULL;
      kwe.kwe_getenv = NULL;
      kwe.kwe_setenv = NULL;

      int ret = kwordexp(argv[i], &kwe, 0);
      if (ret != 0) {
        printf("kwordexp failed\n");
        continue;
      }
      printf("kwe_wordc: %zu\n", kwe.kwe_wordc);
      for (size_t j = 0; j < kwe.kwe_wordc; j++) {
        printf("kwe_wordv[%zu]: %s\n", j, kwe.kwe_wordv[j]);
      }
      kwordfree(&kwe);
    }
  }
  exit(EXIT_SUCCESS);
}