#include "kio_internal.h"
#include "kmalloc_internal.h"
#include "kwordexp_internal.h"
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <gc.h>
#include <glob.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

int kwei_isspace(int ch, const char *ifs) {
  while (*ifs != '\0') {
    if (ch == *ifs)
      return 1;
    ifs++;
  }
  return 0;
}

kwordexp_internal_t kwei_init(kwordexp_t *pkwe, kin_t *pkin, kout_t *pkout,
                              int flags) {
  kwordexp_internal_t kwei;
  kwei.kwei_pwe = pkwe;
  kwei.kwei_pin = pkin;
  kwei.kwei_pout = pkout;
  kwei.kwei_flags = flags;
  kwei.kwei_has_arg = 0;
  kwei.kwei_has_pattern = 0;
  kwei.kwei_errno = 0;
  kwei.kwei_errex = KENONE;
  kwei.kwei_status = KSSUCCESS;
  char *ifs;
  kwei_status_t kstat = kwei_getenv(&kwei, "IFS", &ifs);
  if (kstat != KSSUCCESS || ifs == NULL)
    ifs = " \f\n\r\t\v";
  kwei.kwei_ifs = ifs;
  return kwei;
}

void kwe_init(kwordexp_t *pkwe, char **argv, size_t argc) {
  pkwe->kwe_wordv = NULL;
  pkwe->kwe_wordc = 0;
  pkwe->kwe_argv = argv;
  pkwe->kwe_argc = argc;
  pkwe->kwe_last_status = 0;
  pkwe->kwe_last_bgpid = 0;
  pkwe->kwe_last_arg = NULL;
  pkwe->kwe_exec = NULL;
  pkwe->kwe_getenv = NULL;
  pkwe->kwe_setenv = NULL;
  pkwe->kwe_data = NULL;
}

void kwe_copy(kwordexp_t *pkwe, const kwordexp_t *pother) {
  pkwe->kwe_argv = pother->kwe_argv;
  pkwe->kwe_argc = pother->kwe_argc;
  pkwe->kwe_last_status = pother->kwe_last_status;
  pkwe->kwe_last_bgpid = pother->kwe_last_bgpid;
  pkwe->kwe_last_arg = pother->kwe_last_arg;
  pkwe->kwe_exec = pother->kwe_exec;
  pkwe->kwe_getenv = pother->kwe_getenv;
  pkwe->kwe_setenv = pother->kwe_setenv;
  pkwe->kwe_data = pother->kwe_data;
}

void kwe_free(kwordexp_t *pkwe) {
  if (pkwe->kwe_wordv != NULL) {
    kfree(pkwe->kwe_wordv);
    pkwe->kwe_wordv = NULL;
  }
}

kwei_status_t kwei_parse_squote(kwordexp_internal_t *pkwei) {
  pkwei->kwei_has_arg = 1;
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

kwei_status_t kwei_getenv(kwordexp_internal_t *pkwei, const char *key,
                          char **pvalue) {
  kwordexp_getenv_t getenv = pkwei->kwei_pwe->kwe_getenv;
  if (getenv == NULL)
    getenv = kwordexp_getenv_default;
  int ret = getenv(pkwei->kwei_pwe->kwe_data, key, pvalue);
  if (ret < 0) {
    pkwei->kwei_errno = errno;
    pkwei->kwei_errex = KESYSTEM;
    pkwei->kwei_status = KSERROR;
    return KSERROR;
  }
  return KSSUCCESS;
}

kwei_status_t kwei_exec(kwordexp_internal_t *pkwei, char **argv, FILE *ofp) {
  kwordexp_exec_t exec = pkwei->kwei_pwe->kwe_exec;
  void *data = pkwei->kwei_pwe->kwe_data;
  if (exec == NULL) {
    exec = kwordexp_exec_default;
    data = pkwei;
  }
  int ret = exec(data, argv, ofp);
  if (ret < 0) {
    pkwei->kwei_errno = errno;
    pkwei->kwei_errex = KESYSTEM;
    pkwei->kwei_status = KSERROR;
    return KSERROR;
  }
  pkwei->kwei_pwe->kwe_last_status = ret;
  return KSSUCCESS;
}

kwei_status_t kwei_parse_var_paren(kwordexp_internal_t *pkwei) {
  kout_t *pkout_cmd = kout_open(NULL, NULL, 0);
  if (pkout_cmd == NULL) {
    pkwei->kwei_errno = errno;
    pkwei->kwei_errex = KESYSTEM;
    pkwei->kwei_status = KSERROR;
    return KSERROR;
  }
  kwordexp_t kwe_cmd;
  kwe_init(&kwe_cmd, pkwei->kwei_pwe->kwe_argv, pkwei->kwei_pwe->kwe_argc);
  kwe_copy(&kwe_cmd, pkwei->kwei_pwe);
  kwordexp_internal_t kwei_cmd =
      kwei_init(&kwe_cmd, pkwei->kwei_pin, pkout_cmd, pkwei->kwei_flags);
  kwei_status_t kstat = kwei_parse(&kwei_cmd);
  if (kstat != KSSUCCESS) {
    int ret = kout_close(pkout_cmd, NULL, NULL);
    (void)ret;
    kwe_free(&kwe_cmd);
    return kstat;
  }

  int ch = kin_getc_while(pkwei->kwei_pin, kwei_isspace, pkwei->kwei_ifs);

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
    int ret = kout_close(pkout_cmd, NULL, NULL);
    (void)ret;
    kwe_free(&kwe_cmd);
    return KSERROR;
  }

  FILE *ofp = kout_getfp(pkwei->kwei_pout);
  if (ofp == NULL) {
    pkwei->kwei_errno = errno;
    pkwei->kwei_errex = KESYSTEM;
    pkwei->kwei_status = KSERROR;
    int ret = kout_close(pkout_cmd, NULL, NULL);
    (void)ret;
    kwe_free(&kwe_cmd);
    return KSERROR;
  }
  kstat = kwei_exec(pkwei, kwei_cmd.kwei_pwe->kwe_wordv, ofp);
  int ret = kout_close(pkout_cmd, NULL, NULL);
  (void)ret;
  kwordfree(&kwe_cmd);
  return kstat;
}

kwei_status_t kwei_parse_var_brace(kwordexp_internal_t *pkwei) {
  kout_t *pkout_varname = kout_open(NULL, NULL, 0);
  if (pkout_varname == NULL) {
    pkwei->kwei_errno = errno;
    pkwei->kwei_errex = KESYSTEM;
    pkwei->kwei_status = KSERROR;
    return KSERROR;
  }
  kwordexp_t kwe_varname = *pkwei->kwei_pwe; // TODO: change
  kwe_varname.kwe_wordv = NULL;
  kwe_varname.kwe_wordc = 0;
  kwordexp_internal_t kwei_varname = kwei_init(
      &kwe_varname, pkwei->kwei_pin, pkout_varname, pkwei->kwei_flags);
  kwei_status_t kstat = kwei_parse(&kwei_varname);
  if (kstat != KSSUCCESS) {
    int ret = kout_close(pkout_varname, NULL, NULL);
    (void)ret;
    return kstat;
  }
  int ch = kin_getc_while(pkwei->kwei_pin, kwei_isspace, pkwei->kwei_ifs);

  if (ch == EOF) {
    if (kin_error(pkwei->kwei_pin)) {
      pkwei->kwei_errno = errno;
      pkwei->kwei_errex = KESYSTEM;
    } else {
      pkwei->kwei_errex = KESYNTAX;
    }
  }

  if (ch != '}') {
    pkwei->kwei_errex = KESYNTAX;
    pkwei->kwei_status = KSERROR;
    int ret = kout_close(pkout_varname, NULL, NULL);
    (void)ret;
    kwe_free(&kwe_varname);
    return KSERROR;
  }

  if (kwe_varname.kwe_wordc != 1) {
    pkwei->kwei_errex = KESYNTAX;
    pkwei->kwei_status = KSERROR;
    int ret = kout_close(pkout_varname, NULL, NULL);
    (void)ret;
    kwe_free(&kwe_varname);
    return KSERROR;
  }
  char *varvalue;
  kstat = kwei_getenv(pkwei, kwe_varname.kwe_wordv[0], &varvalue);
  int ret = kout_close(pkout_varname, NULL, NULL);
  (void)ret;
  kwe_free(&kwe_varname);
  if (kstat != KSSUCCESS)
    return kstat;
  if (varvalue == NULL) {
    if (pkwei->kwei_flags & KWRDE_UNDEF) {
      pkwei->kwei_errex = KEUNDEF;
      pkwei->kwei_status = KSERROR;
      return KSERROR;
    }
    return KSSUCCESS;
  }
  ret = kout_printf(pkwei->kwei_pout, "%s", varvalue);
  if (ret == -1) {
    pkwei->kwei_errno = errno;
    pkwei->kwei_errex = KESYSTEM;
    pkwei->kwei_status = KSERROR;
    return KSERROR;
  }
  return KSSUCCESS;
}

kwei_status_t kwei_push_word(kwordexp_internal_t *pkwei) {
  if (!pkwei->kwei_has_arg)
    return KSSUCCESS;

  kwordexp_t *pkwe = pkwei->kwei_pwe;
  size_t wordc = pkwe->kwe_wordc;

  char *word = NULL;
  int ret = kout_close(pkwei->kwei_pout, &word, NULL);
  if (ret == -1) {
    pkwei->kwei_errno = errno;
    pkwei->kwei_errex = KESYSTEM;
    pkwei->kwei_status = KSERROR;
    return KSERROR;
  }
  if (word == NULL) {
    word = kstrdup("");
    if (word == NULL) {
      pkwei->kwei_errno = errno;
      pkwei->kwei_errex = KESYSTEM;
      pkwei->kwei_status = KSERROR;
      return KSERROR;
    }
  }

  glob_t gl;
  int has_pattern = pkwei->kwei_has_pattern;
  if (has_pattern) {
    int ret = glob(word, GLOB_NOCHECK | GLOB_BRACE | GLOB_TILDE, NULL, &gl);
    if (ret != 0)
      has_pattern = 0;
  }

  size_t wordc_add = has_pattern ? gl.gl_pathc : 1;
  char **wordv =
      krealloc(pkwe->kwe_wordv, (wordc + wordc_add + 1) * sizeof(char *));
  if (wordv == NULL) {
    pkwei->kwei_errno = errno;
    pkwei->kwei_errex = KESYSTEM;
    pkwei->kwei_status = KSERROR;
    kfree(word);
    if (has_pattern)
      globfree(&gl);
    return KSERROR;
  }

  if (has_pattern) {
    for (size_t i = 0; i < gl.gl_pathc; i++) {
      wordv[wordc + i] = kstrdup(gl.gl_pathv[i]);
      if (wordv[wordc + i] == NULL) {
        pkwei->kwei_errno = errno;
        pkwei->kwei_errex = KESYSTEM;
        pkwei->kwei_status = KSERROR;
        for (size_t j = 0; j < i; j++)
          kfree(wordv[wordc + j]);
        kfree(word);
        if (has_pattern)
          globfree(&gl);
        return KSERROR;
      }
    }
  } else {
    wordv[wordc] = word;
  }

  pkwe->kwe_wordv = wordv;
  pkwe->kwe_wordv[wordc + wordc_add] = NULL;
  pkwe->kwe_wordc = wordc + wordc_add;
  pkwei->kwei_has_arg = 0;
  pkwei->kwei_has_pattern = 0;
  if (has_pattern)
    globfree(&gl);
  return KSSUCCESS;
}

kwei_status_t kwei_var_asterisk(kwordexp_internal_t *pkwei) {
  pkwei->kwei_has_arg = 1;
  const char *ifs = pkwei->kwei_ifs;
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

kwei_status_t kwei_var_atto(kwordexp_internal_t *pkwei) {
  for (size_t i = 1; i < pkwei->kwei_pwe->kwe_argc; i++) {
    // separate words
    if (i > 1) {
      kwei_status_t kstat = kwei_push_word(pkwei);
      if (kstat != KSSUCCESS)
        return kstat;
    }

    pkwei->kwei_has_arg = 1;
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

kwei_status_t kwei_var_num(kwordexp_internal_t *pkwei, int ch) {
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

kwei_status_t kwei_parse_var(kwordexp_internal_t *pkwei) {
  pkwei->kwei_has_arg = 1;
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
    return KSSUCCESS;
  }

  case '-': {
    int ret = kout_printf(pkwei->kwei_pout, "$-");
    if (ret == -1) {
      pkwei->kwei_errno = errno;
      pkwei->kwei_errex = KESYSTEM;
      pkwei->kwei_status = KSERROR;
      return KSERROR;
    }
    return KSSUCCESS;
  }

  case '$': {
    int ret = kout_printf(pkwei->kwei_pout, "%d", (int)getpid());
    if (ret == -1) {
      pkwei->kwei_errno = errno;
      pkwei->kwei_errex = KESYSTEM;
      pkwei->kwei_status = KSERROR;
      return KSERROR;
    }
    return KSSUCCESS;
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
    return KSSUCCESS;
  }

  case '0': {
    int ret = kout_printf(pkwei->kwei_pout, "%s", pkwei->kwei_pwe->kwe_argv[0]);
    if (ret == -1) {
      pkwei->kwei_errno = errno;
      pkwei->kwei_errex = KESYSTEM;
      pkwei->kwei_status = KSERROR;
      return KSERROR;
    }
    return KSSUCCESS;
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
    return KSSUCCESS;
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
    kout_t *pkout = kout_open(NULL, NULL, 0);
    if (pkout == NULL) {
      pkwei->kwei_errno = errno;
      pkwei->kwei_errex = KESYSTEM;
      pkwei->kwei_status = KSERROR;
      return KSERROR;
    }
    do {
      int ret = kout_putc(pkout, ch);
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
    char *varname;
    int ret = kout_close(pkout, &varname, NULL);
    if (ret == -1) {
      pkwei->kwei_errno = errno;
      pkwei->kwei_errex = KESYSTEM;
      pkwei->kwei_status = KSERROR;
      return KSERROR;
    }
    char *varvalue;
    kwei_status_t kstat = kwei_getenv(pkwei, varname, &varvalue);
    ksfree((void *)varname);
    if (kstat != KSSUCCESS) {
      pkwei->kwei_errno = errno;
      pkwei->kwei_errex = KESYSTEM;
      pkwei->kwei_status = KSERROR;
      return KSERROR;
    }
    if (varvalue == NULL) {
      if (pkwei->kwei_flags & KWRDE_UNDEF) {
        pkwei->kwei_errex = KEUNDEF;
        pkwei->kwei_status = KSERROR;
        return KSERROR;
      }
      return KSSUCCESS;
    }
    ret = kout_printf(pkwei->kwei_pout, "%s", varvalue);
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
  pkwei->kwei_has_arg = 1;
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
      kwei_status_t kstat = kwei_parse_var(pkwei);
      if (kstat != KSSUCCESS)
        return kstat;
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

kwei_status_t kwei_parse_internal(kwordexp_internal_t *pkwei) {
  int brace_level = 0;
  int bracket_level = 0;
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
      kwei_status_t kstat = kwei_parse_squote(pkwei);
      if (kstat != KSSUCCESS)
        return kstat;
      break;
    }

    case '"': {
      // parse double quoted string
      kwei_status_t kstat = kwei_parse_dquote(pkwei);
      if (kstat != KSSUCCESS)
        return kstat;
      break;
    }

    case '$': {
      // parse variable
      kwei_status_t kstat = kwei_parse_var(pkwei);
      if (kstat != KSSUCCESS)
        return kstat;
      break;
    }

    default:
      if (kwei_isspace(ch, pkwei->kwei_ifs)) {
        kwei_status_t kstat = kwei_push_word(pkwei);
        if (kstat != KSSUCCESS)
          return kstat;
        continue;
      }
      int esc = 0;
      if (ch == '\\') {
        esc = 1;
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
      if (ch == '[' || (bracket_level > 0 && ch == ']') || ch == '{' ||
          (brace_level > 0 && ch == '}') || ch == '~' || ch == '*' ||
          ch == '?') {
        if (ch == '[')
          bracket_level++;
        if (ch == ']')
          bracket_level--;
        if (ch == '{')
          brace_level++;
        if (ch == '}')
          brace_level--;
        if (!esc)
          pkwei->kwei_has_pattern = 1;
        pkwei->kwei_has_arg = 1;
        int ret = kout_putc(pkwei->kwei_pout, ch);
        if (ret == -1) {
          pkwei->kwei_errno = errno;
          pkwei->kwei_errex = KESYSTEM;
          pkwei->kwei_status = KSERROR;
          return KSERROR;
        }
        continue;
      }
      if (isprint(ch) && ch != '}' && ch != ']') {
        pkwei->kwei_has_arg = 1;
        int ret = kout_putc(pkwei->kwei_pout, ch);
        if (ret == -1) {
          pkwei->kwei_errno = errno;
          pkwei->kwei_errex = KESYSTEM;
          pkwei->kwei_status = KSERROR;
          return KSERROR;
        }
        continue;
      }
      int ret = kin_ungetc(pkwei->kwei_pin, ch);
      if (ret == EOF) {
        pkwei->kwei_errno = errno;
        pkwei->kwei_errex = KESYSTEM;
        pkwei->kwei_status = KSERROR;
        return KSERROR;
      }
      return KSSUCCESS;
    }
  }
}

kwei_status_t kwei_parse(kwordexp_internal_t *pkwei) {
  kwei_status_t kstat = kwei_parse_internal(pkwei);
  if (kstat != KSSUCCESS)
    return kstat;
  kstat = kwei_push_word(pkwei);
  if (kstat != KSSUCCESS)
    return kstat;
  return KSSUCCESS;
}

// ----------------------------------------------------------------
// kwordexp
// ----------------------------------------------------------------

int kfwordexp(FILE *fp, kwordexp_t *pwe, int flags) {

  kin_t *pkin = kin_open(fp, NULL, 0);
  if (pkin == NULL)
    return -1;
  kout_t *pkout = kout_open(NULL, NULL, 0);
  if (pkout == NULL) {
    kin_close(pkin);
    return -1;
  }
  kwordexp_internal_t kwei = kwei_init(pwe, pkin, pkout, flags);
  kwei_status_t kstat = kwei_parse(&kwei);
  kin_close(pkin);
  int ret = kout_close(pkout, NULL, NULL);
  (void)ret;
  if (kstat != KSSUCCESS) {
    kwe_free(pwe);
    return -1;
  }
  return 0;
}

int kwordexp(const char *ibuf, kwordexp_t *pwe, int flags) {
  kin_t *pkin = kin_open(NULL, ibuf, strlen(ibuf));
  if (pkin == NULL)
    return -1;
  kout_t *pkout = kout_open(NULL, NULL, 0);
  if (pkout == NULL) {
    kin_close(pkin);
    return -1;
  }
  kwordexp_internal_t kwei = kwei_init(pwe, pkin, pkout, flags);
  kwei_status_t kstat = kwei_parse(&kwei);
  kin_close(pkin);
  int ret = kout_close(pkout, NULL, NULL);
  (void)ret;
  if (kstat != KSSUCCESS) {
    kwe_free(pwe);
    return -1;
  }
  return 0;
}

// ----------------------------------------------------------------
// kwordfree
// ----------------------------------------------------------------
void kwordfree(kwordexp_t *pwe) { kwe_free(pwe); }

void kwordexp_init(kwordexp_t *pwe, char **argv, size_t argc) {
  kwe_init(pwe, argv, argc);
}

// ----------------------------------------------------------------
// Default functions
// ----------------------------------------------------------------
int kwordexp_setenv_default(void *data, const char *key, char *value,
                            int overwrite) {
  (void)data;
  return setenv(key, value, overwrite);
}

int kwordexp_getenv_default(void *data, const char *key, char **pvalue) {
  (void)data;
  *pvalue = getenv(key);
  return 0;
}

int kwordexp_exec_default(void *data, char **argv, FILE *ofp) {
  kwordexp_internal_t *pkwei = data;
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
    if (!(pkwei->kwei_flags & KWRDE_SHOWERR)) {
      close(STDERR_FILENO);
      open("/dev/null", O_WRONLY);
    }
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
    if (n == 0)
      break;
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
