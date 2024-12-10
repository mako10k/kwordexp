#include "kstr.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>

int kstr_init(kstr_t *kstr, const char *buf, size_t len) {
  if (buf == NULL && len != 0) {
    errno = EINVAL;
    return -1;
  }
  char *buf_new = NULL;
  size_t cap_new = 0;
  if (buf != NULL) {
    cap_new = len + 1;
    buf_new = malloc(cap_new);
    if (buf_new == NULL) {
      return -1;
    }
    memcpy(buf_new, buf, len);
  }
  kstr->buf = buf_new;
  kstr->len = len;
  kstr->cap = cap_new;
  return 0;
}

void kstr_destory(kstr_t *kstr) {
  free(kstr->buf);
}

int kstr_put(kstr_t *ks, const char *str, size_t len) {
  if (ks->len + len + 1 >= ks->cap) {
    size_t cap_new = ks->cap * 2;
    if (ks->len + len + 1 >= cap_new) {
      cap_new = ks->len + len + 1;
    }
    char *buf_new = realloc(ks->buf, cap_new);
    if (buf_new == NULL) {
      return -1;
    }
    ks->buf = buf_new;
    ks->cap = cap_new;
  }
  memcpy(ks->buf + ks->len, str, len);
  ks->len += len;
  ks->buf[ks->len] = '\0';
  return 0;
}
