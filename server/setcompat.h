/***********************************************************************
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
#ifndef FC__SETCOMPAT_H
#define FC__SETCOMPAT_H

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#define setcompat_current_name_from_previous(_old_name_) \
  setcompat_S3_2_name_from_S3_1(_old_name_)
#define setcompat_current_val_from_previous(_set_, _old_val_) \
  setcompat_S3_2_val_from_S3_1(_set_, _old_val_)

struct setting;

const char *setcompat_S3_2_name_from_S3_1(const char *old_name);

const char *setcompat_S3_2_val_from_S3_1(struct setting *pset,
                                         const char *val);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif  /* FC__SETCOMPAT_H */
