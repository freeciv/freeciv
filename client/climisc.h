#ifndef __CLIMISC_H
#define __CLIMISC_H

void client_remove_player(int plr_id);
void client_remove_city(int city_id);
void client_remove_unit(int unit_id);
char *datafilename(char *filename);
char *tilefilename(char *name);

#endif

