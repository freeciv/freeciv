/********************************************************************** 
 Freeciv - Copyright (C) 2003 - The Freeciv Project
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
***********************************************************************/
#ifndef FC__PF_TOOLS_H
#define FC__PF_TOOLS_H

#include "path_finding.h"

struct pf_path *pft_concat(struct pf_path *dest_path,
			   const struct pf_path *src_path);
void pft_fill_default_parameter(struct pf_parameter *parameter);
void pft_fill_unit_parameter(struct pf_parameter *parameter,
			     struct unit *punit);
void pft_fill_unit_overlap_param(struct pf_parameter *parameter,
				 struct unit *punit);

/*
 * Below iterator is mostly for use by AI, iterates through all positions
 * on the map, reachable by punit, in order of their distance from punit.
 * Returns info about these positions via the second field.
 */
#define simple_unit_path_iterator(punit, position) {                          \
  struct pf_map *UPI_map;                                                     \
  struct pf_parameter UPI_parameter;                                          \
                                                                              \
  pft_fill_default_parameter(&UPI_parameter);                                 \
  pft_fill_unit_parameter(&UPI_parameter, punit);                             \
  UPI_map = pf_create_map(&UPI_parameter);                                    \
  while (pf_next(UPI_map)) {                                                  \
    struct pf_position position;                                              \
                                                                              \
    pf_next_get_position(UPI_map, &position);

#define simple_unit_path_iterator_end                                         \
  }                                                                           \
  pf_destroy_map(UPI_map);                                                    \
}

/* 
 * Below iterator is to be used when a land unit needs to consider going one
 * step into the sea (to consider boarding, say) or a sea unit needs to
 * consider going one step into the land (land bombardment)
 */
#define simple_unit_overlap_path_iterator(punit, position) {                  \
  struct pf_map *UPI_map;                                                     \
  struct pf_parameter UPI_parameter;                                          \
                                                                              \
  pft_fill_unit_overlap_param(&UPI_parameter, punit);                         \
  UPI_map = pf_create_map(&UPI_parameter);                                    \
  while (pf_next(UPI_map)) {                                                  \
    struct pf_position position;                                              \
                                                                              \
    pf_next_get_position(UPI_map, &position);

#define simple_unit_overlap_path_iterator_end                                 \
  }                                                                           \
  pf_destroy_map(UPI_map);                                                    \
}

#endif				/* FC__PF_TOOLS_H */
