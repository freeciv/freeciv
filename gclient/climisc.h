#ifndef __CLIMISC_H
#define __CLIMISC_H

char *datafilename(char *filename);
void log_output_window( GtkWidget *widget, gpointer data );


void client_remove_player(int plr_id);
void client_remove_city(int city_id);
void client_remove_unit(int unit_id);

#endif

