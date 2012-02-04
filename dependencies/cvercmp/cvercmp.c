/********************************************************
*                                                       *
* (c) 2011 Marko Lindqvist                              *
*                                                       *
* Licensed under Gnu General Public License version 2   *
*                                                       *
********************************************************/

#ifdef HAVE_CONFIG_H
#include <fc_config.h>
#endif

#include <assert.h>
#include <ctype.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "cvercmp.h"

static char **cvercmp_ver_tokenize(const char *ver);
static int cvercmp_next_token(const char *str);
static char **cvercmp_ver_subtokenize(const char *ver);
static int cvercmp_next_subtoken(const char *str);

static enum cvercmp_prever cvercmp_parse_prever(const char *ver);

enum cvercmp_prever
{
  CVERCMP_PRE_ALPHA,
  CVERCMP_PRE_BETA,
  CVERCMP_PRE_PRE,
  CVERCMP_PRE_RC,
  CVERCMP_PRE_NONE
};

bool cvercmp(const char *ver1, const char *ver2, enum cvercmp_type type)
{
  typedef bool (*cmpfunc)(const char *ver1, const char *ver2);

  cmpfunc cmpfuncs[] =
  {
    cvercmp_equal,
    cvercmp_nonequal,
    cvercmp_greater,
    cvercmp_lesser,
    cvercmp_min,
    cvercmp_max
  };

  return cmpfuncs[type](ver1, ver2);
}

bool cvercmp_equal(const char *ver1, const char *ver2)
{
  return cvercmp_cmp(ver1, ver2) == CVERCMP_EQUAL;
}

bool cvercmp_nonequal(const char *ver1, const char *ver2)
{
  return cvercmp_cmp(ver1, ver2) != CVERCMP_EQUAL;
}

bool cvercmp_greater(const char *ver1, const char *ver2)
{
  return cvercmp_cmp(ver1, ver2) == CVERCMP_GREATER;
}

bool cvercmp_lesser(const char *ver1, const char *ver2)
{
  return cvercmp_cmp(ver1, ver2) == CVERCMP_LESSER;
}

bool cvercmp_min(const char *ver1, const char *ver2)
{
  return cvercmp_cmp(ver1, ver2) != CVERCMP_LESSER;
}

bool cvercmp_max(const char *ver1, const char *ver2)
{
  return cvercmp_cmp(ver1, ver2) != CVERCMP_GREATER;
}

enum cvercmp_type cvercmp_cmp(const char *ver1, const char *ver2)
{
  enum cvercmp_type result = CVERCMP_EQUAL;
  bool solution = false;
  int i;
  char **tokens1 = cvercmp_ver_tokenize(ver1);
  char **tokens2 = cvercmp_ver_tokenize(ver2);

  for (i = 0; (tokens1[i] != NULL && tokens2[i] != NULL) && !solution; i++) {
    if (strcasecmp(tokens1[i], tokens2[i])) {
      /* Parts are not equal */
      char **t1 = cvercmp_ver_subtokenize(tokens1[i]);
      char **t2 = cvercmp_ver_subtokenize(tokens2[i]);
      int j;

      for (j = 0; (t1[j] != NULL && t2[j] != NULL) && !solution; j++) {
        bool d1 = isdigit(t1[j][0]);
        bool d2 = isdigit(t2[j][0]);

        if (d1 && !d2) {
          result = CVERCMP_GREATER;
          solution = true;
        } else if (!d1 && d2) {
          result = CVERCMP_LESSER;
          solution = true;
        } else if (d1) {
          int val1 = atoi(t1[j]);
          int val2 = atoi(t2[j]);

          if (val1 > val2) {
            result = CVERCMP_GREATER;
            solution = true;
          } else if (val1 < val2) {
            result = CVERCMP_LESSER;
            solution = true;
          }
        } else {
          int alphacmp = strcasecmp(t1[j], t2[j]);

          if (alphacmp) {
            enum cvercmp_prever pre1 = cvercmp_parse_prever(t1[j]);
            enum cvercmp_prever pre2 = cvercmp_parse_prever(t2[j]);

            if (pre1 > pre2) {
              result = CVERCMP_GREATER;
            } else if (pre1 < pre2) {
              result = CVERCMP_LESSER;
            } else if (alphacmp < 0) {
              result = CVERCMP_LESSER;
            } else if (alphacmp > 0) {
              result = CVERCMP_GREATER;
            } else {
              assert(false);
            }

            solution = true;
          }
        }
      }

      if (!solution) {
        if (t1[j] != NULL && t2[j] == NULL) {
          if (cvercmp_parse_prever(t1[j]) != CVERCMP_PRE_NONE) {
            result = CVERCMP_LESSER;
          } else {
            result = CVERCMP_GREATER;
          }
          solution = true;
        } else if (t1[j] == NULL && t2[j] != NULL) {
          if (cvercmp_parse_prever(t2[j]) != CVERCMP_PRE_NONE) {
            result = CVERCMP_GREATER;
          } else {
            result = CVERCMP_LESSER;
          }
          solution = true;
        }
      }

      for (j = 0; t1[j] != NULL; j++) {
        free(t1[j]);
      }
      for (j = 0; t2[j] != NULL; j++) {
        free(t2[j]);
      }
      free(t1);
      free(t2);
    }
  }

  if (!solution) {
    /* Longer version number is greater if all the parts up to
     * end of shorter one are equal. */
    if (tokens1[i] == NULL && tokens2[i] != NULL) {
      if (cvercmp_parse_prever(tokens2[i]) != CVERCMP_PRE_NONE) {
        result = CVERCMP_GREATER;
      } else {
        result = CVERCMP_LESSER;
      }
    } else if (tokens1[i] != NULL && tokens2[i] == NULL) {
      if (cvercmp_parse_prever(tokens1[i]) != CVERCMP_PRE_NONE) {
        result = CVERCMP_LESSER;
      } else {
        result = CVERCMP_GREATER;
      }
    } else {
      result = CVERCMP_EQUAL;
    }
  }

  for (i = 0; tokens1[i] != NULL; i++) {
    free(tokens1[i]);
  }
  for (i = 0; tokens2[i] != NULL; i++) {
    free(tokens2[i]);
  }
  free(tokens1);
  free(tokens2);

  return result;
}

static char **cvercmp_ver_tokenize(const char *ver)
{
  int num = 0;
  int idx;
  int verlen = strlen(ver);
  char **tokens;
  int i;
  int tokenlen;

  for (idx = 0; idx < verlen; idx += cvercmp_next_token(ver + idx) + 1) {
    num++;
  }

  tokens = calloc(sizeof(char *), num + 1);

  for (i = 0, idx = 0; i < num; i++) {
    tokenlen = cvercmp_next_token(ver + idx);
    tokens[i] = malloc(sizeof(char) * (tokenlen + 1));
    strncpy(tokens[i], ver + idx, tokenlen);
    tokens[i][tokenlen] = '\0';
    idx += tokenlen + 1;
  }

  return tokens;
}

static int cvercmp_next_token(const char *str)
{
  int i;

  for (i = 0;
       str[i] != '\0' && str[i] != '.' && str[i] != '-' && str[i] != '_';
       i++) {
  }

  return i;
}

static char **cvercmp_ver_subtokenize(const char *ver)
{
  int num = 0;
  int idx;
  int verlen = strlen(ver);
  char **tokens;
  int i;
  int tokenlen;

  for (idx = 0; idx < verlen; idx += cvercmp_next_subtoken(ver + idx)) {
    num++;
  }

  tokens = calloc(sizeof(char *), num + 1);

  for (i = 0, idx = 0; i < num; i++) {
    tokenlen = cvercmp_next_subtoken(ver + idx);
    tokens[i] = malloc(sizeof(char) * (tokenlen + 1));
    strncpy(tokens[i], ver + idx, tokenlen);
    tokens[i][tokenlen] = '\0';
    idx += tokenlen + 1;
  }

  return tokens;
}

static int cvercmp_next_subtoken(const char *str)
{
  int i;
  bool alpha;

  if (isdigit(str[0])) {
    alpha = false;
  } else {
    alpha = true;
  }

  for (i = 0; (alpha && !isdigit(str[i])) || (!alpha && isdigit(str[i]));
       i++) {
  }

  return i;
}

static enum cvercmp_prever cvercmp_parse_prever(const char *ver)
{
  if (!strcasecmp(ver, "beta")) {
    return CVERCMP_PRE_BETA;
  }
  if (!strcasecmp(ver, "pre")) {
    return CVERCMP_PRE_PRE;
  }
  if (!strcasecmp(ver, "rc")) {
    return CVERCMP_PRE_RC;
  }
  if (!strcasecmp(ver, "candidate")) {
    return CVERCMP_PRE_RC;
  }
  if (!strcasecmp(ver, "alpha")) {
    return CVERCMP_PRE_ALPHA;
  }

  return CVERCMP_PRE_NONE;
}
