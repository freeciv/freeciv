/********************************************************************** 
 Freeciv - Copyright (C) 1996 - A Kjeldberg, L Gregersen, P Unold
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
***********************************************************************/
#ifndef FC__VERSION_H
#define FC__VERSION_H

#ifdef HAVE_CONFIG_H
#ifndef FC_CONFIG_H		/* this should be defined in config.h */
#error Files including versions.h should also include config.h directly
#endif
#endif

/* The following is for the benefit (?) of non-configure make methods: */
#ifndef MAJOR_VERSION
#define MAJOR_VERSION  1
#endif
#ifndef MINOR_VERSION
#define MINOR_VERSION  8
#endif
#ifndef PATCH_VERSION
#define PATCH_VERSION  1
#endif
#ifndef VERSION_LABEL
#define VERSION_LABEL  "-devel"
#endif
#ifndef IS_BETA_VERSION
#define IS_BETA_VERSION 0
#endif
#ifndef VERSION_STRING
#define VER_STRINGIFY1(x) #x
#define VER_STRINGIFY(x) VER_STRINGIFY1(x)
#define VERSION_STRING VER_STRINGIFY(MAJOR_VERSION) "." \
                       VER_STRINGIFY(MINOR_VERSION) "." \
                       VER_STRINGIFY(PATCH_VERSION) VERSION_LABEL
#endif

/* below, FREECIV_NAME_VERSION uses pre-processor string concatenation --dwp */
#if IS_BETA_VERSION
#define FREECIV_NAME_VERSION "Freeciv version " VERSION_STRING " (beta release)"
#define WORD_VERSION "betatest version"
#else
#define FREECIV_NAME_VERSION "Freeciv version " VERSION_STRING
#define WORD_VERSION "version"
#endif

#endif  /* FC__VERSION_H */
