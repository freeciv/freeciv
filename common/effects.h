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
#ifndef FC__EFFECTS_H
#define FC__EFFECTS_H

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#include "support.h"            /* bool type */
#include "fc_types.h"

#include "connection.h"

struct requirement;

/* Type of effects. Add new values via SPECENUM_VALUE%d and
 * SPECENUM_VALUE%dNAME at the end of the list. */
#define SPECENUM_NAME effect_type
#define SPECENUM_VALUE0 EFT_TECH_PARASITE
#define SPECENUM_VALUE0NAME "Tech_Parasite"
#define SPECENUM_VALUE1 EFT_AIRLIFT
#define SPECENUM_VALUE1NAME "Airlift"
#define SPECENUM_VALUE2 EFT_ANY_GOVERNMENT
#define SPECENUM_VALUE2NAME "Any_Government"
#define SPECENUM_VALUE3 EFT_CAPITAL_CITY
#define SPECENUM_VALUE3NAME "Capital_City"
#define SPECENUM_VALUE4 EFT_ENABLE_NUKE
#define SPECENUM_VALUE4NAME "Enable_Nuke"
#define SPECENUM_VALUE5 EFT_ENABLE_SPACE
#define SPECENUM_VALUE5NAME "Enable_Space"
#define SPECENUM_VALUE6 EFT_SPECIALIST_OUTPUT
#define SPECENUM_VALUE6NAME "Specialist_Output"
#define SPECENUM_VALUE7 EFT_OUTPUT_BONUS
#define SPECENUM_VALUE7NAME "Output_Bonus"
#define SPECENUM_VALUE8 EFT_OUTPUT_BONUS_2
#define SPECENUM_VALUE8NAME "Output_Bonus_2"
/* add to each worked tile */
#define SPECENUM_VALUE9 EFT_OUTPUT_ADD_TILE
#define SPECENUM_VALUE9NAME "Output_Add_Tile"
/* add to each worked tile that already has output */
#define SPECENUM_VALUE10 EFT_OUTPUT_INC_TILE
#define SPECENUM_VALUE10NAME "Output_Inc_Tile"
/* increase tile output by given % */
#define SPECENUM_VALUE11 EFT_OUTPUT_PER_TILE
#define SPECENUM_VALUE11NAME "Output_Per_Tile"
#define SPECENUM_VALUE12 EFT_OUTPUT_WASTE_PCT
#define SPECENUM_VALUE12NAME "Output_Waste_Pct"
#define SPECENUM_VALUE13 EFT_FORCE_CONTENT
#define SPECENUM_VALUE13NAME "Force_Content"
/* TODO: EFT_FORCE_CONTENT_PCT */
#define SPECENUM_VALUE14 EFT_GIVE_IMM_TECH
#define SPECENUM_VALUE14NAME "Give_Imm_Tech"
#define SPECENUM_VALUE15 EFT_GROWTH_FOOD
#define SPECENUM_VALUE15NAME "Growth_Food"
/* reduced illness due to buildings ... */
#define SPECENUM_VALUE16 EFT_HEALTH_PCT
#define SPECENUM_VALUE16NAME "Health_Pct"
#define SPECENUM_VALUE17 EFT_HAVE_EMBASSIES
#define SPECENUM_VALUE17NAME "Have_Embassies"
#define SPECENUM_VALUE18 EFT_MAKE_CONTENT
#define SPECENUM_VALUE18NAME "Make_Content"
#define SPECENUM_VALUE19 EFT_MAKE_CONTENT_MIL
#define SPECENUM_VALUE19NAME "Make_Content_Mil"
#define SPECENUM_VALUE20 EFT_MAKE_CONTENT_MIL_PER
#define SPECENUM_VALUE20NAME "Make_Content_Mil_Per"
/* TODO: EFT_MAKE_CONTENT_PCT */
#define SPECENUM_VALUE21 EFT_MAKE_HAPPY
#define SPECENUM_VALUE21NAME "Make_Happy"
#define SPECENUM_VALUE22 EFT_NO_ANARCHY
#define SPECENUM_VALUE22NAME "No_Anarchy"
#define SPECENUM_VALUE23 EFT_NUKE_PROOF
#define SPECENUM_VALUE23NAME "Nuke_Proof"
/* TODO: EFT_POLLU_ADJ */
/* TODO: EFT_POLLU_PCT */
/* TODO: EFT_POLLU_POP_ADJ */
#define SPECENUM_VALUE24 EFT_POLLU_POP_PCT
#define SPECENUM_VALUE24NAME "Pollu_Pop_Pct"
/* TODO: EFT_POLLU_PROD_ADJ */
#define SPECENUM_VALUE25 EFT_POLLU_PROD_PCT
#define SPECENUM_VALUE25NAME "Pollu_Prod_Pct"
/* TODO: EFT_PROD_PCT */
#define SPECENUM_VALUE26 EFT_REVEAL_CITIES
#define SPECENUM_VALUE26NAME "Reveal_Cities"
#define SPECENUM_VALUE27 EFT_REVEAL_MAP
#define SPECENUM_VALUE27NAME "Reveal_Map"
/* TODO: EFT_INCITE_DIST_ADJ */
#define SPECENUM_VALUE28 EFT_INCITE_COST_PCT
#define SPECENUM_VALUE28NAME "Incite_Cost_Pct"
#define SPECENUM_VALUE29 EFT_SIZE_ADJ
#define SPECENUM_VALUE29NAME "Size_Adj"
#define SPECENUM_VALUE30 EFT_SIZE_UNLIMIT
#define SPECENUM_VALUE30NAME "Size_Unlimit"
#define SPECENUM_VALUE31 EFT_SS_STRUCTURAL
#define SPECENUM_VALUE31NAME "SS_Structural"
#define SPECENUM_VALUE32 EFT_SS_COMPONENT
#define SPECENUM_VALUE32NAME "SS_Component"
#define SPECENUM_VALUE33 EFT_SS_MODULE
#define SPECENUM_VALUE33NAME "SS_Module"
#define SPECENUM_VALUE34 EFT_SPY_RESISTANT
#define SPECENUM_VALUE34NAME "Spy_Resistant"
#define SPECENUM_VALUE35 EFT_MOVE_BONUS
#define SPECENUM_VALUE35NAME "Move_Bonus"
#define SPECENUM_VALUE36 EFT_UNIT_NO_LOSE_POP
#define SPECENUM_VALUE36NAME "Unit_No_Lose_Pop"
#define SPECENUM_VALUE37 EFT_UNIT_RECOVER
#define SPECENUM_VALUE37NAME "Unit_Recover"
#define SPECENUM_VALUE38 EFT_UPGRADE_UNIT
#define SPECENUM_VALUE38NAME "Upgrade_Unit"
#define SPECENUM_VALUE39 EFT_UPKEEP_FREE
#define SPECENUM_VALUE39NAME "Upkeep_Free"
#define SPECENUM_VALUE40 EFT_TECH_UPKEEP_FREE
#define SPECENUM_VALUE40NAME "Tech_Upkeep_Free"
#define SPECENUM_VALUE41 EFT_NO_UNHAPPY
#define SPECENUM_VALUE41NAME "No_Unhappy"
#define SPECENUM_VALUE42 EFT_VETERAN_BUILD
#define SPECENUM_VALUE42NAME "Veteran_Build"
#define SPECENUM_VALUE43 EFT_VETERAN_COMBAT
#define SPECENUM_VALUE43NAME "Veteran_Combat"
#define SPECENUM_VALUE44 EFT_HP_REGEN
#define SPECENUM_VALUE44NAME "HP_Regen"
#define SPECENUM_VALUE45 EFT_CITY_VISION_RADIUS_SQ
#define SPECENUM_VALUE45NAME "City_Vision_Radius_Sq"
#define SPECENUM_VALUE46 EFT_UNIT_VISION_RADIUS_SQ
#define SPECENUM_VALUE46NAME "Unit_Vision_Radius_Sq"
/* Interacts with F_BADWALLATTACKER, ignored by F_IGWALL */
#define SPECENUM_VALUE47 EFT_DEFEND_BONUS
#define SPECENUM_VALUE47NAME "Defend_Bonus"
#define SPECENUM_VALUE48 EFT_NO_INCITE
#define SPECENUM_VALUE48NAME "No_Incite"
#define SPECENUM_VALUE49 EFT_GAIN_AI_LOVE
#define SPECENUM_VALUE49NAME "Gain_AI_Love"
#define SPECENUM_VALUE50 EFT_TURN_YEARS
#define SPECENUM_VALUE50NAME "Turn_Years"
#define SPECENUM_VALUE51 EFT_SLOW_DOWN_TIMELINE
#define SPECENUM_VALUE51NAME "Slow_Down_Timeline"
#define SPECENUM_VALUE52 EFT_CIVIL_WAR_CHANCE
#define SPECENUM_VALUE52NAME "Civil_War_Chance"
/* change of the migration score */
#define SPECENUM_VALUE53 EFT_MIGRATION_PCT
#define SPECENUM_VALUE53NAME "Migration_Pct"
/* +1 unhappy when more than this cities */
#define SPECENUM_VALUE54 EFT_EMPIRE_SIZE_BASE
#define SPECENUM_VALUE54NAME "Empire_Size_Base"
/* adds additional +1 unhappy steps to above */
#define SPECENUM_VALUE55 EFT_EMPIRE_SIZE_STEP
#define SPECENUM_VALUE55NAME "Empire_Size_Step"
#define SPECENUM_VALUE56 EFT_MAX_RATES
#define SPECENUM_VALUE56NAME "Max_Rates"
#define SPECENUM_VALUE57 EFT_MARTIAL_LAW_EACH
#define SPECENUM_VALUE57NAME "Martial_Law_Each"
#define SPECENUM_VALUE58 EFT_MARTIAL_LAW_MAX
#define SPECENUM_VALUE58NAME "Martial_Law_Max"
#define SPECENUM_VALUE59 EFT_RAPTURE_GROW
#define SPECENUM_VALUE59NAME "Rapture_Grow"
#define SPECENUM_VALUE60 EFT_UNBRIBABLE_UNITS
#define SPECENUM_VALUE60NAME "Unbribable_Units"
#define SPECENUM_VALUE61 EFT_REVOLUTION_WHEN_UNHAPPY
#define SPECENUM_VALUE61NAME "Revolution_When_Unhappy"
#define SPECENUM_VALUE62 EFT_HAS_SENATE
#define SPECENUM_VALUE62NAME "Has_Senate"
#define SPECENUM_VALUE63 EFT_INSPIRE_PARTISANS
#define SPECENUM_VALUE63NAME "Inspire_Partisans"
#define SPECENUM_VALUE64 EFT_HAPPINESS_TO_GOLD
#define SPECENUM_VALUE64NAME "Happiness_To_Gold"
/* stupid special case; we hate it */
#define SPECENUM_VALUE65 EFT_FANATICS
#define SPECENUM_VALUE65NAME "Fanatics"
#define SPECENUM_VALUE66 EFT_NO_DIPLOMACY
#define SPECENUM_VALUE66NAME "No_Diplomacy"
#define SPECENUM_VALUE67 EFT_TRADE_REVENUE_BONUS
#define SPECENUM_VALUE67NAME "Trade_Revenue_Bonus"
/* multiply unhappy upkeep by this effect */
#define SPECENUM_VALUE68 EFT_UNHAPPY_FACTOR
#define SPECENUM_VALUE68NAME "Unhappy_Factor"
/* multiply upkeep by this effect */
#define SPECENUM_VALUE69 EFT_UPKEEP_FACTOR
#define SPECENUM_VALUE69NAME "Upkeep_Factor"
/* this many units are free from upkeep */
#define SPECENUM_VALUE70 EFT_UNIT_UPKEEP_FREE_PER_CITY
#define SPECENUM_VALUE70NAME "Unit_Upkeep_Free_Per_City"
#define SPECENUM_VALUE71 EFT_OUTPUT_WASTE
#define SPECENUM_VALUE71NAME "Output_Waste"
#define SPECENUM_VALUE72 EFT_OUTPUT_WASTE_BY_DISTANCE
#define SPECENUM_VALUE72NAME "Output_Waste_By_Distance"
/* -1 penalty to tiles producing more than this */
#define SPECENUM_VALUE73 EFT_OUTPUT_PENALTY_TILE
#define SPECENUM_VALUE73NAME "Output_Penalty_Tile"
#define SPECENUM_VALUE74 EFT_OUTPUT_INC_TILE_CELEBRATE
#define SPECENUM_VALUE74NAME "Output_Inc_Tile_Celebrate"
/* all citizens after this are unhappy */
#define SPECENUM_VALUE75 EFT_CITY_UNHAPPY_SIZE
#define SPECENUM_VALUE75NAME "City_Unhappy_Size"
/* add to default squared city radius */
#define SPECENUM_VALUE76 EFT_CITY_RADIUS_SQ
#define SPECENUM_VALUE76NAME "City_Radius_Sq"
/* number of build slots for units */
#define SPECENUM_VALUE77 EFT_CITY_BUILD_SLOTS
#define SPECENUM_VALUE77NAME "City_Build_Slots"
#define SPECENUM_VALUE78 EFT_UPGRADE_PRICE_PCT
#define SPECENUM_VALUE78NAME "Upgrade_Price_Pct"
/* City should use walls gfx */
#define SPECENUM_VALUE79 EFT_VISIBLE_WALLS
#define SPECENUM_VALUE79NAME "Visible_Walls"
#define SPECENUM_VALUE80 EFT_TECH_COST_FACTOR
#define SPECENUM_VALUE80NAME "Tech_Cost_Factor"
/* [x%] gold upkeep instead of [1] shield upkeep for units */
#define SPECENUM_VALUE81 EFT_SHIELD2GOLD_FACTOR
#define SPECENUM_VALUE81NAME "Shield2Gold_Factor"
#define SPECENUM_VALUE82 EFT_TILE_WORKABLE
#define SPECENUM_VALUE82NAME "Tile_Workable"
/* The index for the city image of the given city style. */
#define SPECENUM_VALUE83 EFT_CITY_IMAGE
#define SPECENUM_VALUE83NAME "City_Image"
#define SPECENUM_VALUE84 EFT_IRRIG_POSSIBLE
#define SPECENUM_VALUE84NAME "Irrig_Possible"
#define SPECENUM_VALUE85 EFT_MAX_TRADE_ROUTES
#define SPECENUM_VALUE85NAME "Max_Trade_Routes"
#define SPECENUM_VALUE86 EFT_GOV_CENTER
#define SPECENUM_VALUE86NAME "Gov_Center"
#define SPECENUM_VALUE87 EFT_TRANSFORM_POSSIBLE
#define SPECENUM_VALUE87NAME "Transform_Possible"
#define SPECENUM_VALUE88 EFT_MINING_POSSIBLE
#define SPECENUM_VALUE88NAME "Mining_Possible"
#define SPECENUM_VALUE89 EFT_IRRIG_TF_POSSIBLE
#define SPECENUM_VALUE89NAME "Irrig_TF_Possible"
#define SPECENUM_VALUE90 EFT_MINING_TF_POSSIBLE
#define SPECENUM_VALUE90NAME "Mining_TF_Possible"
/* keep this last */
#define SPECENUM_VALUE91 EFT_LAST
#include "specenum_gen.h"

/* An effect is provided by a source.  If the source is present, and the
 * other conditions (described below) are met, the effect will be active.
 * Note the difference between effect and effect_type. */
struct effect {
  enum effect_type type;

  /* The "value" of the effect.  The meaning of this varies between
   * effects.  When get_xxx_bonus() is called the value of all applicable
   * effects will be summed up. */
  int value;

  /* An effect can have multiple requirements.  The effect will only be
   * active if all of these requirement are met. */
  struct requirement_list *reqs;

  /* An effect can have multiple negated requirements.  The effect will
   * only be active if none of these requirements are met. */
  struct requirement_list *nreqs;
};

/* An effect_list is a list of effects. */
#define SPECLIST_TAG effect
#define SPECLIST_TYPE struct effect
#include "speclist.h"
#define effect_list_iterate(effect_list, peffect) \
  TYPED_LIST_ITERATE(struct effect, effect_list, peffect)
#define effect_list_iterate_end LIST_ITERATE_END

struct effect *effect_new(enum effect_type type, int value);
void effect_req_append(struct effect *peffect, bool neg,
		       struct requirement *preq);

void get_effect_req_text(struct effect *peffect, char *buf, size_t buf_len);

/* ruleset cache creation and communication functions */
struct packet_ruleset_effect;
struct packet_ruleset_effect_req;

void ruleset_cache_init(void);
void ruleset_cache_free(void);
void recv_ruleset_effect(const struct packet_ruleset_effect *packet);
void recv_ruleset_effect_req(const struct packet_ruleset_effect_req *packet);
void send_ruleset_cache(struct conn_list *dest);

bool is_effect_useful(const struct player *target_player,
		      const struct city *target_pcity,
		      const struct impr_type *target_building,
		      const struct tile *target_tile,
		      const struct unit_type *target_unittype,
		      const struct output_type *target_output,
		      const struct specialist *target_specialist,
		      const struct impr_type *source,
		      const struct effect *effect,
                      const enum   req_problem_type prob_type);

bool is_building_replaced(const struct city *pcity,
			  struct impr_type *pimprove,
                          const enum req_problem_type prob_type);

/* functions to know the bonuses a certain effect is granting */
int get_world_bonus(enum effect_type effect_type);
int get_player_bonus(const struct player *plr, enum effect_type effect_type);
int get_city_bonus(const struct city *pcity, enum effect_type effect_type);
int get_city_specialist_output_bonus(const struct city *pcity,
				     const struct specialist *pspecialist,
				     const struct output_type *poutput,
				     enum effect_type effect_type);
int get_city_tile_output_bonus(const struct city *pcity,
			       const struct tile *ptile,
			       const struct output_type *poutput,
			       enum effect_type effect_type);
int get_player_output_bonus(const struct player *pplayer,
                            const struct output_type *poutput,
                            enum effect_type effect_type);
int get_city_output_bonus(const struct city *pcity,
                          const struct output_type *poutput,
                          enum effect_type effect_type);
int get_building_bonus(const struct city *pcity,
		       const struct impr_type *building,
		       enum effect_type effect_type);
int get_unittype_bonus(const struct player *pplayer,
		       const struct tile *ptile, /* pcity is implied */
		       const struct unit_type *punittype,
		       enum effect_type effect_type);
int get_unit_bonus(const struct unit *punit, enum effect_type effect_type);
int get_tile_bonus(const struct tile *ptile, const struct unit *punit,
                   enum effect_type etype);

/* miscellaneous auxiliary effects functions */
struct effect_list *get_req_source_effects(struct universal *psource);
bool is_effect_disabled(const struct player *target_player,
		        const struct city *target_city,
		        const struct impr_type *target_building,
		        const struct tile *target_tile,
			const struct unit_type *target_unittype,
			const struct output_type *target_output,
			const struct specialist *target_specialist,
		        const struct effect *peffect,
                        const enum   req_problem_type prob_type);

int get_player_bonus_effects(struct effect_list *plist,
    const struct player *pplayer, enum effect_type effect_type);
int get_city_bonus_effects(struct effect_list *plist,
			   const struct city *pcity,
			   const struct output_type *poutput,
			   enum effect_type effect_type);

bool building_has_effect(const struct impr_type *pimprove,
			 enum effect_type effect_type);
int get_current_construction_bonus(const struct city *pcity,
				   enum effect_type effect_type,
                                   const enum req_problem_type prob_type);

Impr_type_id ai_find_source_building(struct city *pcity,
				     enum effect_type effect_type,
                                     struct unit_class *uclass,
                                     enum unit_move_type move);

typedef bool (*iec_cb)(const struct effect*);
bool iterate_effect_cache(iec_cb cb);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif  /* FC__EFFECTS_H */
