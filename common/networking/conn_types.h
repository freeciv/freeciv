/***********************************************************************
 Freeciv - Copyright (C) 2023 - The Freeciv Team
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
***********************************************************************/
#ifndef FC__CONN_TYPES_H
#define FC__CONN_TYPES_H

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* common/networking/ related type definitions.
 *
 * Like common/fc_types.h,
 * this file serves to reduce the cross-inclusion of header files which
 * occurs when a type which is defined in one file is needed for a function
 * definition in another file.
 */

/* Used in the network protocol. */
#define MAX_LEN_PACKET   4096
#define MAX_LEN_CAPSTR    512
#define MAX_LEN_PASSWORD  512 /* Do not change this under any circumstances */
#define MAX_LEN_CONTENT  (MAX_LEN_PACKET - 20)

#define MAX_LEN_BUFFER   (MAX_LEN_PACKET * 128)

/* Used in network protocol. */
#define MAX_LEN_MSG             1536
#define MAX_LEN_ROUTE           2000      /* MAX_LEN_PACKET / 2 - header */

/* Used in network protocol. */
enum authentication_type {
  AUTH_LOGIN_FIRST,   /* Request a password for a returning user */
  AUTH_NEWUSER_FIRST, /* Request a password for a new user */
  AUTH_LOGIN_RETRY,   /* Inform the client to try a different password */
  AUTH_NEWUSER_RETRY  /* Inform the client to try a different [new] password */
};

/* Used in network protocol. */
enum report_type {
  REPORT_WONDERS_OF_THE_WORLD,
  REPORT_WONDERS_OF_THE_WORLD_LONG,
  REPORT_TOP_CITIES,
  REPORT_DEMOGRAPHIC,
  REPORT_ACHIEVEMENTS
};

/* The size of opaque (void *) data sent in the network packet. To avoid
 * fragmentation issues, this SHOULD NOT be larger than the standard
 * ethernet or PPP 1500 byte frame size (with room for headers).
 *
 * Do not spend much time optimizing, you have no idea of the actual dynamic
 * path characteristics between systems, such as VPNs and tunnels.
 *
 * Used in network protocol.
 */
#define ATTRIBUTE_CHUNK_SIZE    (1400)

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* FC__CONN_TYPES_H */
