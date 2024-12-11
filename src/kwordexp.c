#include "kwordexp.h"
#include "kio.h"
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

typedef enum kwei_err {
  KENONE = 0,
  KESYSTEM = 1,
  KESYNTAX = 2,
  KENARG = 3,
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
  int kwei_inword;
  int kwei_errno;
  kwei_err_t kwei_errex;
  kwei_status_t kwei_status;
};

static void kwe_free(kwordexp_t *pkwe) __attribute__((nonnull(1)));
static kwordexp_internal_t kwei_init(kwordexp_t *pkwe, kin_t *pkin,
                                     kout_t *pkout, int flags)
    __attribute__((warn_unused_result, nonnull(1)));

static kwei_status_t kwei_parse_squote(kwordexp_internal_t *pkwei)
    __attribute__((warn_unused_result, nonnull(1)));
static kwei_status_t kwei_exec(kwordexp_internal_t *pkwei, const char **argv,
                               FILE *ofp)
    __attribute__((warn_unused_result, nonnull(1, 2, 3)));
static kwei_status_t kwei_parse_var_paren(kwordexp_internal_t *pkwei)
    __attribute__((warn_unused_result, nonnull(1)));
static const char *kwei_get_ifs(kwordexp_t *pkwe)
    __attribute__((warn_unused_result, nonnull(1)));
static kwei_status_t kwei_push_word(kwordexp_internal_t *pkwei)
    __attribute__((warn_unused_result, nonnull(1)));
static kwei_status_t kwei_var_asterisk(kwordexp_internal_t *pkwei)
    __attribute__((warn_unused_result, nonnull(1)));
static kwei_status_t kwei_var_atto(kwordexp_internal_t *pkwei)
    __attribute__((warn_unused_result, nonnull(1)));
static kwei_status_t kwei_var_num(kwordexp_internal_t *pkwei, int ch)
    __attribute__((warn_unused_result, nonnull(1)));
static kwei_status_t kwei_parse_var(kwordexp_internal_t *pkwei)
    __attribute__((warn_unused_result, nonnull(1)));
static kwei_status_t kwei_parse(kwordexp_internal_t *pkwei)
    __attribute__((warn_unused_result, nonnull(1)));

static kwordexp_internal_t kwei_init(kwordexp_t *pkwe, kin_t *pkin,
                                     kout_t *pkout, int flags) {
  kwordexp_internal_t kwei;
  kwei.kwei_pwe = pkwe;
  kwei.kwei_pin = pkin;
  kwei.kwei_pout = pkout;
  kwei.kwei_flags = flags;
  kwei.kwei_inword = 0;
  kwei.kwei_errno = 0;
  kwei.kwei_errex = KENONE;
  kwei.kwei_status = KSSUCCESS;
  return kwei;
}

static void kwe_free(kwordexp_t *pkwe) {
  if (pkwe->kwe_wordv != NULL) {
    free(pkwe->kwe_wordv);
    pkwe->kwe_wordv = NULL;
  }
}

static kwei_status_t kwei_parse_squote(kwordexp_internal_t *pkwei) {
  while (1) {
    int ch = kin_getc(pkwei->kwei_pin);
    switch (ch) {

    case EOF:
      if (kin_error(pkwei->kwei_pin)) {
        pkwei->kwei_errno = errno;
        pkwei->kwei_errex = KESYSTEM;
      } else {
        pkwei->kwei_errex = KESYNTAX;
      }
      pkwei->kwei_status = KSERROR;
      return KSERROR;

    case '\'':
      return KSSUCCESS;

    default: {
      int ret = kout_putc(pkwei->kwei_pout, ch);
      if (ret == -1) {
        pkwei->kwei_errno = errno;
        pkwei->kwei_errex = KESYSTEM;
        pkwei->kwei_status = KSERROR;
        return KSERROR;
      }
    }
    }
  }
}

static kwei_status_t kwei_exec(kwordexp_internal_t *pkwei, const char **argv,
                               FILE *ofp) {
  kwordexp_exec_t exec = pkwei->kwei_pwe->kwe_exec;
  if (exec == NULL)
    exec = kwordexp_exec_default;
  int ret = exec(pkwei->kwei_pwe->kwe_data, argv, ofp);
  if (ret < 0) {
    pkwei->kwei_errno = errno;
    pkwei->kwei_errex = KESYSTEM;
    pkwei->kwei_status = KSERROR;
    return KSERROR;
  }
  pkwei->kwei_pwe->kwe_last_status = ret;
  return KSSUCCESS;
}

static kwei_status_t kwei_parse_var_paren(kwordexp_internal_t *pkwei) {
  kout_t kout_cmd = kout_init(NULL, NULL, 0);
  kwordexp_t kwe_cmd = *pkwei->kwei_pwe;
  kwe_cmd.kwe_wordv = NULL;
  kwe_cmd.kwe_wordc = 0;
  kwordexp_internal_t kwei_cmd =
      kwei_init(&kwe_cmd, pkwei->kwei_pin, &kout_cmd, pkwei->kwei_flags);
  kwei_status_t kret = kwei_parse(&kwei_cmd);
  if (kret != KSSUCCESS) {
    (void)kout_destroy(&kout_cmd);
    return kret;
  }

  int ch;

  // Skip trailing spaces
  do {
    ch = kin_getc(pkwei->kwei_pin);
  } while (isspace(ch));

  if (ch == EOF) {
    if (kin_error(pkwei->kwei_pin)) {
      pkwei->kwei_errno = errno;
      pkwei->kwei_errex = KESYSTEM;
    } else {
      pkwei->kwei_errex = KESYNTAX;
    }
  }

  if (ch != ')') {
    pkwei->kwei_errex = KESYNTAX;
    pkwei->kwei_status = KSERROR;
    (void)kout_destroy(&kout_cmd);
    kwe_free(&kwe_cmd);
    return KSERROR;
  }

  FILE *ofp = kout_getfp(pkwei->kwei_pout);
  if (ofp == NULL) {
    pkwei->kwei_errno = errno;
    pkwei->kwei_errex = KESYSTEM;
    pkwei->kwei_status = KSERROR;
    (void)kout_destroy(&kout_cmd);
    kwe_free(&kwe_cmd);
    return KSERROR;
  }
  kret = kwei_exec(pkwei, kwei_cmd.kwei_pwe->kwe_wordv, ofp);
  (void)kout_destroy(&kout_cmd);
  kwordfree(&kwe_cmd);
  return kret;
}

static const char *kwei_get_ifs(kwordexp_t *pkwe) {
  void *data = pkwe->kwe_data;
  kwordexp_getenv_t getenv = pkwe->kwe_getenv;
  if (getenv == NULL)
    getenv = kwordexp_getenv_default;
  const char *ifs = getenv(data, "IFS");
  if (ifs == NULL)
    ifs = " \t\n";
  return ifs;
}

static kwei_status_t kwei_push_word(kwordexp_internal_t *pkwei) {
  kwordexp_t *pkwe = pkwei->kwei_pwe;
  size_t wordc = pkwe->kwe_wordc + 1;
  const char **wordv = realloc(pkwe->kwe_wordv, (wordc + 2) * sizeof(char *));
  if (wordv == NULL) {
    pkwei->kwei_errno = errno;
    pkwei->kwei_errex = KESYSTEM;
    pkwei->kwei_status = KSERROR;
    return KSERROR;
  }

  (void)kout_close(pkwei->kwei_pout);
  char *word = pkwei->kwei_pout->kout_obuf;
  pkwei->kwei_pout->kout_obuf = NULL;
  (void)kout_destroy(pkwei->kwei_pout);

  pkwe->kwe_wordv = wordv;
  pkwe->kwe_wordv[wordc] = word;
  pkwe->kwe_wordv[wordc + 1] = NULL;
  pkwe->kwe_wordc = wordc;
  return KSSUCCESS;
}

static kwei_status_t kwei_var_asterisk(kwordexp_internal_t *pkwei) {
  const char *ifs = kwei_get_ifs(pkwei->kwei_pwe);
  for (size_t i = 1; i < pkwei->kwei_pwe->kwe_argc; i++) {
    if (i > 1) {
      int ret = kout_putc(pkwei->kwei_pout, *ifs);
      if (ret == -1) {
        pkwei->kwei_errno = errno;
        pkwei->kwei_errex = KESYSTEM;
        pkwei->kwei_status = KSERROR;
        return KSERROR;
      }
    }
    int ret = kout_printf(pkwei->kwei_pout, "%s", pkwei->kwei_pwe->kwe_argv[i]);
    if (ret == -1) {
      pkwei->kwei_errno = errno;
      pkwei->kwei_errex = KESYSTEM;
      pkwei->kwei_status = KSERROR;
      return KSERROR;
    }
  }
  return KSSUCCESS;
}

static kwei_status_t kwei_var_atto(kwordexp_internal_t *pkwei) {
  for (size_t i = 1; i < pkwei->kwei_pwe->kwe_argc; i++) {
    // separate words
    if (i > 1) {
      kwei_status_t kret = kwei_push_word(pkwei);
      if (kret != KSSUCCESS)
        return kret;

      int ret =
          kout_printf(pkwei->kwei_pout, "%s", pkwei->kwei_pwe->kwe_argv[i]);
      if (ret == -1) {
        pkwei->kwei_errno = errno;
        pkwei->kwei_errex = KESYSTEM;
        pkwei->kwei_status = KSERROR;
        return KSERROR;
      }
    }
  }
  return KSSUCCESS;
}

static kwei_status_t kwei_var_num(kwordexp_internal_t *pkwei, int ch) {
  size_t n = ch - '0';
  if (n >= pkwei->kwei_pwe->kwe_argc) {
    pkwei->kwei_errex = KENARG;
    pkwei->kwei_status = KSERROR;
    return KSERROR;
  }
  int ret = kout_printf(pkwei->kwei_pout, "%s", pkwei->kwei_pwe->kwe_argv[n]);
  if (ret == -1) {
    pkwei->kwei_errno = errno;
    pkwei->kwei_errex = KESYSTEM;
    pkwei->kwei_status = KSERROR;
    return KSERROR;
  }
  return KSSUCCESS;
}

static kwei_status_t kwei_parse_var(kwordexp_internal_t *pkwei) {
  int ch = kin_getc(pkwei->kwei_pin);
  switch (ch) {

  case EOF:
    if (kin_error(pkwei->kwei_pin)) {
      pkwei->kwei_errno = errno;
      pkwei->kwei_errex = KESYSTEM;
    } else {
      pkwei->kwei_errex = KESYNTAX;
    }
    pkwei->kwei_status = KSERROR;
    return KSERROR;

  case '*':
    return kwei_var_asterisk(pkwei);

  case '@':
    return kwei_var_atto(pkwei);

  case '#': {
    int ret =
        kout_printf(pkwei->kwei_pout, "%zu", pkwei->kwei_pwe->kwe_argc - 1);
    if (ret == -1) {
      pkwei->kwei_errno = errno;
      pkwei->kwei_errex = KESYSTEM;
      pkwei->kwei_status = KSERROR;
      return KSERROR;
    }
    return KSSUCCESS;
  }
  case '?': {
    int ret =
        kout_printf(pkwei->kwei_pout, "%d", pkwei->kwei_pwe->kwe_last_status);
    if (ret == -1) {
      pkwei->kwei_errno = errno;
      pkwei->kwei_errex = KESYSTEM;
      pkwei->kwei_status = KSERROR;
      return KSERROR;
    }
  }

  case '-': {
    int ret = kout_printf(pkwei->kwei_pout, "$-");
    if (ret == -1) {
      pkwei->kwei_errno = errno;
      pkwei->kwei_errex = KESYSTEM;
      pkwei->kwei_status = KSERROR;
      return KSERROR;
    }
  }

  case '$': {
    int ret = kout_printf(pkwei->kwei_pout, "%d", (int)getpid());
    if (ret == -1) {
      pkwei->kwei_errno = errno;
      pkwei->kwei_errex = KESYSTEM;
      pkwei->kwei_status = KSERROR;
      return KSERROR;
    }
  }

  case '!': {
    int ret =
        kout_printf(pkwei->kwei_pout, "%d", pkwei->kwei_pwe->kwe_last_bgpid);
    if (ret == -1) {
      pkwei->kwei_errno = errno;
      pkwei->kwei_errex = KESYSTEM;
      pkwei->kwei_status = KSERROR;
      return KSERROR;
    }
  }

  case '0': {
    int ret = kout_printf(pkwei->kwei_pout, "%s", pkwei->kwei_pwe->kwe_argv[0]);
    if (ret == -1) {
      pkwei->kwei_errno = errno;
      pkwei->kwei_errex = KESYSTEM;
      pkwei->kwei_status = KSERROR;
      return KSERROR;
    }
  }

  case '_': {
    int ret =
        kout_printf(pkwei->kwei_pout, "%s", pkwei->kwei_pwe->kwe_last_arg);
    if (ret == -1) {
      pkwei->kwei_errno = errno;
      pkwei->kwei_errex = KESYSTEM;
      pkwei->kwei_status = KSERROR;
      return KSERROR;
    }
  }

  case '{':
    return kwei_parse_var_brace(pkwei);

  case '(':
    return kwei_parse_var_paren(pkwei);

  case '1' ... '9':
    return kwei_var_num(pkwei, ch);

  default:
    if (!isalpha(ch)) {
      pkwei->kwei_errex = KESYNTAX;
      pkwei->kwei_status = KSERROR;
      return KSERROR;
    }
    kout_t kout = kout_init(NULL, NULL, 0);
    do {
      int ret = kout_putc(&kout, ch);
      if (ret == EOF) {
        pkwei->kwei_errno = errno;
        pkwei->kwei_errex = KESYSTEM;
        pkwei->kwei_status = KSERROR;
        return KSERROR;
      }
      ch = kin_getc(pkwei->kwei_pin);
      if (ch == EOF && kin_error(pkwei->kwei_pin)) {
        pkwei->kwei_errno = errno;
        pkwei->kwei_errex = KESYSTEM;
        pkwei->kwei_status = KSERROR;
        return KSERROR;
      }
    } while (isalnum(ch) || ch == '_');
    if (ch != EOF) {
      int ret = kin_ungetc(pkwei->kwei_pin, ch);
      if (ret == EOF) {
        pkwei->kwei_errno = errno;
        pkwei->kwei_errex = KESYSTEM;
        pkwei->kwei_status = KSERROR;
        return KSERROR;
      }
    }
    int ret = kout_close(&kout);
    if (ret == -1) {
      pkwei->kwei_errno = errno;
      pkwei->kwei_errex = KESYSTEM;
      pkwei->kwei_status = KSERROR;
      return KSERROR;
    }
    const char *varname = kout.kout_obuf;
    kout.kout_obuf = NULL;
    ret = kout_destroy(&kout);
    if (ret == -1) {
      pkwei->kwei_errno = errno;
      pkwei->kwei_errex = KESYSTEM;
      pkwei->kwei_status = KSERROR;
      return KSERROR;
    }
    kwordexp_getenv_t getenv = pkwei->kwei_pwe->kwe_getenv;
    if (getenv == NULL)
      getenv = kwordexp_getenv_default;
    const char *value = getenv(pkwei->kwei_pwe->kwe_data, varname);
    free((void *)varname);
    if (value == NULL) {
      pkwei->kwei_errno = errno;
      pkwei->kwei_errex = KESYSTEM;
      pkwei->kwei_status = KSERROR;
      return KSERROR;
    }
    ret = kout_printf(pkwei->kwei_pout, "%s", value);
    if (ret == -1) {
      pkwei->kwei_errno = errno;
      pkwei->kwei_errex = KESYSTEM;
      pkwei->kwei_status = KSERROR;
      return KSERROR;
    }
    return KSSUCCESS;
  }
}

static kwei_status_t kwei_parse_dquote(kwordexp_internal_t *pkwei) {
  while (1) {
    int ch = kin_getc(pkwei->kwei_pin);
    switch (ch) {

    case EOF:
      if (kin_error(pkwei->kwei_pin)) {
        pkwei->kwei_errno = errno;
        pkwei->kwei_errex = KESYSTEM;
      } else {
        pkwei->kwei_errex = KESYNTAX;
      }
      pkwei->kwei_status = KSERROR;
      return KSERROR;

    case '"':
      return KSSUCCESS;

    case '$': {
      kwei_status_t kret = kwei_parse_var(pkwei);
      if (kret != KSSUCCESS)
        return kret;
      continue;
    }

    case '\\':
      ch = kin_getc(pkwei->kwei_pin);
      if (ch == EOF) {
        if (kin_error(pkwei->kwei_pin)) {
          pkwei->kwei_errno = errno;
          pkwei->kwei_errex = KESYSTEM;
        } else {
          pkwei->kwei_errex = KESYNTAX;
        }
        pkwei->kwei_status = KSERROR;
        return KSERROR;
      }
      // fallthrough

    default: {
      int ret = kout_putc(pkwei->kwei_pout, ch);
      if (ret == -1) {
        pkwei->kwei_errno = errno;
        pkwei->kwei_errex = KESYSTEM;
        pkwei->kwei_status = KSERROR;
        return KSERROR;
      }
      continue;
    }
    }
  }
}

static kwei_status_t kwei_parse(kwordexp_internal_t *pkwei) {
  while (1) {
    // Skip leading spaces
    int ch = kin_getc(pkwei->kwei_pin);
    switch (ch) {
    case EOF:
      if (kin_error(pkwei->kwei_pin)) {
        pkwei->kwei_errno = errno;
        pkwei->kwei_errex = KESYSTEM;
        pkwei->kwei_status = KSERROR;
        return KSERROR;
      }
      return KSSUCCESS;

    case '\'': {
      // parse single quoted string
      kwei_status_t kret = kwei_parse_squote(pkwei);
      if (kret != KSSUCCESS)
        return kret;
      break;
    }

    case '"': {
      // parse double quoted string
      kwei_status_t kret = kwei_parse_dquote(pkwei);
      if (kret != KSSUCCESS)
        return kret;
      break;
    }

    case '$': {
      // parse variable
      kwei_status_t kret = kwei_parse_var(pkwei);
      if (kret != KSSUCCESS)
        return kret;
      break;
    }

    default:
      if (isspace(ch)) {
        if (pkwei->kwei_inword) {
          kwei_status_t kret = kwei_push_word(pkwei);
          if (kret != KSSUCCESS)
            return kret;
          pkwei->kwei_inword = 0;
        }
        continue;
      }
      if (ch == '\\') {
        ch = kin_getc(pkwei->kwei_pin);
        if (ch == EOF) {
          if (kin_error(pkwei->kwei_pin)) {
            pkwei->kwei_errno = errno;
            pkwei->kwei_errex = KESYSTEM;
          } else {
            pkwei->kwei_errex = KESYNTAX;
          }
          pkwei->kwei_status = KSERROR;
          return KSERROR;
        }
      }
      int ret = kout_putc(pkwei->kwei_pout, ch);
      if (ret == -1) {
        pkwei->kwei_errno = errno;
        pkwei->kwei_errex = KESYSTEM;
        pkwei->kwei_status = KSERROR;
        return KSERROR;
      }
      continue;
    }
  }
}

// ----------------------------------------------------------------
// kwordexp
// ----------------------------------------------------------------

int kfwordexp(FILE *fp, kwordexp_t *pwe, int flags) {
  kwordexp_internal_t kwei;
  kin_t kin = kin_init(fp, NULL, 0);
  kout_t kout = kout_init(NULL, NULL, 0);
  kwei.kwei_pwe = pwe;
  kwei.kwei_pin = &kin;
  kwei.kwei_pout = &kout;
  kwei.kwei_flags = flags;
  kwei.kwei_inword = 0;
  kwei_status_t kret1 = kwei_parse(&kwei);
  kwei_status_t kret2 = kwei_push_word(&kwei);
  kwei_status_t kret3 = kin_destroy(&kin);
  kwei_status_t kret4 = kout_destroy(&kout);
  return kret1 == KSSUCCESS && kret2 == KSSUCCESS && kret3 == KSSUCCESS &&
                 kret4 == KSSUCCESS
             ? 0
             : -1;
}

int kwordexp(const char *ibuf, kwordexp_t *pwe, int flags) {
  kwordexp_internal_t kwei;
  kin_t kin = kin_init(NULL, ibuf, strlen(ibuf));
  kout_t kout = kout_init(NULL, NULL, 0);
  kwei.kwei_pwe = pwe;
  kwei.kwei_pin = &kin;
  kwei.kwei_pout = &kout;
  kwei.kwei_flags = flags;
  kwei.kwei_inword = 0;
  kwei_status_t kret1 = kwei_parse(&kwei);
  kwei_status_t kret2 = kin_destroy(&kin);
  kwei_status_t kret3 = kout_destroy(&kout);
  return kret1 == KSSUCCESS && kret2 == KSSUCCESS && kret3 == KSSUCCESS ? 0
                                                                        : -1;
}

// ----------------------------------------------------------------
// kwordfree
// ----------------------------------------------------------------
void kwordfree(kwordexp_t *pwe) { kwe_free(pwe); }

// ----------------------------------------------------------------
// Default functions
// ----------------------------------------------------------------
int kwordexp_setenv_default(void *data, const char *key, const char *value,
                            int overwrite) {
  (void)data;
  return setenv(key, value, overwrite);
}

const char *kwordexp_getenv_default(void *data, const char *key) {
  (void)data;
  return getenv(key);
}

int kwordexp_exec_default(void *data, const char **argv, FILE *ofp) {
  (void)data;
  int pipefd[2];
  if (pipe(pipefd) == -1)
    return -1;
  pid_t pid = fork();
  if (pid == -1)
    return -1;
  if (pid == 0) {
    close(pipefd[0]);
    if (dup2(pipefd[1], STDOUT_FILENO) == -1)
      exit(EXIT_FAILURE);
    execvp(argv[0], (char *const *)argv);
    exit(EXIT_FAILURE);
  }
  close(pipefd[1]);
  char buf[sysconf(_SC_PAGESIZE)];
  while (1) {
    ssize_t n = read(pipefd[0], buf, sizeof(buf));
    if (n == -1) {
      int err = errno;
      close(pipefd[0]);
      errno = err;
      return -1;
    }
    size_t m = fwrite(buf, 1, n, ofp);
    if (m != (size_t)n) {
      int err = errno;
      close(pipefd[0]);
      errno = err;
      return -1;
    }
  }
  close(pipefd[0]);

  int status;
  while (waitpid(pid, &status, 0) == -1) {
    if (errno != EINTR)
      return -1;
  }
  return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}
