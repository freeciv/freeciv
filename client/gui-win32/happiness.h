struct happiness_dlg;
struct happiness_dlg *create_happiness_box(struct city *pcity,
					   struct fcwin_box *hbox,
					   HWND win);
void cleanup_happiness_box(struct happiness_dlg *dlg);
void repaint_happiness_box(struct happiness_dlg *dlg, HDC hdc);
void refresh_happiness_box(struct happiness_dlg *dlg);
