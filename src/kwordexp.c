#include "kwordexp.h"
#include "kstr.h"
#include "kstrs.h"
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#define KWORDEXP_PARSE_ERROR -1
#define KWORDEXP_PARSE_SUCCESS 0

/**
 * Free the memory allocated by kwordexp.
 *
 * @param we The kwordexp_t object.
 * @return 0 on success, -1 on error.
 */
static int kwordfree_internal(kwordexp_t *we) {
  if (we->kwe_wordv) {
    for (size_t i = 0; i < we->kwe_wordc; i++) {
      free((void *)we->kwe_wordv[i]);
    }
    free(we->kwe_wordv);
  }
  return 0;
}

/**
 * Parse single quoted string.
 *
 * @param pstr Pointer to the string to parse.
 * @param kstr The kstr_t object to store the parsed string.
 * @return KWORDEXP_PARSE_WORD on success, KWORDEXP_PARSE_ERROR on error.
 */
static int kwordexp_parse_squote(const char **pstr, kstrs_t *kstrs) {
  const char *str = *pstr;
  int ret;
  while (1) {
    int ch = *str;
    switch (ch) {

    case '\0':
      return KWORDEXP_PARSE_ERROR;

    case '\'':
      *pstr = ++str;
      return KWORDEXP_PARSE_SUCCESS;

    default:
      ret = kstr_put(&kstrs->kstr, str, 1);
      if (ret == -1)
        return KWORDEXP_PARSE_ERROR;
      str++;
    }
  }
}

static int kwordexp_exec(const char **pstr, kstr_t *kstr, kstrs_t *kstrs,
                         kwordexp_t *we) {
  const char *str = *pstr;
  int ret;
  const char **words = kstrs->strv;
  size_t wordc = kstrs->strc;
  char **argv = malloc((wordc + 1) * sizeof(char *));
  if (argv == NULL) {
    return -1;
  }
  for (size_t i = 0; i < wordc; i++) {
    argv[i] = (char *)words[i];
  }
  argv[wordc] = NULL;
  char *output = NULL;
  int status = we->kwe_exec(we->kwe_data, (const char **)argv, &output);
  free(argv);
  if (status == -1) {
    return -1;
  }
  we->last_status = status;
  if (output != NULL) {
    ret = kstr_put(kstr, output, strlen(output));
    free(output);
    if (ret == -1) {
      return -1;
    }
  }
  *pstr = str;
  return 0;
}

static int kwordexp_parse_words(const char **pstr, kstrs_t *kstrs,
                                kwordexp_t *we, int flags);

static int kwordexp_parse_var_paren(const char **pstr, kstr_t *kstr,
                                    kwordexp_t *we, int flags) {
  const char *str = *pstr;
  kstrs_t kstrs_cmd;
  int ret = kstrs_init(&kstrs_cmd);
  if (ret == -1) {
    return KWORDEXP_PARSE_ERROR;
  }
  ret = kwordexp_parse_words(&str, &kstrs_cmd, we, flags);
  if (ret == KWORDEXP_PARSE_ERROR) {
    kstrs_destroy(&kstrs_cmd);
    return KWORDEXP_PARSE_ERROR;
  }
  while (isspace(*str))
    str++;

  if (*str != ')') {
    kstrs_destroy(&kstrs_cmd);
    return KWORDEXP_PARSE_ERROR;
  }
  str++;

  ret = kwordexp_exec(&str, kstr, &kstrs_cmd, we);
  kstrs_destroy(&kstrs_cmd);
  if (ret == -1) {
    return KWORDEXP_PARSE_ERROR;
  }

  *pstr = str;
  return KWORDEXP_PARSE_SUCCESS;
}

static int kwordexp_parse_var(const char **pstr, kstr_t *kstr, kwordexp_t *we,
                              int flags) {
  const char *str = *pstr;
  int ret;
  int ch = *str;
  kstr_t kstr_var;
  switch (ch) {

  case '\0':
    return KWORDEXP_PARSE_ERROR;

  case '{':
    str++;
    return kwordexp_parse_var_brace(&str, kstr, we, flags);

  case '(':
    str++;
    return kwordexp_parse_var_paren(&str, kstr, we, flags);

  case '$': {
    ret = kstr_init(&kstr_var, NULL, 0);
    if (ret == -1) {
      return KWORDEXP_PARSE_ERROR;
    }
    ret = kwordexp_parse_var(&str, &kstr_var, we, flags);
    if (ret == KWORDEXP_PARSE_ERROR) {
      kstr_destroy(kstr_var);
      return KWORDEXP_PARSE_ERROR;
    }
    const char *value = we->kwe_getenv(we->kwe_data, kstr_var.buf);
    kstr_destroy(kstr_var);
    if (value == NULL) {
      return KWORDEXP_PARSE_ERROR;
    }
    ret = kstr_put(kstr, value, strlen(value));
    if (ret == -1) {
      return KWORDEXP_PARSE_ERROR;
    }
    *pstr = str;
    return KWORDEXP_PARSE_SUCCESS;
  }

  case '0' ... '9': {
    // Parse positional parameter
    char *endptr;
    long int index = strtol(str, &endptr, 10);
    if (endptr == str) {
      return KWORDEXP_PARSE_ERROR;
    }
    if (index < 0 || index >= we->kwe_wordc) {
      return KWORDEXP_PARSE_ERROR;
    }
    ret = kstr_put(kstr, we->kwe_wordv[index], strlen(we->kwe_wordv[index]));
    if (ret == -1) {
      return KWORDEXP_PARSE_ERROR;
    }
    str = endptr;
    *pstr = str;
    return KWORDEXP_PARSE_SUCCESS;
  }

  case '*':
  case '@': {
    // Parse all positional parameters
    for (size_t i = 0; i < we->kwe_wordc; i++) {
      ret = kstr_put(kstr, we->kwe_wordv[i], strlen(we->kwe_wordv[i]));
      if (ret == -1) {
        return KWORDEXP_PARSE_ERROR;
      }
      if (i < we->kwe_wordc - 1) {
        ret = kstr_put(kstr, " ", 1);
        if (ret == -1) {
          return KWORDEXP_PARSE_ERROR;
        }
      }
    }
    *pstr = str + 1;
    return KWORDEXP_PARSE_SUCCESS;
  }

  case '?': {
    // Parse the exit status of the last command
    char buf[16];
    snprintf(buf, sizeof(buf), "%d", we->last_status);
    ret = kstr_put(kstr, buf, strlen(buf));
    if (ret == -1) {
      return KWORDEXP_PARSE_ERROR;
    }
    *pstr = str + 1;
    return KWORDEXP_PARSE_SUCCESS;
  }

  case '!': {
    // Parse the process ID of the shell
    char buf[16];
    snprintf(buf, sizeof(buf), "%d", getpid());
    ret = kstr_put(kstr, buf, strlen(buf));
    if (ret == -1) {
      return KWORDEXP_PARSE_ERROR;
    }
    *pstr = str + 1;
    return KWORDEXP_PARSE_SUCCESS;
  }

  case '#': {
    // Parse the number of positional parameters
    char buf[16];
    snprintf(buf, sizeof(buf), "%zu", we->kwe_wordc);
    ret = kstr_put(kstr, buf, strlen(buf));
    if (ret == -1) {
      return KWORDEXP_PARSE_ERROR;
    }
    *pstr = str + 1;
    return KWORDEXP_PARSE_SUCCESS;
  }

  default:
    if (!isalpha(ch) && ch != '_') {
      return KWORDEXP_PARSE_ERROR;
    }
    ret = kstr_put(&kstr_var, str, 1);
    if (ret == -1)
      return KWORDEXP_PARSE_ERROR;
    str++;
    while (1) {
      ch = *str;
      if (!isalnum(ch) && ch != '_')
        break;

      ret = kstr_put(&kstr_var, str, 1);
      if (ret == -1)
        return KWORDEXP_PARSE_ERROR;
      str++;
    }
    const char *value = we->kwe_getenv(we->kwe_data, kstr_var.buf);
    kstr_destroy(kstr_var);
    if (value == NULL) {
      return KWORDEXP_PARSE_ERROR;
    }
    ret = kstr_put(kstr, value, strlen(value));
    if (ret == -1) {
      return KWORDEXP_PARSE_ERROR;
    }
    *pstr = str;
    return KWORDEXP_PARSE_SUCCESS;
  }
}

static int kwordexp_parse_dquote(const char **pstr, kstr_t *kstr) {
  const char *str = *pstr;
  int ret;
  while (1) {
    int ch = *str;
    switch (ch) {

    case '\0':
      return KWORDEXP_PARSE_ERROR;

    case '"':
      str++;
      break;

    case '\\':
      str++;
      if (*str == '\0')
        return KWORDEXP_PARSE_ERROR;

    case '$':
      str++;
      if (*str == '\0')
        return KWORDEXP_PARSE_ERROR;
      ret = kwordexp_parse_var(&str, kstr);

    default:
      ret = kstr_put(kstr, str, 1);
      if (ret == -1)
        return KWORDEXP_PARSE_ERROR;
      str++;
    }
  }
  *pstr = str;
  return KWORDEXP_PARSE_SUCCESS;
}

static int kwordexp_parse_words(const char **pstr, kstrs_t *kstrs,
                                kwordexp_t *we, int flags) {
  const char *str = *pstr;
  int ret = KWORDEXP_PARSE_ERROR;

  while (1) {
    // Skip leading spaces
    while (isspace(*str))
      str++;

    int ch = *str;

    switch (ch) {
    case '\0':
      ret = KWORDEXP_PARSE_SUCCESS;
      break;

    case '\'':
      str++;

      // parse single quoted string
      ret = kwordexp_parse_squote(&str, &kstrs);
      if (ret == KWORDEXP_PARSE_ERROR) {
        return KWORDEXP_PARSE_ERROR;
      }

      // commit the string
      ret = kstrs_push(kstrs);
      if (ret == -1) {
        return KWORDEXP_PARSE_ERROR;
      }

      break;

    case '"':
      str++;

      // parse double quoted string
      ret = kwordexp_parse_dquote(&str, &kstrs);
      if (ret == KWORDEXP_PARSE_ERROR) {
        return KWORDEXP_PARSE_ERROR;
      }

      // commit the string
      ret = kstrs_push(kstrs);
      if (ret == -1) {
        return KWORDEXP_PARSE_ERROR;
      }

      break;

    case '$':
      str++;

      // parse variable
      ret = kwordexp_parse_var(&str, &kstrs);
      if (ret == KWORDEXP_PARSE_ERROR) {
        return KWORDEXP_PARSE_ERROR;
      }

      // commit the string
      ret = kstrs_push(kstrs);
      if (ret == -1) {
        return KWORDEXP_PARSE_ERROR;
      }

      break;

    default:
      if (isspace(ch)) {
        ret = kstrs_push(kstrs);
        if (ret == -1) {
          return KWORDEXP_PARSE_ERROR;
        }
        str++;
        continue;
      }
      ret = kstr_put(&kstrs->kstr, str, 1);
      if (ret == -1) {
        return KWORDEXP_PARSE_ERROR;
      }
      str++;
    }
  }
}

static int kwordexp_internal(const char *str, kwordexp_t *we, int flags) {
  const char *str_save = str;
  kstrs_t kstrs;
  int ret = kstrs_init(&kstrs);
  if (ret == -1) {
    return -1;
  }
  ret = kwordexp_parse_words(&str, &kstrs, we, flags);
  if (ret == KWORDEXP_PARSE_ERROR) {
    kstrs_destroy(&kstrs);
    return -1;
  }
  if (kstrs.kstr.len != 0) {
    ret = kstrs_push(&kstrs);
    if (ret == -1) {
      kstrs_destroy(&kstrs);
      return -1;
    }
  }
  we->kwe_wordv = kstrs.strv;
  we->kwe_wordc = kstrs.strc;
  return 0;
}

// ----------------------------------------------------------------
// kwordexp
// ----------------------------------------------------------------

int kwordexp(const char *str, kwordexp_t *we, int flags) {
  return kwordexp_internal(str, we, flags);
}

// ----------------------------------------------------------------
// kwordfree
// ----------------------------------------------------------------
void kwordfree(kwordexp_t *we) { kwordfree_internal(we); }

// ----------------------------------------------------------------
// Default functions
// ----------------------------------------------------------------
int kwordexp_setenv_default(void *data, const char *key, const char *value,
                            int overwrite) {
  return setenv(key, value, overwrite);
}

const char *kwordexp_getenv_default(void *data, const char *key) {
  return getenv(key);
}

int kwordexp_exec_default(void *data, const char **words, char **result) {
  pid_t pid = fork();
  if (pid == -1)
    return -1;
  if (pid == 0) {
    execvp(words[0], (char *const *)words);
    exit(EXIT_FAILURE);
  }
  int status;
  while (waitpid(pid, &status, 0) == -1) {
    if (errno != EINTR)
      return -1;
  }
  return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}
