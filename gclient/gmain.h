#ifndef __GTKMAIN_H
#define __GTKMAIN_H

int g_main(int argc, char **argv);
void enable_turn_done_button(void);
void end_turn_callback(GtkWidget *widget, gpointer data);
/*
void setup_widgets(void);

void quit_civ(Widget w, XtPointer client_data, XtPointer call_data);

void remove_net_callback(void);
void remove_net_input(void);
*/

#endif
