#ifndef __CLIMISC_H
#define __CLIMISC_H

struct city;

void client_remove_player(int plr_id);
void client_remove_city(struct city *pcity);
void client_remove_unit(int unit_id);
char *tilefilename(char *name);
void climap_init_continents(void);
void climap_update_continents(int x, int y);

#endif

