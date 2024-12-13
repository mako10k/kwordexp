#pragma once

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "../include/kwordexp.h"
#include "kio_internal.h"

typedef struct kwordexp_internal kwordexp_internal_t;

typedef enum kwei_err {
  KENONE = 0,
  KESYSTEM = 1,
  KESYNTAX = 2,
  KENARG = 3,
  KEUNDEF = 4,
} kwei_err_t;

typedef enum kwei_status {
  KSERROR = -1,
  KSSUCCESS = 0,
} kwei_status_t;

struct kwordexp_internal {
  kwordexp_t *kwei_pwe;
  kin_t *kwei_pin;
  kout_t *kwei_pout;
  int kwei_flags;
  int kwei_has_arg;
  int kwei_has_pattern;
  int kwei_errno;
  kwei_err_t kwei_errex;
  kwei_status_t kwei_status;
  const char *kwei_ifs;
};

int kwordexp(const char *ibuf, kwordexp_t *we, int flags)
    __attribute__((warn_unused_result, nonnull(1, 2)));

int kfwordexp(FILE *ifp, kwordexp_t *we, int flags)
    __attribute__((warn_unused_result, nonnull(1, 2)));

void kwordfree(kwordexp_t *we) __attribute__((nonnull(1)));

void kwordexp_init(kwordexp_t *we, char **argv, size_t argc)
    __attribute__((nonnull(1, 2)));

int kwordexp_setenv_default(void *data, const char *key, char *value,
                            int overwrite)
    __attribute__((weak, warn_unused_result, nonnull(2, 3)));

int kwordexp_getenv_default(void *data, const char *key, char **pvalue)
    __attribute__((weak, warn_unused_result, nonnull(2)));

int kwordexp_exec_default(void *data, char **argv, FILE *ofp)
    __attribute__((weak, warn_unused_result, nonnull(2, 3)));

int kwei_isspace(int ch, const char *ifs)
    __attribute__((warn_unused_result, nonnull(2)));

void kwe_init(kwordexp_t *pkwe, char **argv, size_t argc)
    __attribute__((nonnull(1, 2)));

void kwe_copy(kwordexp_t *pkwe, const kwordexp_t *pother)
    __attribute__((nonnull(1, 2)));

void kwe_free(kwordexp_t *pkwe) __attribute__((nonnull(1)));

kwordexp_internal_t kwei_init(kwordexp_t *pkwe, kin_t *pkin, kout_t *pkout,
                              int flags)
    __attribute__((warn_unused_result, nonnull(1)));

kwei_status_t kwei_parse_squote(kwordexp_internal_t *pkwei)
    __attribute__((warn_unused_result, nonnull(1)));

kwei_status_t kwei_getenv(kwordexp_internal_t *pkwei, const char *key,
                          char **pvalue)
    __attribute__((warn_unused_result, nonnull(1, 2, 3)));

kwei_status_t kwei_exec(kwordexp_internal_t *pkwei, char **argv, FILE *ofp)
    __attribute__((warn_unused_result, nonnull(1, 2, 3)));

kwei_status_t kwei_parse_var_paren(kwordexp_internal_t *pkwei)
    __attribute__((warn_unused_result, nonnull(1)));

kwei_status_t kwei_parse_var_brace(kwordexp_internal_t *pkwei)
    __attribute__((warn_unused_result, nonnull(1)));

kwei_status_t kwei_push_word(kwordexp_internal_t *pkwei)
    __attribute__((warn_unused_result, nonnull(1)));

kwei_status_t kwei_var_asterisk(kwordexp_internal_t *pkwei)
    __attribute__((warn_unused_result, nonnull(1)));

kwei_status_t kwei_var_atto(kwordexp_internal_t *pkwei)
    __attribute__((warn_unused_result, nonnull(1)));

kwei_status_t kwei_var_num(kwordexp_internal_t *pkwei, int ch)
    __attribute__((warn_unused_result, nonnull(1)));

kwei_status_t kwei_parse_var(kwordexp_internal_t *pkwei)
    __attribute__((warn_unused_result, nonnull(1)));

kwei_status_t kwei_parse_internal(kwordexp_internal_t *pkwei)
    __attribute__((warn_unused_result, nonnull(1)));

kwei_status_t kwei_parse(kwordexp_internal_t *pkwei)
    __attribute__((warn_unused_result, nonnull(1)));
