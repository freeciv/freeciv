#ifndef __PACKHAND_H
#define __PACKHAND_H

void handle_join_game_reply(struct packet_join_game_reply *packet);

void handle_remove_city(struct packet_generic_integer *packet);
void handle_remove_unit(struct packet_generic_integer *packet);

#endif
