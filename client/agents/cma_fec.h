/********************************************************************** 
 Freeciv - Copyright (C) 2001 - R. Falke
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
***********************************************************************/

#ifndef FC__CMAFEC_H
#define FC__CMAFEC_H

#include "cma_core.h"

void cmafec_init(void);

void cmafec_set_fe_parameter(struct city *pcity,
			     const struct cma_parameter *const parameter);
void cmafec_get_fe_parameter(struct city *pcity,
			     struct cma_parameter *dest);

const char *const cmafec_get_short_descr(const struct cma_parameter *const
					 parameter);
const char *const cmafec_get_short_descr_of_city(struct city *pcity);
const char *const cmafec_get_result_descr(struct city *pcity,
					  const struct cma_result *const
					  result,
					  const struct cma_parameter *const
					  parameter);

/*
 * Preset handling
 */
void cmafec_preset_add(char *descr_name, struct cma_parameter *pparam);
void cmafec_preset_remove(int index);
int cmafec_preset_get_index_of_parameter(const struct cma_parameter
					 *const parameter);
char *cmafec_preset_get_descr(int index);
const struct cma_parameter *const cmafec_preset_get_parameter(int index);
int cmafec_preset_num(void);

#endif
