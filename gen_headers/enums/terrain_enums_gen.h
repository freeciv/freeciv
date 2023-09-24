 /**************************************************************************
 *                         THIS FILE WAS GENERATED                         *
 * Script: gen_headers/generate_enums.py                                   *
 * Input:  gen_headers/enums/terrain_enums.def                             *
 *                         DO NOT CHANGE THIS FILE                         *
 **************************************************************************/

#ifndef FC__TERRAIN_ENUMS_GEN_H
#define FC__TERRAIN_ENUMS_GEN_H


#define SPECENUM_NAME terrain_class
#define SPECENUM_VALUE0 TC_LAND
#define SPECENUM_VALUE0NAME N_("Land")
#define SPECENUM_VALUE1 TC_OCEAN
#define SPECENUM_VALUE1NAME N_("Oceanic")
#define SPECENUM_COUNT TC_COUNT
#include "specenum_gen.h"

#define SPECENUM_NAME terrain_alteration
#define SPECENUM_VALUE0 TA_CAN_IRRIGATE
#define SPECENUM_VALUE0NAME N_("CanIrrigate")
#define SPECENUM_VALUE1 TA_CAN_MINE
#define SPECENUM_VALUE1NAME N_("CanMine")
#define SPECENUM_VALUE2 TA_CAN_ROAD
#define SPECENUM_VALUE2NAME N_("CanRoad")
#define SPECENUM_VALUE3 TA_CAN_BASE
#define SPECENUM_VALUE3NAME N_("CanBase")
#define SPECENUM_VALUE4 TA_CAN_PLACE
#define SPECENUM_VALUE4NAME N_("CanPlace")
#define SPECENUM_COUNT TA_COUNT
#include "specenum_gen.h"

#define SPECENUM_NAME terrain_flag_id
#define SPECENUM_VALUE0 TER_NO_BARBS
#define SPECENUM_VALUE0NAME N_("NoBarbs")
#define SPECENUM_VALUE1 TER_NO_CITIES
#define SPECENUM_VALUE1NAME N_("NoCities")
#define SPECENUM_VALUE2 TER_STARTER
#define SPECENUM_VALUE2NAME N_("Starter")
#define SPECENUM_VALUE3 TER_CAN_HAVE_RIVER
#define SPECENUM_VALUE3NAME N_("CanHaveRiver")
#define SPECENUM_VALUE4 TER_UNSAFE_COAST
#define SPECENUM_VALUE4NAME N_("UnsafeCoast")
#define SPECENUM_VALUE5 TER_FRESHWATER
#define SPECENUM_VALUE5NAME N_("FreshWater")
#define SPECENUM_VALUE6 TER_NOT_GENERATED
#define SPECENUM_VALUE6NAME N_("NotGenerated")
#define SPECENUM_VALUE7 TER_NO_ZOC
#define SPECENUM_VALUE7NAME N_("NoZoc")
#define SPECENUM_VALUE8 TER_ENTER_BORDERS
#define SPECENUM_VALUE8NAME N_("EnterBorders")
#define SPECENUM_VALUE9 TER_FROZEN
#define SPECENUM_VALUE9NAME N_("Frozen")
#define SPECENUM_VALUE10 TER_USER_1
#define SPECENUM_VALUE11 TER_USER_2
#define SPECENUM_VALUE12 TER_USER_3
#define SPECENUM_VALUE13 TER_USER_4
#define SPECENUM_VALUE14 TER_USER_5
#define SPECENUM_VALUE15 TER_USER_6
#define SPECENUM_VALUE16 TER_USER_7
#define SPECENUM_VALUE17 TER_USER_8
#define SPECENUM_VALUE18 TER_USER_9
#define SPECENUM_VALUE19 TER_USER_LAST
#define SPECENUM_NAMEOVERRIDE
#define SPECENUM_BITVECTOR bv_terrain_flags
#include "specenum_gen.h"

#define SPECENUM_NAME mapgen_terrain_property
#define SPECENUM_VALUE0 MG_MOUNTAINOUS
#define SPECENUM_VALUE0NAME "mountainous"
#define SPECENUM_VALUE1 MG_GREEN
#define SPECENUM_VALUE1NAME "green"
#define SPECENUM_VALUE2 MG_FOLIAGE
#define SPECENUM_VALUE2NAME "foliage"
#define SPECENUM_VALUE3 MG_TROPICAL
#define SPECENUM_VALUE3NAME "tropical"
#define SPECENUM_VALUE4 MG_TEMPERATE
#define SPECENUM_VALUE4NAME "temperate"
#define SPECENUM_VALUE5 MG_COLD
#define SPECENUM_VALUE5NAME "cold"
#define SPECENUM_VALUE6 MG_FROZEN
#define SPECENUM_VALUE6NAME "frozen"
#define SPECENUM_VALUE7 MG_WET
#define SPECENUM_VALUE7NAME "wet"
#define SPECENUM_VALUE8 MG_DRY
#define SPECENUM_VALUE8NAME "dry"
#define SPECENUM_VALUE9 MG_OCEAN_DEPTH
#define SPECENUM_VALUE9NAME "ocean_depth"
#define SPECENUM_COUNT MG_COUNT
#include "specenum_gen.h"

#endif /* FC__TERRAIN_ENUMS_GEN_H */
