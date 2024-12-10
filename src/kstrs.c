#include "kstrs.h"
#include "kstr.h"
#include <stddef.h>
#include <stdlib.h>

int kstrs_init(kstrs_t *kstrs) {
  kstrs->strv = NULL;
  kstrs->strc = 0;
  return kstr_init(&kstrs->kstr, NULL, 0);
}

int kstrs_push(kstrs_t *kstrs) {
  const char **strv_new = realloc(kstrs->strv, (kstrs->strc + 1) * sizeof(char *));
  if (strv_new == NULL) {
    return -1;
  }
  kstr_t kstr;
  int ret = kstr_init(&kstr, NULL, 0);
  if (ret == -1) {
    return -1;
  }
  kstrs->strv = strv_new;
  kstrs->strv[kstrs->strc++] = kstrs->kstr.buf;
  kstrs->kstr = kstr;
  return 0;
}

void kstrs_destroy(kstrs_t *kstrs) {
  for (size_t i = 0; i < kstrs->strc; i++) {
    free((void *)kstrs->strv[i]);
  }
  free(kstrs->strv);
  kstr_destroy(kstrs->kstr);
}