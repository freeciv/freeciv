#ifndef __PACKHAND_H
#define __PACKHAND_H

void handle_join_game_reply(struct packet_join_game_reply *packet);

void handle_remove_city(struct packet_generic_integer *packet);
void handle_remove_unit(struct packet_generic_integer *packet);
void handle_incite_cost(struct packet_generic_values *packet);

void handle_city_options(struct packet_generic_values *preq);

void handle_spaceship_info(struct packet_spaceship_info *packet);

void popdown_races_dialog(void);
#endif
