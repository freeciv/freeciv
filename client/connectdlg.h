#ifndef __CONNECTDLG_H
#define __CONNECTDLG_H
int gui_server_connect(void);
void connect_dialog_returnkey(Widget w, XEvent *event, String *params,
			    Cardinal *num_params);
#endif
