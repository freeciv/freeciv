/*-
 * Copyright (c) 1990, 1993
 *      The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Chris Torek.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *      This product includes software developed by the University of
 *      California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/* This file has been created with use of vfprintf.c sources from FreeBSD
distribution. It changed a lot, but the main work remained.

Dirk Stöcker <stoecker@epost.de>
2000-12-28
*/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char * __ultoa(register unsigned long, char *, int, int, char *);
static void __find_arguments(const char *, va_list, void ***);
static int __grow_type_table(int, unsigned char **, int *);

/*
 * Macros for converting digits to letters and vice versa
 */
#define to_digit(c) ((c) - '0')
#define is_digit(c) ((unsigned)to_digit(c) <= 9)
#define to_char(n)  ((n) + '0')

/*
 * Convert an unsigned long to ASCII for printf purposes, returning
 * a pointer to the first character of the string representation.
 * Octal numbers can be forced to have a leading zero; hex numbers
 * use the given digits.
 */
static char * __ultoa(register unsigned long val, char *endp, int base, int octzero, char *xdigs)
{
  register char *cp = endp;
  register long sval;

  /*
   * Handle the three cases separately, in the hope of getting
   * better/faster code.
   */
  switch (base) {
  case 10:
    if (val < 10) { /* many numbers are 1 digit */
      *--cp = to_char(val);
      return (cp);
    }
    /*
     * On many machines, unsigned arithmetic is harder than
     * signed arithmetic, so we do at most one unsigned mod and
     * divide; this is sufficient to reduce the range of
     * the incoming value to where signed arithmetic works.
     */
    if (val > LONG_MAX) {
      *--cp = to_char(val % 10);
      sval = val / 10;
    } else
      sval = val;
    do {
      *--cp = to_char(sval % 10);
      sval /= 10;
    } while (sval != 0);
    break;

  case 8:
    do {
      *--cp = to_char(val & 7);
      val >>= 3;
    } while (val);
    if (octzero && *cp != '0')
      *--cp = '0';
    break;

  case 16:
    do {
      *--cp = xdigs[val & 15];
      val >>= 4;
    } while (val);
    break;

  default:      /* oops */
    abort();
  }
  return (cp);
}

#define BUF   68
#define STATIC_ARG_TBL_SIZE 8           /* Size of static argument table. */

/* Flags used during conversion. */
#define ALT       0x001   /* alternate form */
#define HEXPREFIX 0x002   /* add 0x or 0X prefix */
#define LADJUST   0x004   /* left adjustment */
#define LONGDBL   0x008   /* long double */
#define LONGINT   0x010   /* long integer */
#define SHORTINT  0x040   /* short integer */
#define ZEROPAD   0x080   /* zero (as opposed to blank) pad */

int vsnprintf(char *buffer, int buffersize, const char *fmt0, va_list ap)
{
  register char *fmt;   /* format string */
  register int ch;      /* character from fmt */
  register int n, n2;   /* handy integer (short term usage) */
  register char *cp;    /* handy char pointer (short term usage) */
  register int flags;   /* flags as above */
  int ret;              /* return value accumulator */
  int width;            /* width from format (%8d), or 0 */
  int prec;             /* precision from format (%.3d), or -1 */
  char sign;            /* sign prefix (' ', '+', '-', or \0) */
  unsigned long ulval;  /* integer arguments %[diouxX] */
  int base;             /* base for [diouxX] conversion */
  int dprec;            /* a copy of prec if [diouxX], 0 otherwise */
  int realsz;           /* field size expanded by dprec, sign, etc */
  int size;             /* size of converted field or string */
  int prsize;           /* max size of printed field */
  char *xdigs = 0;      /* digits for [xX] conversion */
  char buf[BUF];        /* space for %c, %[diouxX], %[eEfgG] */
  char ox[2];           /* space for 0x hex-prefix */
  void **argtable;      /* args, built due to positional arg */
  void *statargtable [STATIC_ARG_TBL_SIZE];
  int nextarg;          /* 1-based argument index */
  va_list orgap;        /* original argument pointer */

  /*
   * Choose PADSIZE to trade efficiency vs. size.  If larger printf
   * fields occur frequently, increase PADSIZE and make the initialisers
   * below longer.
   */
#define PADSIZE 16    /* pad chunk size */
  static char blanks[PADSIZE] =
   {' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' '};
  static char zeroes[PADSIZE] =
   {'0','0','0','0','0','0','0','0','0','0','0','0','0','0','0','0'};

  /*
   * BEWARE, these `goto error' on error, and PAD uses `n'.
   */
#define PRINT(ptr, len) {int print_i; for(print_i = 0; print_i < len && buffersize > 1; ++print_i) \
  {*(buffer)++ = (ptr)[print_i]; --buffersize;} }

#define PAD(howmany, with) { \
  if ((n = (howmany)) > 0) { \
    while (n > PADSIZE) { \
      PRINT(with, PADSIZE); \
      n -= PADSIZE; \
    } \
    PRINT(with, n); \
  } \
}

/*
 * Get the argument indexed by nextarg.   If the argument table is
 * built, use it to get the argument.  If its not, get the next
 * argument (and arguments must be gotten sequentially).
 */
#define GETARG(type) ((argtable != NULL) ? *((type*)(argtable[nextarg++])) : (nextarg++, va_arg(ap, type)))

/*
 * To extend shorts properly, we need both signed and unsigned
 * argument extraction methods.
 */
#define SARG() (flags&LONGINT ? GETARG(long) : flags&SHORTINT ? (long)(short)GETARG(int) : (long)GETARG(int))
#define UARG() (flags&LONGINT ? GETARG(unsigned long) : flags&SHORTINT ? (unsigned long)(unsigned short)GETARG(int) : \
               (unsigned long)GETARG(unsigned int))

/*
 * Get * arguments, including the form *nn$.  Preserve the nextarg
 * that the argument can be gotten once the type is determined.
 */
#define GETASTER(val) \
  n2 = 0; \
  cp = fmt; \
  while (is_digit(*cp)) { \
    n2 = 10 * n2 + to_digit(*cp); \
    cp++; \
  } \
  if (*cp == '$') { \
  int hold = nextarg; \
  if (argtable == NULL) { \
    argtable = statargtable; \
    __find_arguments (fmt0, orgap, &argtable); \
  } \
  nextarg = n2; \
  val = GETARG (int); \
  nextarg = hold; \
  fmt = ++cp; \
  } else { \
    val = GETARG (int); \
  }

  fmt = (char *)fmt0;
  argtable = NULL;
  nextarg = 1;
  orgap = ap;
  ret = 0;

  /*
   * Scan the format for conversions (`%' character).
   */
  for (;;) {
    for (cp = fmt; (ch = *fmt) != '\0' && ch != '%'; fmt++)
      /* void */;
    if ((n = fmt - cp) != 0) {
      if ((unsigned)ret + n > INT_MAX) {
        ret = EOF;
        goto error;
      }
      PRINT(cp, n);
      ret += n;
    }
    if (ch == '\0')
      goto done;
    fmt++;    /* skip over '%' */

    flags = 0;
    dprec = 0;
    width = 0;
    prec = -1;
    sign = '\0';

rflag:    ch = *fmt++;
reswitch: switch (ch) {
    case ' ':
      /*
       * ``If the space and + flags both appear, the space
       * flag will be ignored.''
       *  -- ANSI X3J11
       */
      if (!sign)
        sign = ' ';
      goto rflag;
    case '#':
      flags |= ALT;
      goto rflag;
    case '*':
      /*
       * ``A negative field width argument is taken as a
       * - flag followed by a positive field width.''
       *  -- ANSI X3J11
       * They don't exclude field widths read from args.
       */
      GETASTER (width);
      if (width >= 0)
        goto rflag;
      width = -width;
      /* FALLTHROUGH */
    case '-':
      flags |= LADJUST;
      goto rflag;
    case '+':
      sign = '+';
      goto rflag;
    case '.':
      if ((ch = *fmt++) == '*') {
        GETASTER (n);
        prec = n < 0 ? -1 : n;
        goto rflag;
      }
      n = 0;
      while (is_digit(ch)) {
        n = 10 * n + to_digit(ch);
        ch = *fmt++;
      }
      prec = n < 0 ? -1 : n;
      goto reswitch;
    case '0':
      /*
       * ``Note that 0 is taken as a flag, not as the
       * beginning of a field width.''
       *  -- ANSI X3J11
       */
      flags |= ZEROPAD;
      goto rflag;
    case '1': case '2': case '3': case '4':
    case '5': case '6': case '7': case '8': case '9':
      n = 0;
      do {
        n = 10 * n + to_digit(ch);
        ch = *fmt++;
      } while (is_digit(ch));
      if (ch == '$') {
        nextarg = n;
        if (argtable == NULL) {
          argtable = statargtable;
          __find_arguments (fmt0, orgap, &argtable);
        }
        goto rflag;
      }
      width = n;
      goto reswitch;
    case 'h':
      flags |= SHORTINT;
      goto rflag;
    case 'l':
      flags |= LONGINT;
      goto rflag;
    case 'c':
      *(cp = buf) = GETARG(int);
      size = 1;
      sign = '\0';
      break;
    case 'D':
      flags |= LONGINT;
      /*FALLTHROUGH*/
    case 'd':
    case 'i':
      ulval = SARG();
      if ((long)ulval < 0) {
        ulval = -ulval;
        sign = '-';
      }
      base = 10;
      goto number;
    case 'n':
      if (flags & LONGINT)
        *GETARG(long *) = ret;
      else if (flags & SHORTINT)
        *GETARG(short *) = ret;
      else
        *GETARG(int *) = ret;
      continue; /* no output */
    case 'O':
      flags |= LONGINT;
      /*FALLTHROUGH*/
    case 'o':
      ulval = UARG();
      base = 8;
      goto nosign;
    case 'p':
      /*
       * ``The argument shall be a pointer to void.  The
       * value of the pointer is converted to a sequence
       * of printable characters, in an implementation-
       * defined manner.''
       *  -- ANSI X3J11
       */
      ulval = (unsigned long)GETARG(void *);
      base = 16;
      xdigs = "0123456789abcdef";
      flags |= HEXPREFIX;
      ch = 'x';
      goto nosign;
    case 's':
      if ((cp = GETARG(char *)) == NULL)
        cp = "(null)";
      if (prec >= 0) {
        /*
         * can't use strlen; can only look for the
         * NUL in the first `prec' characters, and
         * strlen() will go further.
         */
        char *p = memchr(cp, 0, (size_t)prec);

        if (p != NULL) {
          size = p - cp;
          if (size > prec)
            size = prec;
        } else
          size = prec;
      } else
        size = strlen(cp);
      sign = '\0';
      break;
    case 'U':
      flags |= LONGINT;
      /*FALLTHROUGH*/
    case 'u':
      ulval = UARG();
      base = 10;
      goto nosign;
    case 'X':
      xdigs = "0123456789ABCDEF";
      goto hex;
    case 'x':
      xdigs = "0123456789abcdef";
hex:      ulval = UARG();
      base = 16;
      /* leading 0x/X only if non-zero */
      if (flags & ALT && ulval != 0)
        flags |= HEXPREFIX;

      /* unsigned conversions */
nosign:     sign = '\0';
      /*
       * ``... diouXx conversions ... if a precision is
       * specified, the 0 flag will be ignored.''
       *  -- ANSI X3J11
       */
number:     if ((dprec = prec) >= 0)
        flags &= ~ZEROPAD;

      /*
       * ``The result of converting a zero value with an
       * explicit precision of zero is no characters.''
       *  -- ANSI X3J11
       */
      cp = buf + BUF;
      if (ulval != 0 || prec != 0)
        cp = __ultoa(ulval, cp, base,
            flags & ALT, xdigs);
      size = buf + BUF - cp;
      break;
    default:  /* "%?" prints ?, unless ? is NUL */
      if (ch == '\0')
        goto done;
      /* pretend it was %c with argument ch */
      cp = buf;
      *cp = ch;
      size = 1;
      sign = '\0';
      break;
    }

    /*
     * All reasonable formats wind up here.  At this point, `cp'
     * points to a string which (if not flags&LADJUST) should be
     * padded out to `width' places.  If flags&ZEROPAD, it should
     * first be prefixed by any sign or other prefix; otherwise,
     * it should be blank padded before the prefix is emitted.
     * After any left-hand padding and prefixing, emit zeroes
     * required by a decimal [diouxX] precision, then print the
     * string proper, then emit zeroes required by any leftover
     * floating precision; finally, if LADJUST, pad with blanks.
     *
     * Compute actual size, so we know how much to pad.
     * size excludes decimal prec; realsz includes it.
     */
    realsz = dprec > size ? dprec : size;
    if (sign)
      realsz++;
    else if (flags & HEXPREFIX)
      realsz += 2;

    prsize = width > realsz ? width : realsz;
    if ((unsigned)ret + prsize > INT_MAX) {
      ret = EOF;
      goto error;
    }

    /* right-adjusting blank padding */
    if ((flags & (LADJUST|ZEROPAD)) == 0)
      PAD(width - realsz, blanks);

    /* prefix */
    if (sign) {
      PRINT(&sign, 1);
    } else if (flags & HEXPREFIX) {
      ox[0] = '0';
      ox[1] = ch;
      PRINT(ox, 2);
    }

    /* right-adjusting zero padding */
    if ((flags & (LADJUST|ZEROPAD)) == ZEROPAD)
      PAD(width - realsz, zeroes);

    /* leading zeroes from decimal precision */
    PAD(dprec - size, zeroes);

    /* the string or number proper */
    PRINT(cp, size);
    /* left-adjusting padding (always blank) */
    if (flags & LADJUST)
      PAD(width - realsz, blanks);

    /* finally, adjust ret */
    ret += prsize;
  }
done:
error:
        if ((argtable != NULL) && (argtable != statargtable))
                free (argtable);

  if(buffersize)
    *buffer = 0;

  return (ret);
}

/*
 * Type ids for argument type table.
 */
#define T_UNUSED  0
#define T_SHORT   1
#define T_U_SHORT 2
#define TP_SHORT  3
#define T_INT   4
#define T_U_INT   5
#define TP_INT    6
#define T_LONG    7
#define T_U_LONG  8
#define TP_LONG   9
#define TP_CHAR   15
#define TP_VOID   16

/*
 * Find all arguments when a positional parameter is encountered.  Returns a
 * table, indexed by argument number, of pointers to each arguments.  The
 * initial argument table should be an array of STATIC_ARG_TBL_SIZE entries.
 * It will be replaces with a malloc-ed on if it overflows.
 */ 
static void __find_arguments(const char *fmt0, va_list ap, void ***argtable)
{
  register char *fmt; /* format string */
  register int ch;  /* character from fmt */
  register int n, n2; /* handy integer (short term usage) */
  register char *cp;  /* handy char pointer (short term usage) */
  register int flags; /* flags as above */
  unsigned char *typetable; /* table of types */
  unsigned char stattypetable [STATIC_ARG_TBL_SIZE];
  int tablesize;    /* current size of type table */
  int tablemax;   /* largest used index in table */
  int nextarg;    /* 1-based argument index */

  /*
   * Add an argument type to the table, expanding if necessary.
   */
#define ADDTYPE(type) \
  ((nextarg >= tablesize) ? \
    __grow_type_table(nextarg, &typetable, &tablesize) : 0, \
  typetable[nextarg++] = type, \
  (nextarg > tablemax) ? tablemax = nextarg : 0)

#define ADDSARG() \
  ((flags&LONGINT) ? ADDTYPE(T_LONG) : \
    ((flags&SHORTINT) ? ADDTYPE(T_SHORT) : ADDTYPE(T_INT)))

#define ADDUARG() \
  ((flags&LONGINT) ? ADDTYPE(T_U_LONG) : \
    ((flags&SHORTINT) ? ADDTYPE(T_U_SHORT) : ADDTYPE(T_U_INT)))

  /*
   * Add * arguments to the type array.
   */
#define ADDASTER() \
  n2 = 0; \
  cp = fmt; \
  while (is_digit(*cp)) { \
    n2 = 10 * n2 + to_digit(*cp); \
    cp++; \
  } \
  if (*cp == '$') { \
    int hold = nextarg; \
    nextarg = n2; \
    ADDTYPE (T_INT); \
    nextarg = hold; \
    fmt = ++cp; \
  } else { \
    ADDTYPE (T_INT); \
  }
  fmt = (char *)fmt0;
  typetable = stattypetable;
  tablesize = STATIC_ARG_TBL_SIZE;
  tablemax = 0; 
  nextarg = 1;
  memset (typetable, T_UNUSED, STATIC_ARG_TBL_SIZE);

  /*
   * Scan the format for conversions (`%' character).
   */
  for (;;) {
    for (; (ch = *fmt) != '\0' && ch != '%'; fmt++)
      /* void */;
    if (ch == '\0')
      goto done;
    fmt++;    /* skip over '%' */

    flags = 0;

rflag:    ch = *fmt++;
reswitch: switch (ch) {
    case ' ':
    case '#':
      goto rflag;
    case '*':
      ADDASTER ();
      goto rflag;
    case '-':
    case '+':
      goto rflag;
    case '.':
      if ((ch = *fmt++) == '*') {
        ADDASTER ();
        goto rflag;
      }
      while (is_digit(ch)) {
        ch = *fmt++;
      }
      goto reswitch;
    case '0':
      goto rflag;
    case '1': case '2': case '3': case '4':
    case '5': case '6': case '7': case '8': case '9':
      n = 0;
      do {
        n = 10 * n + to_digit(ch);
        ch = *fmt++;
      } while (is_digit(ch));
      if (ch == '$') {
        nextarg = n;
        goto rflag;
      }
      goto reswitch;
    case 'h':
      flags |= SHORTINT;
      goto rflag;
    case 'l':
      flags |= LONGINT;
      goto rflag;
    case 'c':
      ADDTYPE(T_INT);
      break;
    case 'D':
      flags |= LONGINT;
      /*FALLTHROUGH*/
    case 'd':
    case 'i':
      ADDSARG();
      break;
    case 'n':
      if (flags & LONGINT)
        ADDTYPE(TP_LONG);
      else if (flags & SHORTINT)
        ADDTYPE(TP_SHORT);
      else
        ADDTYPE(TP_INT);
      continue; /* no output */
    case 'O':
      flags |= LONGINT;
      /*FALLTHROUGH*/
    case 'o':
      ADDUARG();
      break;
    case 'p':
      ADDTYPE(TP_VOID);
      break;
    case 's':
      ADDTYPE(TP_CHAR);
      break;
    case 'U':
      flags |= LONGINT;
      /*FALLTHROUGH*/
    case 'u':
      ADDUARG();
      break;
    case 'X':
    case 'x':
      ADDUARG();
      break;
    default:  /* "%?" prints ?, unless ? is NUL */
      if (ch == '\0')
        goto done;
      break;
    }
  }
done:
  /*
   * Build the argument table.
   */
  if (tablemax >= STATIC_ARG_TBL_SIZE)
  {
    *argtable = (void **)
        malloc (sizeof (void *) * (tablemax + 1));
  }

  (*argtable) [0] = NULL;
  for (n = 1; n <= tablemax; n++)
  {
#ifdef __VBCC__
    /* VBCC cannot build address of va_arg, thus we need to do the va_arg
       ourself (assuming all types are 4 byte). */
    (*argtable)[n] = (void *) ap;
    ap += 4;
#else
    switch (typetable [n])
    {
    case T_UNUSED:  (*argtable) [n] = (void *) &va_arg (ap, int); break;
    case T_SHORT:   (*argtable) [n] = (void *) &va_arg (ap, int); break;
    case T_U_SHORT: (*argtable) [n] = (void *) &va_arg (ap, int); break;
    case TP_SHORT:  (*argtable) [n] = (void *) &va_arg (ap, short *); break;
    case T_INT:     (*argtable) [n] = (void *) &va_arg (ap, int); break;
    case T_U_INT:   (*argtable) [n] = (void *) &va_arg (ap, unsigned int); break;
    case TP_INT:    (*argtable) [n] = (void *) &va_arg (ap, int *); break;
    case T_LONG:    (*argtable) [n] = (void *) &va_arg (ap, long); break;
    case T_U_LONG:  (*argtable) [n] = (void *) &va_arg (ap, unsigned long); break;
    case TP_LONG:   (*argtable) [n] = (void *) &va_arg (ap, long *); break;
    case TP_CHAR:   (*argtable) [n] = (void *) &va_arg (ap, char *); break;
    case TP_VOID:   (*argtable) [n] = (void *) &va_arg (ap, void *); break;
    }
#endif
  }

  if ((typetable != NULL) && (typetable != stattypetable))
    free (typetable);
}

/*
 * Increase the size of the type table.
 */
static int __grow_type_table (int nextarg, unsigned char **typetable, int *tablesize)
{
  unsigned char *oldtable = *typetable;
  int newsize = *tablesize * 2;

  if (*tablesize == STATIC_ARG_TBL_SIZE) {
    *typetable = (unsigned char *)
        malloc (sizeof (unsigned char) * newsize);
    memmove(*typetable, oldtable, *tablesize);
  } else {
    *typetable = (unsigned char *)
        realloc(typetable, sizeof (unsigned char) * newsize);

  }
  memset (&typetable [*tablesize], T_UNUSED, (newsize - *tablesize));

  *tablesize = newsize;
  return 0;
}

/*
int snprintf(char *buf, int n, const char *fmt, ...)
{
  int r;
  va_list ap;
  
  va_start(ap, fmt);
  r = vsnprintf(buf, n, fmt, ap);
  va_end(ap);
  return r;
}
*/
