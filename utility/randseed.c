/***********************************************************************
 Freeciv - Copyright (C) 2021 The Freeciv Team
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
***********************************************************************/

/* Random number initialization, possibly system-dependent */

#ifdef HAVE_CONFIG_H
#include <fc_config.h>
#endif

#include "fc_prehdrs.h"

#ifdef HAVE_BCRYPT_H
#include <bcrypt.h>
#endif
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <string.h>
#if defined(HAVE_SYS_RANDOM_H) && defined(HAVE_GETENTROPY)
#include <sys/random.h>
#endif
#include <sys/stat.h>
#include <time.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#ifdef FREECIV_MSWINDOWS
#include <ntdef.h>
#endif

/* utility */
#include "fcintl.h"
#include "log.h"

#include "randseed.h"

/**********************************************************************//**
  Read a 32-bit random value using getentropy(), if available.
  (Linux, FreeBSD, OpenBSD, macOS)
  Return FALSE on error, otherwise return TRUE and store seed in *ret.
**************************************************************************/
static bool generate_seed_getentropy(randseed *ret)
{
#if HAVE_GETENTROPY
  /* getentropy() is from OpenBSD, and should be supported on at least
   * FreeBSD and glibc on Linux (as a wrapper to getrandom()) as well.
   */
  randseed seed = 0;

  if (getentropy(&seed, sizeof(seed)) == 0) {
    *ret = seed;

    return TRUE;
  } else {
    log_error(_("getentropy() failed: %s"), strerror(errno));
  }
#endif /* HAVE_GETENTROPY */

  return FALSE;
}

/**********************************************************************//**
  Read a 32-bit random value using BCryptGenRandom(), if available.
  (Windows)
  Return FALSE on error, otherwise return TRUE and store seed in *ret.
**************************************************************************/
static bool generate_seed_bcryptgenrandom(randseed *ret)
{
#ifdef HAVE_BCRYPTGENRANDOM
  NTSTATUS Status;

  Status = BCryptGenRandom(nullptr, (PUCHAR)ret, sizeof(randseed),
                           BCRYPT_USE_SYSTEM_PREFERRED_RNG);

  if (NT_SUCCESS(Status)) {
    return TRUE;
  }
#endif /* HAVE_BCRYPTGENRANDOM */

  return FALSE;
}

/**********************************************************************//**
  Read a 32-bit random value using /dev/urandom, if available.
  (Most Unix-like systems)
  Return FALSE on error, otherwise return TRUE and store seed in *ret.
**************************************************************************/
static bool generate_seed_urandom(randseed *ret)
{
#if HAVE_USABLE_URANDOM
  /*
   * /dev/urandom should be available on most Unixen. The Wikipedia page
   * mentions Linux, FreeBSD, OpenBSD, macOS as well as Solaris, NetBSD,
   * Tru64 UNIX 5.1B, AIX 5.2 and HP-UX 11i v2.
   *
   * However, opening it may fail if the game is running in a chroot without
   * it, or is otherwise restricted from accessing it. This is why getentropy()
   * is preferred.
   */
  static const char *random_device = "/dev/urandom";
  int fd = 0;
  randseed seed = 0;
  bool ok = FALSE;

  /* stdio would read an unnecessarily large block, which may mess up users
   * /dev/random on Linux, so use the low-level calls instead. */
  fd = open(random_device, O_RDONLY);
  if (fd == -1) {
    /* Only warning as we fallback to other randseed generation methods */
    log_warn(_("Opening %s failed: %s"), random_device, strerror(errno));
  } else {
    int n = read(fd, &seed, sizeof(seed));

    if (n == -1) {
      log_warn(_("Reading %s failed: %s"), random_device, strerror(errno));
    } else if (n != sizeof(seed)) {
      log_warn(_("Reading %s: short read without error"), random_device);
    } else {
      ok = 1;
    }
    close(fd);
  }
  if (ok) {
    *ret = seed;

    return TRUE;
  }
#endif /* HAVE_USABLE_URANDOM */

  return FALSE;
}

/**********************************************************************//**
  Generate a 32-bit random-ish value from the current time, using
  clock_gettime(), if available. (POSIX-compatible systems.)
  Return FALSE on error, otherwise return TRUE and store seed in *ret.
**************************************************************************/
static bool generate_seed_clock_gettime(randseed *ret)
{
#if HAVE_CLOCK_GETTIME
  /*
   * clock_gettime() nominally gives nanoseconds, but the real granularity may be
   * worse, making the lowest bits less useful. On the other hand, the lower bits
   * of full seconds are the most useful, the high bits being very predictable.
   * Xor them together to hopefully get something relatively unpredictable in the
   * bottom 30 bits.
   */
  randseed seed = 0;
  struct timespec tp;

  if (clock_gettime(CLOCK_REALTIME, &tp) == 0) {
    seed  = tp.tv_nsec;
    seed ^= tp.tv_sec;
    *ret = seed;

    return TRUE;
  } else {
    /* This shouldn't fail if the function is supported on the system */
    log_error(_("clock_gettime(CLOCK_REALTIME) failed: %s"), strerror(errno));
  }
#endif /* HAVE_CLOCK_GETTIME */

  return FALSE;
}

/**********************************************************************//**
  Generate a lousy 32-bit random-ish value from the current time.
  Return TRUE and store seed in *ret.
**************************************************************************/
static bool generate_seed_time(randseed *ret)
{
  /* No reasonable way this can fail */
  *ret = (randseed) time(nullptr);

  return TRUE;
}

/**********************************************************************//**
  Return a random 32-bit value to use as game seed, by whatever means
  the underlying system provides.
**************************************************************************/
randseed generate_game_seed(void)
{
  randseed seed = 0;

  /* Good random sources */
  if (generate_seed_getentropy(&seed)) {
    log_debug("Got random seed from getentropy()");
    return seed;
  }
  if (generate_seed_bcryptgenrandom(&seed)) {
    log_debug("Got random seed from BCryptGenRandom()");
    return seed;
  }
  if (generate_seed_urandom(&seed)) {
    log_debug("Got random seed from urandom()");
    return seed;
  }

  /* Not so good random source */
  log_normal(_("No good random source usable. Falling back to time-based random seeding."));

  if (generate_seed_clock_gettime(&seed)) {
    log_debug("Got random seed with clock_gettime()");

    return seed;
  }

  /* Lousy last-case source */
  log_warn(_("Falling back to predictable random seed from current coarse-granularity time."));
  generate_seed_time(&seed);
  log_debug("Got random seed from time()");

  return seed;
}
