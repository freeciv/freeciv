
 /****************************************************************************
 *                       THIS FILE WAS GENERATED                             *
 * Script: common/generate_packets.py                                        *
 * Input:  common/packets.def                                                *
 *                       DO NOT CHANGE THIS FILE                             *
 ****************************************************************************/


#ifndef FC__HAND_GEN_H
#define FC__HAND_GEN_H

#include "shared.h"

#include "fc_types.h"
#include "packets.h"

struct connection;

bool server_handle_packet(enum packet_type type, void *packet,
			  struct player *pplayer, struct connection *pconn);

void handle_nation_select_req(struct player *pplayer, Nation_Type_id nation_no, bool is_male, char *name, int city_style);
void handle_chat_msg_req(struct connection *pc, char *message);
void handle_city_sell(struct player *pplayer, int city_id, int build_id);
void handle_city_buy(struct player *pplayer, int city_id);
void handle_city_change(struct player *pplayer, int city_id, int build_id, bool is_build_id_unit_id);
void handle_city_worklist(struct player *pplayer, int city_id, struct worklist *worklist);
void handle_city_make_specialist(struct player *pplayer, int city_id, int worker_x, int worker_y);
void handle_city_make_worker(struct player *pplayer, int city_id, int worker_x, int worker_y);
void handle_city_change_specialist(struct player *pplayer, int city_id, Specialist_type_id from, Specialist_type_id to);
void handle_city_rename(struct player *pplayer, int city_id, char *name);
void handle_city_options_req(struct player *pplayer, int city_id, int value);
void handle_city_refresh(struct player *pplayer, int city_id);
void handle_city_incite_inq(struct connection *pc, int city_id);
void handle_city_name_suggestion_req(struct player *pplayer, int unit_id);
void handle_player_turn_done(struct player *pplayer);
void handle_player_rates(struct player *pplayer, int tax, int luxury, int science);
void handle_player_change_government(struct player *pplayer, int government);
void handle_player_research(struct player *pplayer, int tech);
void handle_player_tech_goal(struct player *pplayer, int tech);
void handle_player_attribute_block(struct player *pplayer);
struct packet_player_attribute_chunk;
void handle_player_attribute_chunk(struct player *pplayer, struct packet_player_attribute_chunk *packet);
void handle_unit_move(struct player *pplayer, int unit_id, int x, int y);
void handle_unit_build_city(struct player *pplayer, int unit_id, char *name);
void handle_unit_disband(struct player *pplayer, int unit_id);
void handle_unit_change_homecity(struct player *pplayer, int unit_id, int city_id);
void handle_unit_establish_trade(struct player *pplayer, int unit_id);
void handle_unit_help_build_wonder(struct player *pplayer, int unit_id);
void handle_unit_goto(struct player *pplayer, int unit_id, int x, int y);
struct packet_unit_orders;
void handle_unit_orders(struct player *pplayer, struct packet_unit_orders *packet);
void handle_unit_auto(struct player *pplayer, int unit_id);
void handle_unit_load(struct player *pplayer, int cargo_id, int transporter_id);
void handle_unit_unload(struct player *pplayer, int cargo_id, int transporter_id);
void handle_unit_upgrade(struct player *pplayer, int unit_id);
void handle_unit_nuke(struct player *pplayer, int unit_id);
void handle_unit_paradrop_to(struct player *pplayer, int unit_id, int x, int y);
void handle_unit_airlift(struct player *pplayer, int unit_id, int city_id);
void handle_unit_bribe_inq(struct connection *pc, int unit_id);
void handle_unit_type_upgrade(struct player *pplayer, Unit_Type_id type);
void handle_unit_diplomat_action(struct player *pplayer, int diplomat_id, enum diplomat_actions action_type, int target_id, int value);
void handle_unit_change_activity(struct player *pplayer, int unit_id, enum unit_activity activity, enum tile_special_type activity_target);
void handle_diplomacy_init_meeting_req(struct player *pplayer, int counterpart);
void handle_diplomacy_cancel_meeting_req(struct player *pplayer, int counterpart);
void handle_diplomacy_create_clause_req(struct player *pplayer, int counterpart, int giver, enum clause_type type, int value);
void handle_diplomacy_remove_clause_req(struct player *pplayer, int counterpart, int giver, enum clause_type type, int value);
void handle_diplomacy_accept_treaty_req(struct player *pplayer, int counterpart);
void handle_diplomacy_cancel_pact(struct player *pplayer, int other_player_id, enum clause_type clause);
void handle_report_req(struct connection *pc, enum report_type type);
void handle_conn_pong(struct connection *pc);
void handle_spaceship_launch(struct player *pplayer);
void handle_spaceship_place(struct player *pplayer, enum spaceship_place_type type, int num);

#endif /* FC__HAND_GEN_H */
