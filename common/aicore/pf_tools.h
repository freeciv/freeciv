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
void pft_fill_unit_parameter(struct pf_parameter *parameter,
			     struct unit *punit);
void pft_fill_unit_overlap_param(struct pf_parameter *parameter,
				 struct unit *punit);
void pft_fill_unit_attack_param(struct pf_parameter *parameter,
                                struct unit *punit);
enum tile_behavior no_fights_or_unknown(const struct tile *ptile,
                                        enum known_type known,
                                        struct pf_parameter *param);
enum tile_behavior no_fights(const struct tile *ptile, enum known_type known,
			     struct pf_parameter *param);

#define pf_iterator(map, position) {                       \
  struct pf_position position;                             \
  while (pf_next(map)) {                                   \
    pf_next_get_position(map, &position);

#define pf_iterator_end }}

#endif				/* FC__PF_TOOLS_H */
