#ifndef __KWORDEXP_H__
#define __KWORDEXP_H__

#include <stddef.h>

typedef struct kwordexp kwordexp_t;
typedef const char *(*kwordexp_setenv_t)(void *data, const char *key,
                                         const char *value, int overwrite);
typedef const char *(*kwordexp_getenv_t)(void *data, const char *key);
typedef int (*kwordexp_exec_t)(void *data, const char **word, char **result);

struct kwordexp {
  const char **kwe_wordv;
  size_t kwe_wordc;
  const char **kwe_argv;
  size_t kwe_argc;
  kwordexp_setenv_t kwe_setenv;
  kwordexp_getenv_t kwe_getenv;
  kwordexp_exec_t kwe_exec;
  void *kwe_data;
  int last_status;
};

int kwordexp(const char *words, kwordexp_t *we, int flags);
void kwordfree(kwordexp_t *we);

int kwordexp_setenv_default(void *data, const char *key, const char *value,
                            int overwrite);
const char *kwordexp_getenv_default(void *data, const char *key);
int kwordexp_exec_default(void *data, const char **words, char **result);

#endif