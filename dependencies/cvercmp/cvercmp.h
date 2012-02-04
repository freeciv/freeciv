/********************************************************
*                                                       *
* (c) 2011 Marko Lindqvist                              *
*                                                       *
* Licensed under Gnu General Public License version 2   *
*                                                       *
********************************************************/

#ifndef H_CVERCMP
#define H_CVERCMP

#include <stdbool.h>

enum cvercmp_type
{
  CVERCMP_EQUAL = 0,
  CVERCMP_NONEQUAL,
  CVERCMP_GREATER,
  CVERCMP_LESSER,
  CVERCMP_MIN,
  CVERCMP_MAX
};

bool cvercmp(const char *ver1, const char *ver2, enum cvercmp_type type);
enum cvercmp_type cvercmp_cmp(const char *ver1, const char *ver2);

bool cvercmp_equal(const char *ver1, const char *ver2);
bool cvercmp_nonequal(const char *ver1, const char *ver2);
bool cvercmp_greater(const char *ver1, const char *ver2);
bool cvercmp_lesser(const char *ver1, const char *ver2);
bool cvercmp_min(const char *ver1, const char *ver2);
bool cvercmp_max(const char *ver1, const char *ver2);

#endif /* H_CVERCMP */
