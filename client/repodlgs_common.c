
#include "game.h"

#include "repodlgs_common.h"

/****************************************************************
  Fills out the array of struct improvement_entry given by
  entries. The array must be able to hold at least B_LAST entries.
*****************************************************************/
void get_economy_report_data(struct improvement_entry *entries,
			     int *num_entries_used, int *total_cost,
			     int *total_income)
{
  *num_entries_used = 0;
  *total_cost = 0;

  impr_type_iterate(impr_id) {
    if (!is_wonder(impr_id)) {
      int count = 0, cost = 0;
      city_list_iterate(game.player_ptr->cities, pcity) {
	if (city_got_building(pcity, impr_id)) {
	  count++;
	  cost += improvement_upkeep(pcity, impr_id);
	}
      }
      city_list_iterate_end;

      if (count == 0) {
	continue;
      }

      entries[*num_entries_used].type = impr_id;
      entries[*num_entries_used].count = count;
      entries[*num_entries_used].total_cost = cost;
      entries[*num_entries_used].cost = cost / count;
      (*num_entries_used)++;
      
      *total_cost += cost;
    }
  } impr_type_iterate_end;

  *total_income = 0;

  city_list_iterate(game.player_ptr->cities, pcity) {
    *total_income += pcity->tax_total;
    if (!pcity->is_building_unit && pcity->currently_building == B_CAPITAL) {
      *total_income += pcity->shield_surplus;
    }
  } city_list_iterate_end;
}
