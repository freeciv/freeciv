/********************************************************************** 
 Freeciv - Copyright (C) 2005 - The Freeciv Project
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
***********************************************************************/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>

#include "fcintl.h"
#include "hash.h"
#include "log.h"
#include "mem.h"

#include "game.h"
#include "map.h"
#include "tile.h"

#include "civclient.h"
#include "editor.h"
#include "mapview_common.h"
#include "tilespec.h"

#include "canvas.h"
#include "gui_main.h"
#include "plrdlg.h"

#include "editprop.h"


/* Forward declarations. */
struct objprop;
struct objbind;
struct property_page;
struct property_editor;
struct extviewer;

/* Helper function declarations. */
static GdkPixbuf *create_pixbuf_from_layers(struct tile *ptile,
                                            struct unit *punit,
                                            struct city *pcity,
                                            int *layers,
                                            int num_layers);
static GdkPixbuf *create_tile_pixbuf(struct tile *ptile);
static GdkPixbuf *create_unit_pixbuf(struct unit *punit);
static GdkPixbuf *create_city_pixbuf(struct city *pcity);

static void add_column(GtkWidget *view,
                       int col_id,
                       const char *name,
                       GType gtype,
                       bool editable,
                       bool is_radio,
                       GCallback edit_callback,
                       gpointer callback_userdata);
static void disable_widget_callback(GtkWidget *w, GCallback cb);
static void enable_widget_callback(GtkWidget *w, GCallback cb);

static int built_status_to_string(char *buf, int buflen,
                                  struct built_status *bs);

/* NB: If packet definitions change, be sure to
 * update objbind_pack_current_values!!! */
union packetdata {
  gpointer v_pointer;
  struct packet_edit_tile *tile;
  struct packet_edit_city *city;
  struct packet_edit_unit *unit;
  struct packet_edit_player *player;
};


/****************************************************************************
  Object type declarations.

  To add a new object type:
  1. Add a value in enum editor_object_type in client/editor.h.
  2. Add a string name to objtype_get_name.
  3. Add a code in objtype_get_id_from_object.
  4. Add code to get an object from an id in
     objbind_get_object.
  5. Add an if-block in objbind_get_value_from_object.
  6. Add an if-block in objbind_get_allowed_value_span.
  7. Add a case handler in property_page_setup_objprops.
  8. Add a case handler in property_page_add_objbinds_from_tile
     if applicable.

  Furthermore, if the object type is to be editable:
  9. Define its edit packet in common/packets.def.
  10. Add the packet handler in server/edithand.c.
  11. Add its edit packet type to union packetdata.
  12. Add an if-block in objbind_pack_current_values.
  13. Add an if-block in objbind_pack_modified_value.
  14. Add code in property_page_new_packet.
  15. Add code in property_page_send_packet.
  16. Add calls to editgui_notify_object_changed in
      client/packhand.c or where applicable.

****************************************************************************/

/* OBJTYPE_* enum values defined in client/editor.h */

static const char *objtype_get_name(int objtype);
static int objtype_get_id_from_object(int objtype, gpointer object);


/****************************************************************************
  Value type declarations.

  To add a new value type:
  1. Add a value in enum value_types.
  2. Add its field in union propval_data.
  3. Add a case handler in valtype_get_name.
  4. Add a case handler in propval_copy if needed.
  5. Add a case handler in propval_free if needed.
  6. Add a case handler in propval_equal if needed.
  7. Add a case handler in objprop_get_gtype.
  8. Add a case handler in property_page_set_store_value.
  9. Add a case handler in propval_as_string if needed.
****************************************************************************/
enum value_types {
  VALTYPE_NONE = 0,
  VALTYPE_INT,
  VALTYPE_STRING,
  VALTYPE_PIXBUF,
  VALTYPE_BUILT_ARRAY,        /* struct built_status[B_LAST] */
  VALTYPE_INVENTIONS_ARRAY,   /* bool[A_LAST] */
  VALTYPE_BV_SPECIAL,
  VALTYPE_NATION
};

static const char *valtype_get_name(int valtype);


/****************************************************************************
  Propstate and propval declarations.

  To add a new member to union propval_data, see the steps for adding a
  new value type above.

  New property values are "constructed" by objbind_get_value_from_object.
****************************************************************************/
union propval_data {
  gpointer v_pointer;
  int v_int;
  char *v_string;
  const char *v_const_string;
  GdkPixbuf *v_pixbuf;
  struct built_status *v_built;
  bv_special v_bv_special;
  struct nation_type *v_nation;
  bool *v_inventions;
};

struct propval {
  union propval_data data;
  int valtype;
  bool must_free;
};

static void propval_free(struct propval *pv);
static struct propval *propval_copy(struct propval *pv);
static bool propval_equal(struct propval *pva, struct propval *pvb);
static int propval_as_string(struct propval *pv, char *buf, int buflen);

struct propstate {
  int property_id;
  struct propval *property_value;
};

static struct propstate *propstate_new(struct objprop *op,
                                       struct propval *pv);
static void propstate_free(struct propstate *ps);
static void propstate_clear_value(struct propstate *ps);
static void propstate_set_value(struct propstate *ps,
                                struct propval *pv);
static struct propval *propstate_get_value(struct propstate *ps);


/****************************************************************************
  Objprop declarations.

  To add a new object property:
  1. Add a value in enum object_property_ids (grouped by
     object type).
  2. Define the property in property_page_setup_objprops.
  3. Add a case handler in objbind_get_value_from_object
     in the appropriate object type block.
  4. Add a case handler in objprop_setup_widget.
  5. Add a case handler in objprop_refresh_widget.

  Furthermore, if the property is editable:
  5. Add a field for this property in the edit
     packet for this object type in common/packets.def.
  6. Add a case handler in objbind_pack_modified_value.
  !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
  !!! 7. Add code to set the packet field in  !!!
  !!!    objbind_pack_current_values.         !!!
  !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
  8. Add code to handle changes in the packet field in
     server/edithand.c handle_edit_<objtype>.

  If the property makes use of an extviewer:
  9. Handle widget creation in extviewer_new.
  10. Handle refresh in extviewer_refresh_widgets.
  11. Handle clear in extviewer_clear_widgets.
  12. Handle any signal callbacks (e.g. toggled) if needed.

  TODO: Add more object properties.
****************************************************************************/
enum object_property_ids {
  OPID_TILE_IMAGE,
  OPID_TILE_X,
  OPID_TILE_Y,
  OPID_TILE_NAT_X,
  OPID_TILE_NAT_Y,
  OPID_TILE_CONTINENT,
  OPID_TILE_ADDRESS,
  OPID_TILE_TERRAIN,
  OPID_TILE_INDEX,
  OPID_TILE_XY,
  OPID_TILE_RESOURCE,
  OPID_TILE_SPECIALS,

  OPID_UNIT_IMAGE,
  OPID_UNIT_ADDRESS,
  OPID_UNIT_TYPE,
  OPID_UNIT_ID,
  OPID_UNIT_XY,
  OPID_UNIT_MOVES_LEFT,

  OPID_CITY_IMAGE,
  OPID_CITY_NAME,
  OPID_CITY_ADDRESS,
  OPID_CITY_ID,
  OPID_CITY_XY,
  OPID_CITY_SIZE,
  OPID_CITY_BUILDINGS,

  OPID_PLAYER_NAME,
  OPID_PLAYER_NATION,
  OPID_PLAYER_ADDRESS,
  OPID_PLAYER_INVENTIONS
};

enum object_property_flags {
  OPF_NO_FLAGS    = 0,
  OPF_EDITABLE    = 1<<0,
  OPF_IN_LISTVIEW = 1<<1,
  OPF_HAS_WIDGET  = 1<<2
};

struct objprop {
  int id;
  const char *name;
  int flags;
  int valtype;
  int column_id;
  GtkWidget *widget;
  struct extviewer *extviewer;
  struct property_page *parent_page;
};

static struct objprop *objprop_new(int id,
                                   const char *name,
                                   int flags,
                                   int valtype,
                                   struct property_page *parent);
static int objprop_get_id(const struct objprop *op);
static const char *objprop_get_name(const struct objprop *op);
static int objprop_get_valtype(const struct objprop *op);
static struct property_page *objprop_get_property_page(const struct objprop *op);

static bool objprop_show_in_listview(const struct objprop *op);
static bool objprop_is_sortable(const struct objprop *op);
static bool objprop_is_readonly(const struct objprop *op);
static bool objprop_has_widget(const struct objprop *op);

static GType objprop_get_gtype(const struct objprop *op);
static const char *objprop_get_attribute_type_string(const struct objprop *op);
static void objprop_set_column_id(struct objprop *op, int col_id);
static int objprop_get_column_id(const struct objprop *op);
static GtkCellRenderer *objprop_create_cell_renderer(const struct objprop *op);

static void objprop_setup_widget(struct objprop *op);
static GtkWidget *objprop_get_widget(struct objprop *op);
static void objprop_set_child_widget(struct objprop *op,
                                     const char *widget_name,
                                     GtkWidget *widget);
static GtkWidget *objprop_get_child_widget(struct objprop *op,
                                           const char *widget_name);
static void objprop_set_extviewer(struct objprop *op,
                                  struct extviewer *ev);
static struct extviewer *objprop_get_extviewer(struct objprop *op);
static void objprop_refresh_widget(struct objprop *op,
                                   struct objbind *ob);
static void objprop_widget_entry_activated(GtkEntry *entry,
                                           gpointer userdata);
static void objprop_widget_spin_button_changed(GtkSpinButton *spin,
                                               gpointer userdata);


/****************************************************************************
  Objbind declarations.
****************************************************************************/
struct objbind {
  int objtype;
  int object_id;
  struct property_page *parent_property_page;
  struct hash_table *propstate_table;
  GtkTreeRowReference *rowref;
};

static struct objbind *objbind_new(int objtype,
                                   gpointer object);
static void objbind_free(struct objbind *ob);
static int objbind_get_objtype(const struct objbind *ob);
static void objbind_bind_properties(struct objbind *ob,
                                    struct property_page *pp);
static gpointer objbind_get_object(struct objbind *ob);
static struct propval *objbind_get_value_from_object(struct objbind *ob,
                                                     struct objprop *op);
static bool objbind_get_allowed_value_span(struct objbind *ob,
                                           struct objprop *op,
                                           double *pmin,
                                           double *pmax,
                                           double *pstep,
                                           double *pbig_step);
static void objbind_set_modified_value(struct objbind *ob,
                                       struct objprop *op,
                                       struct propval *pv);
static struct propval *objbind_get_modified_value(struct objbind *ob,
                                                  struct objprop *op);
static void objbind_clear_modified_value(struct objbind *ob,
                                         struct objprop *op);
static bool objbind_property_is_modified(struct objbind *ob,
                                         struct objprop *op);
static bool objbind_has_modified_properties(struct objbind *ob);
static void objbind_clear_all_modified_values(struct objbind *ob);
static void objbind_pack_current_values(struct objbind *ob,
                                        union packetdata packet);
static void objbind_pack_modified_value(struct objbind *ob,
                                        struct objprop *op,
                                        union packetdata packet);
static void objbind_set_rowref(struct objbind *ob,
                               GtkTreeRowReference *rr);
static GtkTreeRowReference *objbind_get_rowref(struct objbind *ob);


/****************************************************************************
  Extended property viewer declarations. This is a set of widgets used
  for viewing and/or editing properties with complex values (e.g. arrays).
****************************************************************************/
struct extviewer {
  struct objprop *objprop;
  struct propval *pv_cached;

  GtkWidget *panel_widget;
  GtkWidget *panel_label;
  GtkWidget *panel_button;
  GtkWidget *panel_image;

  GtkWidget *view_widget;
  GtkWidget *view_label;

  GtkListStore *store;
};

static struct extviewer *extviewer_new(struct objprop *op);
static struct objprop *extviewer_get_objprop(struct extviewer *ev);
static GtkWidget *extviewer_get_panel_widget(struct extviewer *ev);
static GtkWidget *extviewer_get_view_widget(struct extviewer *ev);
static void extviewer_refresh_widgets(struct extviewer *ev,
                                      struct propval *pv);
static void extviewer_clear_widgets(struct extviewer *ev);
static void extviewer_panel_button_clicked(GtkButton *button,
                                           gpointer userdata);
static void extviewer_view_cell_toggled(GtkCellRendererToggle *cell,
                                        gchar *path,
                                        gpointer userdata);


/****************************************************************************
  Property page declarations.
****************************************************************************/
struct property_page {
  int objtype;

  GtkWidget *widget;
  GtkListStore *object_store;
  GtkWidget *object_view;
  GtkWidget *extviewer_notebook;

  struct hash_table *objprop_table;
  struct hash_table *objbind_table;

  struct objbind *focused_objbind;
};

static struct property_page *property_page_new(int objtype);
static void property_page_setup_objprops(struct property_page *pp);
static const char *property_page_get_name(const struct property_page *pp);
static void property_page_load_tiles(struct property_page *pp,
                                     const struct tile_list *tiles);
static void property_page_add_objbinds_from_tile(struct property_page *pp,
                                                 const struct tile *ptile);
static int property_page_get_num_objbinds(const struct property_page *pp);
static void property_page_clear_objbinds(struct property_page *pp);
static void property_page_add_objbind(struct property_page *pp,
                                      gpointer object_data);
static void property_page_fill_widgets(struct property_page *pp);
static struct objbind *property_page_get_focused_objbind(struct property_page *pp);
static void property_page_set_focused_objbind(struct property_page *pp,
                                              struct objbind *ob);
static struct objbind *property_page_get_objbind(struct property_page *pp,
                                                 int object_id);
static void property_page_selection_changed(GtkTreeSelection *sel,
                                            gpointer userdata);
static gboolean property_page_selection_func(GtkTreeSelection *sel,
                                             GtkTreeModel *model,
                                             GtkTreePath *path,
                                             gboolean currently_selected,
                                             gpointer data);
static void property_page_quick_find_entry_changed(GtkWidget *entry,
                                                   gpointer userdata);
static void property_page_change_value(struct property_page *pp,
                                       struct objprop *op,
                                       struct propval *pv);
static void property_page_send_values(struct property_page *pp);
static void property_page_reset_objbinds(struct property_page *pp);
static union packetdata property_page_new_packet(struct property_page *pp);
static void property_page_send_packet(struct property_page *pp,
                                      union packetdata packet);
static void property_page_free_packet(struct property_page *pp,
                                      union packetdata packet);
static void property_page_object_changed(struct property_page *pp,
                                         int object_id,
                                         bool remove);
static void property_page_add_extviewer(struct property_page *pp,
                                        struct extviewer *ev);
static void property_page_show_extviewer(struct property_page *pp,
                                         struct extviewer *ev);

#define property_page_objprop_iterate(ARG_pp, NAME_op) do {\
  struct objprop *NAME_op;\
  if (!(ARG_pp) || !(ARG_pp)->objprop_table) {\
    break;\
  }\
  hash_iterate((ARG_pp)->objprop_table, MY_dummy, MY_value) {\
    NAME_op = MY_value;

#define property_page_objprop_iterate_end \
  } hash_iterate_end;\
} while (0)

#define property_page_objbind_iterate(ARG_pp, NAME_ob) do {\
  struct objbind *NAME_ob;\
  if (!(ARG_pp) || !(ARG_pp)->objbind_table) {\
    break;\
  }\
  hash_iterate((ARG_pp)->objbind_table, MY_dummy, MY_value) {\
    NAME_ob = MY_value;

#define property_page_objbind_iterate_end \
  } hash_iterate_end;\
} while (0)


/****************************************************************************
  Property editor declarations.
****************************************************************************/
struct property_editor {
  GtkWidget *widget;
  GtkWidget *notebook;
  GtkWidget *combo;

  struct property_page *property_pages[NUM_OBJTYPES];
};

static struct property_editor *property_editor_new(void);
static bool property_editor_add_page(struct property_editor *pe,
                                     int objtype);
static void property_editor_apply_button_clicked(GtkButton *button,
                                                 gpointer userdata);
static void property_editor_refresh_button_clicked(GtkButton *button,
                                                   gpointer userdata);
static void property_editor_combo_changed(GtkComboBox *combo,
                                          gpointer userdata);

static struct property_editor *the_property_editor;


/****************************************************************************
  Returns the translated name for the given object type.
****************************************************************************/
static const char *objtype_get_name(int objtype)
{
  switch (objtype) {

  case OBJTYPE_TILE:
    return _("Tile");
    break;

  case OBJTYPE_UNIT:
    return _("Unit");
    break;

  case OBJTYPE_CITY:
    return _("City");
    break;

  case OBJTYPE_PLAYER:
    return _("Player");
    break;

  default:
    break;
  }

  return "";
}

/****************************************************************************
  Returns the unique identifier value from the given object, assuming it
  is of the 'objtype' object type. Valid IDs are always greater than zero.
****************************************************************************/
static int objtype_get_id_from_object(int objtype, gpointer object)
{
  struct tile *ptile;
  struct unit *punit;
  struct city *pcity;
  struct player *pplayer;
  int id = 0;

  switch (objtype) {

  case OBJTYPE_TILE:
    ptile = object;
    id = tile_index(ptile);
    break;
    
  case OBJTYPE_UNIT:
    punit = object;
    id = punit->id;
    break;
    
  case OBJTYPE_CITY:
    pcity = object;
    id = pcity->id;
    break;

  case OBJTYPE_PLAYER:
    pplayer = object;
    id = player_number(pplayer);
    break;

  default:
    break;
  }

  return id;
}

/****************************************************************************
  Returns the untranslated name for the given value type.
****************************************************************************/
static const char *valtype_get_name(int valtype)
{
  switch (valtype) {
  case VALTYPE_NONE:
    return "none";
    break;
  case VALTYPE_STRING:
    return "string";
    break;
  case VALTYPE_INT:
    return "int";
    break;
  case VALTYPE_PIXBUF:
    return "pixbuf";
    break;
  case VALTYPE_BUILT_ARRAY:
    return "struct built_status[B_LAST]";
    break;
  case VALTYPE_INVENTIONS_ARRAY:
    return "bool[A_LAST]";
    break;
  case VALTYPE_BV_SPECIAL:
    return "bv_special";
    break;
  case VALTYPE_NATION:
    return "nation";
    break;
  default:
    break;
  }
  return NULL;
}

/****************************************************************************
  Convenience function to add a column to a GtkTreeView. Used for the
  view widget creation in extviewer_new().
****************************************************************************/
static void add_column(GtkWidget *view,
                       int col_id,
                       const char *name,
                       GType gtype,
                       bool editable,
                       bool is_radio,
                       GCallback edit_callback,
                       gpointer userdata)
{
  GtkCellRenderer *cell;
  GtkTreeViewColumn *col;
  const char *attr = NULL;

  if (gtype == G_TYPE_BOOLEAN) {
    cell = gtk_cell_renderer_toggle_new();
    gtk_cell_renderer_toggle_set_radio(GTK_CELL_RENDERER_TOGGLE(cell),
                                       is_radio);
    if (editable) {
      g_signal_connect(cell, "toggled", edit_callback, userdata);
    }
    attr = "active";
  } else if (gtype == GDK_TYPE_PIXBUF) {
    cell = gtk_cell_renderer_pixbuf_new();
    attr = "pixbuf";
  } else {
    cell = gtk_cell_renderer_text_new();
    if (editable) {
      g_object_set(cell, "editable", TRUE, NULL);
      g_signal_connect(cell, "edited", edit_callback, userdata);
    }
    attr = "text";
  }

  col = gtk_tree_view_column_new_with_attributes(name, cell,
                                                 attr, col_id, NULL);
  gtk_tree_view_append_column(GTK_TREE_VIEW(view), col);
}

/****************************************************************************
  Temporarily disable signal invocation of the given callback for the given
  widget. Re-enable the signal with enable_widget_callback.
****************************************************************************/
static void disable_widget_callback(GtkWidget *w, GCallback cb)
{
  gulong hid;

  if (!w || !cb) {
    return;
  }

  hid = g_signal_handler_find(w, G_SIGNAL_MATCH_FUNC,
                              0, 0, NULL, cb, NULL);
  g_signal_handler_block(w, hid);
}

/****************************************************************************
  Re-enable a signal callback blocked by disable_widget_callback.
****************************************************************************/
static void enable_widget_callback(GtkWidget *w, GCallback cb)
{
  gulong hid;

  if (!w || !cb) {
    return;
  }

  hid = g_signal_handler_find(w, G_SIGNAL_MATCH_FUNC,
                              0, 0, NULL, cb, NULL);
  g_signal_handler_unblock(w, hid);
}

/****************************************************************************
  Fill the supplied buffer with a string representation of the given value.
****************************************************************************/
static int propval_as_string(struct propval *pv, char *buf, int buflen)
{
  int id, ret = 0, count = 0;
  int great_wonder_count = 0;
  int small_wonder_count = 0;
  int building_count = 0;

  if (!pv || buflen < 1) {
    return 0;
  }

  switch (pv->valtype) {

  case VALTYPE_BUILT_ARRAY:
    improvement_iterate(pimprove) {
      id = improvement_index(pimprove);
      if (pv->data.v_built[id].turn < 0) {
        continue;
      }
      if (is_great_wonder(pimprove)) {
        great_wonder_count++;
      } else if (is_small_wonder(pimprove)) {
        small_wonder_count++;
      } else {
        building_count++;
      }
    } improvement_iterate_end;
    /* TRANS: "Number of buildings, number of small
     * wonders (e.g. palace), number of great wonders." */
    ret = my_snprintf(buf, buflen, _("%db %ds %dW"),
                      building_count, small_wonder_count,
                      great_wonder_count);
    break;

  case VALTYPE_INVENTIONS_ARRAY:
    advance_index_iterate(A_FIRST, tech) {
      if (pv->data.v_inventions[tech]) {
        count++;
      }
    } advance_index_iterate_end;
    /* TRANS: "Number of technologies known". */
    ret = my_snprintf(buf, buflen, _("%d known"), count);
    break;

  case VALTYPE_BV_SPECIAL:
    tile_special_type_iterate(spe) {
      if (!(0 <= spe && spe < buflen - 1)) {
        continue;
      }
      if (BV_ISSET(pv->data.v_bv_special, spe)) {
        count++;
      }
    } tile_special_type_iterate_end;
    /* TRANS: "The number of terrain specials (e.g. road,
     * rail, hut, etc.) present on a tile." */
    ret = my_snprintf(buf, buflen, _("%d present"), count);
    break;

  default:
    buf[0] = '\0';
    break;
  }

  return ret;
}

/****************************************************************************
  Convert the built_status information to a user viewable string.
****************************************************************************/
static int built_status_to_string(char *buf, int buflen,
                                  struct built_status *bs)
{
  int ret, turn_built;

  turn_built = bs->turn;

  if (turn_built == I_NEVER) {
    /* TRANS: Improvement never built. */
    ret = my_snprintf(buf, buflen, "%s", _("(never)"));
  } else if (turn_built == I_DESTROYED) {
    /* TRANS: Improvement was destroyed. */
    ret = my_snprintf(buf, buflen, "%s", _("(destroyed)"));
  } else {
    ret = my_snprintf(buf, buflen, "%d", turn_built);
  }

  return ret;
}

/****************************************************************************
  Return a newly allocated deep copy of the given value.
****************************************************************************/
static struct propval *propval_copy(struct propval *pv)
{
  struct propval *pv_copy;
  int size;

  if (!pv) {
    return NULL;
  }

  pv_copy = fc_calloc(1, sizeof(*pv));
  pv_copy->valtype = pv->valtype;

  switch (pv->valtype) {
  case VALTYPE_STRING:
    pv_copy->data.v_string = mystrdup(pv->data.v_string);
    pv_copy->must_free = TRUE;
    break;
  case VALTYPE_PIXBUF:
    g_object_ref(pv->data.v_pixbuf);
    pv_copy->data.v_pixbuf = pv->data.v_pixbuf;
    pv_copy->must_free = TRUE;
    break;
  case VALTYPE_BUILT_ARRAY:
    size = B_LAST * sizeof(struct built_status);
    pv_copy->data.v_pointer = fc_malloc(size);
    memcpy(pv_copy->data.v_pointer, pv->data.v_pointer, size);
    pv_copy->must_free = TRUE;
    break;
  case VALTYPE_INVENTIONS_ARRAY:
    size = A_LAST * sizeof(bool);
    pv_copy->data.v_pointer = fc_malloc(size);
    memcpy(pv_copy->data.v_pointer, pv->data.v_pointer, size);
    pv_copy->must_free = TRUE;
    break;
  default:
    pv_copy->data = pv->data;
    break;
  }

  return pv_copy;
}

/****************************************************************************
  Free all allocated memory used by this property value, including calling
  the appropriate free function on the internal data according to its type.
****************************************************************************/
static void propval_free(struct propval *pv)
{
  if (!pv) {
    return;
  }

  if (pv->must_free) {
    switch (pv->valtype) {
    case VALTYPE_PIXBUF:
      g_object_unref(pv->data.v_pixbuf);
      break;
    case VALTYPE_STRING:
    case VALTYPE_BUILT_ARRAY:
    case VALTYPE_INVENTIONS_ARRAY:
      free(pv->data.v_pointer);
      break;
    default:
      freelog(LOG_FATAL, "Unhandled request to free data %p (type %s) "
              "in propval_free().",
              pv->data.v_pointer, valtype_get_name(pv->valtype));
      assert(FALSE);
      break;
    }
    pv->data.v_pointer = NULL;
  }

  free(pv);
}

/****************************************************************************
  Returns TRUE if the two values are equal, in a deep sense.
****************************************************************************/
static bool propval_equal(struct propval *pva,
                          struct propval *pvb)
{
  if (!pva || !pvb) {
    return pva == pvb;
  }

  if (pva->valtype != pvb->valtype) {
    return FALSE;
  }

  switch (pva->valtype) {
  case VALTYPE_STRING:
    if (pva->data.v_const_string && pvb->data.v_const_string) {
      return 0 == strcmp(pva->data.v_const_string, pvb->data.v_const_string);
    }
    break;
  case VALTYPE_BUILT_ARRAY:
    if (pva->data.v_pointer == pvb->data.v_pointer
        || !pva->data.v_pointer || !pvb->data.v_pointer) {
      break;
    }
    improvement_iterate(pimprove) {
      int id, vatb, vbtb;
      id = improvement_index(pimprove);
      vatb = pva->data.v_built[id].turn;
      vbtb = pvb->data.v_built[id].turn;
      if (vatb < 0 && vbtb < 0) {
        continue;
      }
      if (vatb != vbtb) {
        return FALSE;
      }
    } improvement_iterate_end;
    return TRUE;
    break;
  case VALTYPE_INVENTIONS_ARRAY:
    if (pva->data.v_pointer == pvb->data.v_pointer
        || !pva->data.v_pointer || !pvb->data.v_pointer) {
      break;
    }
    advance_index_iterate(A_FIRST, tech) {
      if (pva->data.v_inventions[tech] != pvb->data.v_inventions[tech]) {
        return FALSE;
      }
    } advance_index_iterate_end;
    return TRUE;
    break;
  case VALTYPE_BV_SPECIAL:
    return BV_ARE_EQUAL(pva->data.v_bv_special, pvb->data.v_bv_special);
    break;
  default:
    break;
  }

  return pva->data.v_pointer == pvb->data.v_pointer;
}

/****************************************************************************
  Create a new "property state" record. It keeps track of the modified value
  of a property bound to an object.

  NB: Does NOT make a copy of 'pv'.
****************************************************************************/
static struct propstate *propstate_new(struct objprop *op,
                                       struct propval *pv)
{
  struct propstate *ps;

  if (!op) {
    return NULL;
  }

  ps = fc_calloc(1, sizeof(*ps));
  ps->property_id = objprop_get_id(op);
  ps->property_value = pv;

  return ps;
}

/****************************************************************************
  Removes the stored value, freeing it if needed.
****************************************************************************/
static void propstate_clear_value(struct propstate *ps)
{
  if (!ps) {
    return;
  }

  propval_free(ps->property_value);
  ps->property_value = NULL;
}

/****************************************************************************
  Free a property state and any associated resources.
****************************************************************************/
static void propstate_free(struct propstate *ps)
{
  if (!ps) {
    return;
  }
  propstate_clear_value(ps);
  free(ps);
}

/****************************************************************************
  Replace the stored property value with a new one. The old value will
  be freed if needed.
  
  NB: Does NOT make a copy of 'pv'.
****************************************************************************/
static void propstate_set_value(struct propstate *ps,
                                struct propval *pv)
{
  if (!ps) {
    return;
  }
  propstate_clear_value(ps);
  ps->property_value = pv;
}

/****************************************************************************
  Returns the stored value.

  NB: NOT a copy of the value.
****************************************************************************/
static struct propval *propstate_get_value(struct propstate *ps)
{
  if (!ps) {
    return NULL;
  }
  return ps->property_value;
}

/****************************************************************************
  Create a new object "bind". It serves to bind a set of object properties
  to an object instance.
****************************************************************************/
static struct objbind *objbind_new(int objtype, gpointer object)
{
  struct objbind *ob;
  int id;

  if (object == NULL) {
    return NULL;
  }

  id = objtype_get_id_from_object(objtype, object);

  ob = fc_calloc(1, sizeof(*ob));
  ob->object_id = id;
  ob->objtype = objtype;
  ob->propstate_table = hash_new_full(hash_fval_keyval,
                                      hash_fcmp_keyval, NULL,
                                      (hash_free_fn_t) propstate_free);

  return ob;
}

/****************************************************************************
  Returns the bound object, if it still "exists". Returns NULL on error.
****************************************************************************/
static gpointer objbind_get_object(struct objbind *ob)
{
  int id;
  if (!ob) {
    return NULL;
  }

  id = ob->object_id;

  switch (ob->objtype) {
  case OBJTYPE_TILE:
    return index_to_tile(id);
    break;
  case OBJTYPE_UNIT:
    return game_find_unit_by_number(id);
    break;
  case OBJTYPE_CITY:
    return game_find_city_by_number(id);
    break;
  case OBJTYPE_PLAYER:
    return valid_player_by_number(id);
    break;
  default:
    break;
  }
   
  return NULL;
}

/****************************************************************************
  Returns a newly allocated property value for the given object property
  on the object referenced by the given object bind, or NULL on failure.

  NB: You must call propval_free on the non-NULL return value when it
  no longer needed.
****************************************************************************/
static struct propval *objbind_get_value_from_object(struct objbind *ob,
                                                     struct objprop *op)
{
  int objtype, propid;
  struct tile *ptile;
  struct propval *pv;

  if (!op || !ob) {
    return NULL;
  }

  objtype = objbind_get_objtype(ob);
  propid = objprop_get_id(op);

  pv = fc_calloc(1, sizeof(*pv));
  pv->valtype = objprop_get_valtype(op);

  if (objtype == OBJTYPE_TILE) {
    struct resource *presource;
    struct terrain *pterrain;

    ptile = objbind_get_object(ob);
    if (ptile == NULL) {
      goto FAILED;
    }

    switch (propid) {
    case OPID_TILE_IMAGE:
      pv->data.v_pixbuf = create_tile_pixbuf(ptile);
      pv->must_free = TRUE;
      break;
    case OPID_TILE_ADDRESS:
      pv->data.v_string = g_strdup_printf("%p", ptile);
      pv->must_free = TRUE;
      break;
    case OPID_TILE_TERRAIN:
      pterrain = tile_terrain(ptile);
      if (pterrain) {
        pv->data.v_const_string = terrain_name_translation(pterrain);
      } else {
        pv->data.v_const_string = "";
      }
      break;
    case OPID_TILE_RESOURCE:
      presource = tile_resource(ptile);
      if (presource) {
        pv->data.v_const_string = resource_name_translation(presource);
      } else {
        pv->data.v_const_string = "";
      }
      break;
    case OPID_TILE_XY:
      pv->data.v_string = g_strdup_printf("(%d, %d)", ptile->x, ptile->y);
      pv->must_free = TRUE;
      break;
    case OPID_TILE_INDEX:
      pv->data.v_int = tile_index(ptile);
      break;
    case OPID_TILE_X:
      pv->data.v_int = ptile->x;
      break;
    case OPID_TILE_Y:
      pv->data.v_int = ptile->y;
      break;
    case OPID_TILE_NAT_X:
      pv->data.v_int = ptile->nat_x;
      break;
    case OPID_TILE_NAT_Y:
      pv->data.v_int = ptile->nat_y;
      break;
    case OPID_TILE_CONTINENT:
      pv->data.v_int = ptile->continent;
      break;
    case OPID_TILE_SPECIALS:
      pv->data.v_bv_special = tile_specials(ptile);
      break;
    default:
      freelog(LOG_ERROR, "Unhandled request for value of property %d "
              "(%s) from object of type \"%s\" in "
              "objbind_get_value_from_object().",
              propid, objprop_get_name(op), objtype_get_name(objtype));
      goto FAILED;
      break;
    }

  } else if (objtype == OBJTYPE_UNIT) {
    struct unit *punit = objbind_get_object(ob);
    struct unit_type *putype;
    
    if (punit == NULL) {
      goto FAILED;
    }

    switch (propid) {
    case OPID_UNIT_IMAGE:
      pv->data.v_pixbuf = create_unit_pixbuf(punit);
      pv->must_free = TRUE;      
      break;
    case OPID_UNIT_ADDRESS:
      pv->data.v_string = g_strdup_printf("%p", punit);
      pv->must_free = TRUE;
      break;
    case OPID_UNIT_XY:
      ptile = unit_tile(punit);
      pv->data.v_string = g_strdup_printf("(%d, %d)", ptile->x, ptile->y);
      pv->must_free = TRUE;
      break;
    case OPID_UNIT_ID:
      pv->data.v_int = punit->id;
      break;
    case OPID_UNIT_TYPE:
      putype = unit_type(punit);
      pv->data.v_const_string = utype_name_translation(putype);
      break;
    case OPID_UNIT_MOVES_LEFT:
      pv->data.v_int = punit->moves_left;
      break;
    default:
      freelog(LOG_ERROR, "Unhandled request for value of property %d "
              "(%s) from object of type \"%s\" in "
              "objbind_get_value_from_object().",
              propid, objprop_get_name(op), objtype_get_name(objtype));
      goto FAILED;
      break;
    }

  } else if (objtype == OBJTYPE_CITY) {
    struct city *pcity = objbind_get_object(ob);

    if (pcity == NULL) {
      goto FAILED;
    }

    switch (propid) {
    case OPID_CITY_IMAGE:
      pv->data.v_pixbuf = create_city_pixbuf(pcity);
      pv->must_free = TRUE;
      break;
    case OPID_CITY_ADDRESS:
      pv->data.v_string = g_strdup_printf("%p", pcity);
      pv->must_free = TRUE;
      break;
    case OPID_CITY_XY:
      ptile = city_tile(pcity);
      pv->data.v_string = g_strdup_printf("(%d, %d)", ptile->x, ptile->y);
      pv->must_free = TRUE;
      break;
    case OPID_CITY_ID:
      pv->data.v_int = pcity->id;
      break;
    case OPID_CITY_NAME:
      pv->data.v_const_string = pcity->name;
      break;
    case OPID_CITY_SIZE:
      pv->data.v_int = pcity->size;
      break;
    case OPID_CITY_BUILDINGS:
      pv->data.v_built = pcity->built;
      break;
    default:
      freelog(LOG_ERROR, "Unhandled request for value of property %d "
              "(%s) from object of type \"%s\" in "
              "objbind_get_value_from_object().",
              propid, objprop_get_name(op), objtype_get_name(objtype));
      goto FAILED;
      break;
    }
  } else if (objtype == OBJTYPE_PLAYER) {
    struct player *pplayer = objbind_get_object(ob);

    if (pplayer == NULL) {
      goto FAILED;
    }

    switch (propid) {
    case OPID_PLAYER_NAME:
      pv->data.v_const_string = pplayer->name;
      break;
    case OPID_PLAYER_NATION:
      pv->data.v_nation = nation_of_player(pplayer);
      break;
    case OPID_PLAYER_ADDRESS:
      pv->data.v_string = g_strdup_printf("%p", pplayer);
      pv->must_free = TRUE;
      break;
    case OPID_PLAYER_INVENTIONS:
      pv->data.v_inventions = fc_calloc(A_LAST, sizeof(bool));
      advance_index_iterate(A_FIRST, tech) {
        pv->data.v_inventions[tech]
            = TECH_KNOWN == player_invention_state(pplayer, tech);
      } advance_index_iterate_end;
      pv->must_free = TRUE;
      break;
    default:
      freelog(LOG_ERROR, "Unhandled request for value of property %d "
              "(%s) from object of type \"%s\" in "
              "objbind_get_value_from_object().",
              propid, objprop_get_name(op), objtype_get_name(objtype));
      goto FAILED;
      break;
    }
  } else {
    goto FAILED;
  }

  return pv;

FAILED:
  if (pv) {
    free(pv);
  }
  return NULL;
}

/****************************************************************************
  If applicable, sets the allowed range values of the given object property
  as applied to the bound object. Returns TRUE if values were set.
****************************************************************************/
static bool objbind_get_allowed_value_span(struct objbind *ob,
                                           struct objprop *op,
                                           double *pmin,
                                           double *pmax,
                                           double *pstep,
                                           double *pbig_step)
{
  int propid;
  int objtype;
  bool ok = TRUE;
  double min = 0, max = 1, step = 1, big_step = 1;

  if (!ob || !op) {
    return FALSE;
  }

  propid = objprop_get_id(op);
  objtype = objbind_get_objtype(ob);

  if (objtype == OBJTYPE_UNIT) {
    struct unit *punit = objbind_get_object(ob);
    struct unit_type *putype;

    if (!punit) {
      return FALSE;
    }

    putype = unit_type(punit);

    switch (propid) {
    case OPID_UNIT_MOVES_LEFT:
      min = 0;
      max = putype->move_rate;
      step = 1;
      big_step = 5;
      break;
    default:
      freelog(LOG_ERROR, "Unhandled request for value range of "
              "property %d (%s) from object of type \"%s\" in "
              "objbind_get_allowed_value_span().",
              propid, objprop_get_name(op), objtype_get_name(objtype));
      ok = FALSE;
      break;
    }

  } else if (objtype == OBJTYPE_CITY) {

    switch (propid) {
    case OPID_CITY_SIZE:
      min = 1;
      max = MAX_CITY_SIZE;
      step = 1;
      big_step = 5;
      break;
    default:
      freelog(LOG_ERROR, "Unhandled request for value range of "
              "property %d (%s) from object of type \"%s\" in "
              "objbind_get_allowed_value_span().",
              propid, objprop_get_name(op), objtype_get_name(objtype));
      ok = FALSE;
      break;
    }

  } else if (objtype == OBJTYPE_PLAYER) {

    switch (propid) {
    default:
      freelog(LOG_ERROR, "Unhandled request for value range of "
              "property %d (%s) from object of type \"%s\" in "
              "objbind_get_allowed_value_span().",
              propid, objprop_get_name(op), objtype_get_name(objtype));
      ok = FALSE;
      break;
    }
  } else {
    ok = FALSE;
  }

  if (!ok) {
    return FALSE;
  }

  if (pmin) {   
    *pmin = min;
  }
  if (pmax) {
    *pmax = max;
  }
  if (pstep) {
    *pstep = step;
  }
  if (pbig_step) {
    *pbig_step = big_step;
  }

  return TRUE;
}

/****************************************************************************
  Remove a stored modified value, if it exists.
****************************************************************************/
static void objbind_clear_modified_value(struct objbind *ob,
                                         struct objprop *op)
{
  int propid;

  if (!ob || !op || !ob->propstate_table) {
    return;
  }
  
  propid = objprop_get_id(op);
  hash_delete_entry(ob->propstate_table, GINT_TO_POINTER(propid));
}

/****************************************************************************
  Returns TRUE if a stored modified property value exists for this bound
  object for the given property.
****************************************************************************/
static bool objbind_property_is_modified(struct objbind *ob,
                                         struct objprop *op)
{
  int propid;

  if (!ob || !op) {
    return FALSE;
  }

  if (objprop_is_readonly(op)) {
    return FALSE;
  }

  propid = objprop_get_id(op);
  return hash_key_exists(ob->propstate_table, GINT_TO_POINTER(propid));
}

/****************************************************************************
  Returns TRUE if there are any stored modified values of any of the
  properties of the bound object.
****************************************************************************/
static bool objbind_has_modified_properties(struct objbind *ob)
{
  if (!ob) {
    return FALSE;
  }

  return hash_num_entries(ob->propstate_table) > 0;
}

/****************************************************************************
  Deletes all stored modified property values.
****************************************************************************/
static void objbind_clear_all_modified_values(struct objbind *ob)
{
  if (!ob) {
    return;
  }
  hash_delete_all_entries(ob->propstate_table);
}

/****************************************************************************
  Store a modified property value, but only if it is different from the
  current value. Always makes a copy of the given value when storing.
****************************************************************************/
static void objbind_set_modified_value(struct objbind *ob,
                                       struct objprop *op,
                                       struct propval *pv)
{
  struct propstate *ps;
  int valtype;
  bool equal;
  struct propval *pv_old, *pv_copy;
  int propid;

  if (!ob || !op) {
    return;
  }

  valtype = objprop_get_valtype(op);
  propid = objprop_get_id(op);

  pv_old = objbind_get_value_from_object(ob, op);
  if (!pv_old) {
    return;
  }

  equal = propval_equal(pv, pv_old);
  propval_free(pv_old);

  if (equal) {
    objbind_clear_modified_value(ob, op);
    return;
  }

  pv_copy = propval_copy(pv);

  ps = hash_lookup_data(ob->propstate_table,
                        GINT_TO_POINTER(propid));
  if (!ps) {
    ps = propstate_new(op, pv_copy);
    hash_insert(ob->propstate_table, GINT_TO_POINTER(propid), ps);
  } else {
    propstate_set_value(ps, pv_copy);
  }
}

/****************************************************************************
  Retrieve the stored property value for the bound object, or NULL if none
  exists.

  NB: Does NOT return a copy.
****************************************************************************/
static struct propval *objbind_get_modified_value(struct objbind *ob,
                                                  struct objprop *op)
{
  struct propstate *ps;
  int propid;

  if (!ob || !op) {
    return FALSE;
  }

  propid = objprop_get_id(op);
  ps = hash_lookup_data(ob->propstate_table, GINT_TO_POINTER(propid));
  if (!ps) {
    return NULL;
  }

  return propstate_get_value(ps);
}

/****************************************************************************
  Destroy the object bind and free any resources it might have been using.
****************************************************************************/
static void objbind_free(struct objbind *ob)
{
  if (!ob) {
    return;
  }
  if (ob->propstate_table) {
    hash_free(ob->propstate_table);
    ob->propstate_table = NULL;
  }
  if (ob->rowref) {
    gtk_tree_row_reference_free(ob->rowref);
    ob->rowref = NULL;
  }
  free(ob);
}

/****************************************************************************
  Returns the object type of the bound object.
****************************************************************************/
static int objbind_get_objtype(const struct objbind *ob)
{
  if (!ob) {
    return NUM_OBJTYPES;
  }
  return ob->objtype;
}

/****************************************************************************
  Bind the object in the given objbind to the properties in the page.
****************************************************************************/
static void objbind_bind_properties(struct objbind *ob,
                                    struct property_page *pp)
{
  if (!ob) {
    return;
  }
  ob->parent_property_page = pp;
}

/****************************************************************************
  Fill the packet with the bound object's current state.

  NB: This must be updated if the packet_edit_<object> definitions change.
****************************************************************************/
static void objbind_pack_current_values(struct objbind *ob,
                                        union packetdata pd)
{
  int objtype;

  if (!ob || !pd.v_pointer) {
    return;
  }

  objtype = objbind_get_objtype(ob);

  if (objtype == OBJTYPE_TILE) {
    struct packet_edit_tile *packet = pd.tile;
    struct tile *ptile = objbind_get_object(ob);

    if (!ptile) {
      return;
    }

    packet->id = tile_index(ptile);
    packet->specials = tile_specials(ptile);
    /* TODO: Set more packet fields. */

  } else if (objtype == OBJTYPE_UNIT) {
    struct packet_edit_unit *packet = pd.unit;
    struct unit *punit = objbind_get_object(ob);

    if (!punit) {
      return;
    }

    packet->id = punit->id;
    packet->moves_left = punit->moves_left;
    /* TODO: Set more packet fields. */

  } else if (objtype == OBJTYPE_CITY) {
    struct packet_edit_city *packet = pd.city;
    struct city *pcity = objbind_get_object(ob);
    int i;

    if (!pcity) {
      return;
    }
    
    packet->id = pcity->id;
    sz_strlcpy(packet->name, pcity->name);
    packet->size = pcity->size;
    for (i = 0; i < B_LAST; i++) {
      packet->built[i] = pcity->built[i].turn;
    }
    /* TODO: Set more packet fields. */

  } else if (objtype == OBJTYPE_PLAYER) {
    struct packet_edit_player *packet = pd.player;
    struct player *pplayer = objbind_get_object(ob);
    struct nation_type *pnation;

    if (!pplayer) {
      return;
    }

    packet->id = player_number(pplayer);
    sz_strlcpy(packet->name, pplayer->name);
    pnation = nation_of_player(pplayer);
    packet->nation = nation_index(pnation);
    advance_index_iterate(A_FIRST, tech) {
      packet->inventions[tech]
          = TECH_KNOWN == player_invention_state(pplayer, tech);
    } advance_index_iterate_end;
    /* TODO: Set more packet fields. */

  }
}

/****************************************************************************
  Package the modified property value into the supplied packet.
****************************************************************************/
static void objbind_pack_modified_value(struct objbind *ob,
                                        struct objprop *op,
                                        union packetdata pd)
{
  struct propval *pv;
  int objtype, propid;
  int i;

  if (!op || !ob || !pd.v_pointer) {
    return;
  }

  if (objprop_is_readonly(op) || !objbind_property_is_modified(ob, op)) {
    return;
  }

  pv = objbind_get_modified_value(ob, op);
  if (!pv) {
    return;
  }

  objtype = objbind_get_objtype(ob);
  propid = objprop_get_id(op);

  if (objtype == OBJTYPE_TILE) {
    struct packet_edit_tile *packet = pd.tile;
    struct tile *ptile = objbind_get_object(ob);

    if (!ptile) {
      return;
    }

    switch (propid) {
    case OPID_TILE_SPECIALS:
      packet->specials = pv->data.v_bv_special;
      break;
    default:
      freelog(LOG_ERROR, "Unhandled request to pack value of "
              "property %d (%s) from object of type \"%s\" in "
              "objbind_pack_modified_value().",
              propid, objprop_get_name(op), objtype_get_name(objtype));
      break;
    }

  } else if (objtype == OBJTYPE_UNIT) {
    struct packet_edit_unit *packet = pd.unit;
    struct unit *punit = objbind_get_object(ob);

    if (!punit) {
      return;
    }

    switch (propid) {
    case OPID_UNIT_MOVES_LEFT:
      packet->moves_left = pv->data.v_int;
      break;
    default:
      freelog(LOG_ERROR, "Unhandled request to pack value of "
              "property %d (%s) from object of type \"%s\" in "
              "objbind_pack_modified_value().",
              propid, objprop_get_name(op), objtype_get_name(objtype));
      break;
    }

  } else if (objtype == OBJTYPE_CITY) {
    struct packet_edit_city *packet = pd.city;
    struct city *pcity = objbind_get_object(ob);

    if (!pcity) {
      return;
    }

    switch (propid) {
    case OPID_CITY_NAME:
      sz_strlcpy(packet->name, pv->data.v_string);
      break;
    case OPID_CITY_SIZE:
      packet->size = pv->data.v_int;
      break;
    case OPID_CITY_BUILDINGS:
      for (i = 0; i < B_LAST; i++) {
        packet->built[i] = pv->data.v_built[i].turn;
      }
      break;
    default:
      freelog(LOG_ERROR, "Unhandled request to pack value of "
              "property %d (%s) from object of type \"%s\" in "
              "objbind_pack_modified_value().",
              propid, objprop_get_name(op), objtype_get_name(objtype));
      break;
    }
  } else if (objtype == OBJTYPE_PLAYER) {
    struct packet_edit_player *packet = pd.player;
    struct player *pplayer = objbind_get_object(ob);

    if (!pplayer) {
      return;
    }

    switch (propid) {
    case OPID_PLAYER_NAME:
      sz_strlcpy(packet->name, pv->data.v_string);
      break;
    case OPID_PLAYER_NATION:
      packet->nation = nation_index(pv->data.v_nation);
      break;
    case OPID_PLAYER_INVENTIONS:
      advance_index_iterate(A_FIRST, tech) {
        packet->inventions[tech] = pv->data.v_inventions[tech];
      } advance_index_iterate_end;
      break;
    default:
      freelog(LOG_ERROR, "Unhandled request to pack value of "
              "property %d (%s) from object of type \"%s\" in "
              "objbind_pack_modified_value().",
              propid, objprop_get_name(op), objtype_get_name(objtype));
      break;
    }
  }
}

/****************************************************************************
  Sets the row reference in a GtkTreeModel of this objbind.
****************************************************************************/
static void objbind_set_rowref(struct objbind *ob,
                               GtkTreeRowReference *rr)
{
  if (!ob) {
    return;
  }
  ob->rowref = rr;
}

/****************************************************************************
  Returns the row reference of this objbind, or NULL if not applicable.
****************************************************************************/
static GtkTreeRowReference *objbind_get_rowref(struct objbind *ob)
{
  if (!ob) {
    return NULL;
  }
  return ob->rowref;
}

/****************************************************************************
  Returns the unique property identifier for this object property.
****************************************************************************/
static int objprop_get_id(const struct objprop *op)
{
  if (!op) {
    return -1;
  }
  return op->id;
}

/****************************************************************************
  Returns the GType that this object property renders as in a GtkTreeView.
  Returning G_TYPE_NONE indicates that this property cannot be viewed
  in a list.

  NB: This must correspond to the actions in property_page_set_store_value.
****************************************************************************/
static GType objprop_get_gtype(const struct objprop *op)
{
  if (!op) {
    return G_TYPE_NONE;
  }
  switch (op->valtype) {
  case VALTYPE_INT:
    return G_TYPE_INT;
    break;
  case VALTYPE_STRING:
  case VALTYPE_BUILT_ARRAY:
  case VALTYPE_INVENTIONS_ARRAY:
  case VALTYPE_BV_SPECIAL:
    return G_TYPE_STRING;
    break;
  case VALTYPE_PIXBUF:
  case VALTYPE_NATION:
    return GDK_TYPE_PIXBUF;
    break;
  default:
    break;
  }
  return G_TYPE_NONE;
}

/****************************************************************************
  Returns the value type of this property value (one of enum value_types).
****************************************************************************/
static int objprop_get_valtype(const struct objprop *op)
{
  if (!op) {
    return VALTYPE_NONE;
  }
  return op->valtype;
}

/****************************************************************************
  Returns TRUE if this object property can be viewed in a GtkTreeView.
****************************************************************************/
static bool objprop_show_in_listview(const struct objprop *op)
{
  if (!op) {
    return FALSE;
  }
  return op->flags & OPF_IN_LISTVIEW;
}

/****************************************************************************
  Returns TRUE if this object property can create and use a property widget.
****************************************************************************/
static bool objprop_has_widget(const struct objprop *op)
{
  if (!op) {
    return FALSE;
  }
  return op->flags & OPF_HAS_WIDGET;
}

/****************************************************************************
  Returns a the string corresponding to the attribute type name required
  for gtk_tree_view_column_new_with_attributes according to this objprop's
  GType value. May return NULL if it does not make sense for this
  object property.
****************************************************************************/
static const char *objprop_get_attribute_type_string(const struct objprop *op)
{
  GType gtype;

  if (!op) {
    return NULL;
  }

  gtype = objprop_get_gtype(op);
  if (gtype == G_TYPE_INT || gtype == G_TYPE_STRING) {
    return "text";
  } else if (gtype == GDK_TYPE_PIXBUF) {
    return "pixbuf";
  }

  return NULL;
}

/****************************************************************************
  Store the column id of the list store that this object property can be
  viewed in. This should generally only be changed set once, when the
  object property's associated list view is created.
****************************************************************************/
static void objprop_set_column_id(struct objprop *op, int col_id)
{
  if (!op) {
    return;
  }
  op->column_id = col_id;
}

/****************************************************************************
  Returns the column id in the list store for this object property.
  May return -1 if not applicable or if not yet set.
****************************************************************************/
static int objprop_get_column_id(const struct objprop *op)
{
  if (!op) {
    return -1;
  }
  return op->column_id;
}

/****************************************************************************
  Returns the string name of this object property.
****************************************************************************/
static const char *objprop_get_name(const struct objprop *op)
{
  if (!op) {
    return NULL;
  }
  return op->name;
}

/****************************************************************************
  Create and return a cell renderer corresponding to this object property,
  suitable to be used with a tree view. May return NULL if this object
  property cannot exist in a list store.
****************************************************************************/
static GtkCellRenderer *objprop_create_cell_renderer(const struct objprop *op)
{
  GtkCellRenderer *cell = NULL;
  GType gtype;

  gtype = objprop_get_gtype(op);

  if (gtype == G_TYPE_INT || gtype == G_TYPE_STRING) {
    cell = gtk_cell_renderer_text_new();
  } else if (gtype == GDK_TYPE_PIXBUF) {
    cell = gtk_cell_renderer_pixbuf_new();
  }

  return cell;
}

/****************************************************************************
  Return TRUE if the given object property can be sorted (i.e. in the list
  view).
****************************************************************************/
static bool objprop_is_sortable(const struct objprop *op)
{
  GType gtype;
  if (!op) {
    return FALSE;
  }
  gtype = objprop_get_gtype(op);
  return gtype == G_TYPE_INT || gtype == G_TYPE_STRING;
}

/****************************************************************************
  Return TRUE if the given object property cannot be edited (i.e. it is
  displayed information only).
****************************************************************************/
static bool objprop_is_readonly(const struct objprop *op)
{
  if (!op) {
    return TRUE;
  }
  return !(op->flags & OPF_EDITABLE);
}

/****************************************************************************
  Callback for entry widget 'activate' signal.
****************************************************************************/
static void objprop_widget_entry_activated(GtkEntry *entry,
                                           gpointer userdata)
{
  struct objprop *op;
  struct property_page *pp;
  struct propval value = {{0,}, VALTYPE_STRING, FALSE};

  op = userdata;
  pp = objprop_get_property_page(op);
  value.data.v_const_string = gtk_entry_get_text(entry);

  property_page_change_value(pp, op, &value);  
}

/****************************************************************************
  Callback for spin button widget 'value-changed' signal.
****************************************************************************/
static void objprop_widget_spin_button_changed(GtkSpinButton *spin,
                                               gpointer userdata)
{
  struct objprop *op;
  struct property_page *pp;
  struct propval value = {{0,}, VALTYPE_INT, FALSE};

  op = userdata;
  pp = objprop_get_property_page(op);
  value.data.v_int = gtk_spin_button_get_value_as_int(spin);

  property_page_change_value(pp, op, &value);  
}

/****************************************************************************
  Create the gtk widget used to edit or display this object property.
****************************************************************************/
static void objprop_setup_widget(struct objprop *op)
{
  GtkWidget *w = NULL;
  GtkWidget *hbox, *hbox2, *label, *image, *entry, *spin;
  struct extviewer *ev = NULL;
  int propid;

  if (!op) {
    return;
  }

  if (!objprop_has_widget(op)) {
    return;
  }

  hbox = gtk_hbox_new(TRUE, 4);
  w = hbox;
  op->widget = w;

  label = gtk_label_new(NULL);
  gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
  gtk_box_pack_start(GTK_BOX(hbox), label, TRUE, TRUE, 0);
  objprop_set_child_widget(op, "name-label", label);

  propid = objprop_get_id(op);

  switch (propid) {

  case OPID_TILE_ADDRESS:
  case OPID_TILE_INDEX:
  case OPID_TILE_X:
  case OPID_TILE_Y:
  case OPID_TILE_NAT_X:
  case OPID_TILE_NAT_Y:
  case OPID_TILE_CONTINENT:
  case OPID_TILE_TERRAIN:
  case OPID_TILE_RESOURCE:
  case OPID_TILE_XY:
  case OPID_UNIT_ADDRESS:
  case OPID_UNIT_ID:
  case OPID_UNIT_XY:
  case OPID_UNIT_TYPE:
  case OPID_CITY_ADDRESS:
  case OPID_CITY_ID:
  case OPID_CITY_XY:
  case OPID_PLAYER_ADDRESS:
    label = gtk_label_new(NULL);
    gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
    gtk_box_pack_start(GTK_BOX(hbox), label, TRUE, TRUE, 0);
    objprop_set_child_widget(op, "value-label", label);
    break;

  case OPID_TILE_IMAGE:
  case OPID_UNIT_IMAGE:
  case OPID_CITY_IMAGE:
    image = gtk_image_new();
    gtk_misc_set_alignment(GTK_MISC(image), 0.0, 0.5);
    gtk_box_pack_start(GTK_BOX(hbox), image, TRUE, TRUE, 0);
    objprop_set_child_widget(op, "image", image);
    break;

  case OPID_CITY_NAME:
  case OPID_PLAYER_NAME:
    entry = gtk_entry_new();
    gtk_entry_set_width_chars(GTK_ENTRY(entry), 8);
    g_signal_connect(entry, "activate",
                     G_CALLBACK(objprop_widget_entry_activated), op);                     
    gtk_box_pack_start(GTK_BOX(hbox), entry, TRUE, TRUE, 0);
    objprop_set_child_widget(op, "entry", entry);
    break;

  case OPID_CITY_SIZE:
    spin = gtk_spin_button_new_with_range(0.0, 100.0, 1.0);
    g_signal_connect(spin, "value-changed",
                     G_CALLBACK(objprop_widget_spin_button_changed), op);
    gtk_box_pack_start(GTK_BOX(hbox), spin, TRUE, TRUE, 0);
    objprop_set_child_widget(op, "spin", spin);
    break;

  case OPID_UNIT_MOVES_LEFT:
    hbox2 = gtk_hbox_new(FALSE, 4);
    gtk_box_pack_start(GTK_BOX(hbox), hbox2, TRUE, TRUE, 0);
    spin = gtk_spin_button_new_with_range(0.0, 100.0, 1.0);
    g_signal_connect(spin, "value-changed",
                     G_CALLBACK(objprop_widget_spin_button_changed), op);
    gtk_box_pack_start(GTK_BOX(hbox2), spin, TRUE, TRUE, 0);
    objprop_set_child_widget(op, "spin", spin);
    label = gtk_label_new(NULL);
    gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
    gtk_box_pack_start(GTK_BOX(hbox2), label, TRUE, TRUE, 0);
    objprop_set_child_widget(op, "max-value-label", label);
    break;

  case OPID_TILE_SPECIALS:
  case OPID_CITY_BUILDINGS:
  case OPID_PLAYER_NATION:
  case OPID_PLAYER_INVENTIONS:
    ev = extviewer_new(op);
    objprop_set_extviewer(op, ev);
    gtk_box_pack_start(GTK_BOX(hbox), extviewer_get_panel_widget(ev),
                       TRUE, TRUE, 0);
    property_page_add_extviewer(objprop_get_property_page(op), ev);
    break;

  default:
    freelog(LOG_ERROR, "Unhandled request to create widget for "
            "property %d (%s) in objprop_setup_widget().",
            propid, objprop_get_name(op));
    break;
  }

  return;
}

/****************************************************************************
  Refresh the display/edit widget corresponding to this object property
  according to the value of the bound object. If a stored modified value
  exists, then check it against the object's current value and remove it
  if they are equal.
  
  If 'ob' is NULL, then clear the widget.
****************************************************************************/
static void objprop_refresh_widget(struct objprop *op,
                                   struct objbind *ob)
{
  GtkWidget *w, *label, *image, *entry, *spin;
  struct extviewer *ev;
  struct propval *pv;
  bool modified;
  int propid;
  double min, max, step, big_step;
  char buf[256];

  if (!op) {
    return;
  }

  if (!objprop_has_widget(op)) {
    return;
  }

  w = objprop_get_widget(op);
  if (!w) {
    return;
  }

  propid = objprop_get_id(op);

  /* NB: We must take care to propval_free the return value of
   * objbind_get_value_from_object, since it always makes a
   * copy, but to NOT free the result of objbind_get_modified_value
   * since it returns its own stored value. */
  pv = objbind_get_value_from_object(ob, op);
  modified = objbind_property_is_modified(ob, op);

  if (pv && modified) {
    struct propval *pv_mod;
   
    pv_mod = objbind_get_modified_value(ob, op);
    if (pv_mod) {
      if (propval_equal(pv, pv_mod)) {
        objbind_clear_modified_value(ob, op);
        modified = FALSE;
      } else {
        propval_free(pv);
        pv = pv_mod;
        modified = TRUE;
      }
    } else {
      modified = FALSE;
    }
  }

  switch (propid) {

  case OPID_TILE_IMAGE:
  case OPID_UNIT_IMAGE:
  case OPID_CITY_IMAGE:
    image = objprop_get_child_widget(op, "image");
    if (pv) {
      gtk_image_set_from_pixbuf(GTK_IMAGE(image), pv->data.v_pixbuf);
    } else {
      gtk_image_set_from_pixbuf(GTK_IMAGE(image), NULL);
    }
    break;

  case OPID_TILE_ADDRESS:
  case OPID_TILE_XY:
  case OPID_TILE_TERRAIN:
  case OPID_TILE_RESOURCE:
  case OPID_UNIT_ADDRESS:
  case OPID_UNIT_XY:
  case OPID_UNIT_TYPE:
  case OPID_CITY_ADDRESS:
  case OPID_CITY_XY:
  case OPID_PLAYER_ADDRESS:
    label = objprop_get_child_widget(op, "value-label");
    if (pv) {
      gtk_label_set_text(GTK_LABEL(label), pv->data.v_string);
    } else {
      gtk_label_set_text(GTK_LABEL(label), NULL);
    }
    break;

  case OPID_TILE_INDEX:
  case OPID_TILE_X:
  case OPID_TILE_Y:
  case OPID_TILE_NAT_X:
  case OPID_TILE_NAT_Y:
  case OPID_TILE_CONTINENT:
  case OPID_UNIT_ID:
  case OPID_CITY_ID:
    label = objprop_get_child_widget(op, "value-label");
    if (pv) {
      char buf[16];
      my_snprintf(buf, sizeof(buf), "%d", pv->data.v_int);
      gtk_label_set_text(GTK_LABEL(label), buf);
    } else {
      gtk_label_set_text(GTK_LABEL(label), NULL);
    }
    break;

  case OPID_CITY_NAME:
  case OPID_PLAYER_NAME:
    entry = objprop_get_child_widget(op, "entry");
    if (pv) {
      gtk_entry_set_text(GTK_ENTRY(entry), pv->data.v_string);
    } else {
      gtk_entry_set_text(GTK_ENTRY(entry), "");
    }
    gtk_widget_set_sensitive(entry, pv != NULL);
    break;

  case OPID_CITY_SIZE:
    spin = objprop_get_child_widget(op, "spin");
    if (pv) {
      disable_widget_callback(spin,
          G_CALLBACK(objprop_widget_spin_button_changed));
      if (objbind_get_allowed_value_span(ob, op, &min, &max,
                                         &step, &big_step)) {
        gtk_spin_button_set_range(GTK_SPIN_BUTTON(spin), min, max);
        gtk_spin_button_set_increments(GTK_SPIN_BUTTON(spin),
                                       step, big_step);
      }
      gtk_spin_button_set_value(GTK_SPIN_BUTTON(spin), pv->data.v_int);
      enable_widget_callback(spin,
          G_CALLBACK(objprop_widget_spin_button_changed));
    }
    gtk_widget_set_sensitive(spin, pv != NULL);
    break;

  case OPID_UNIT_MOVES_LEFT:
    spin = objprop_get_child_widget(op, "spin");
    label = objprop_get_child_widget(op, "max-value-label");
    if (pv) {
      disable_widget_callback(spin,
          G_CALLBACK(objprop_widget_spin_button_changed));
      if (objbind_get_allowed_value_span(ob, op, &min, &max,
                                         &step, &big_step)) {
        gtk_spin_button_set_range(GTK_SPIN_BUTTON(spin), min, max);
        gtk_spin_button_set_increments(GTK_SPIN_BUTTON(spin),
                                       step, big_step);
        my_snprintf(buf, sizeof(buf), "/%d", (int) max);
        gtk_label_set_text(GTK_LABEL(label), buf);
      } else {
        gtk_label_set_text(GTK_LABEL(label), NULL);
      }
      gtk_spin_button_set_value(GTK_SPIN_BUTTON(spin), pv->data.v_int);
      enable_widget_callback(spin,
          G_CALLBACK(objprop_widget_spin_button_changed));
    } else {
      gtk_label_set_text(GTK_LABEL(label), NULL);
    }
    gtk_widget_set_sensitive(spin, pv != NULL);
    break;

  case OPID_TILE_SPECIALS:
  case OPID_CITY_BUILDINGS:
  case OPID_PLAYER_NATION:
  case OPID_PLAYER_INVENTIONS:
    ev = objprop_get_extviewer(op);
    if (pv) {
      extviewer_refresh_widgets(ev, pv);
    } else {
      extviewer_clear_widgets(ev);
    }
    break;

  default:
    freelog(LOG_ERROR, "Widget refresh missing in "
            "objprop_refresh_widget() for objprop id=%d "
            "name \"%s\".", propid, objprop_get_name(op));
    break;
  }

  if (!modified) {
    propval_free(pv);
  }

  label = objprop_get_child_widget(op, "name-label");
  if (label) {
    const char *name = objprop_get_name(op);
    if (modified) {
      char buf[128];
      my_snprintf(buf, sizeof(buf),
                  "<span foreground=\"red\">%s</span>", name);
      gtk_label_set_markup(GTK_LABEL(label), buf);
    } else {
      gtk_label_set_text(GTK_LABEL(label), name);
    }
  }
}

/****************************************************************************
  Returns the gtk widget used to display or edit this property, or NULL
  if not applicable.
****************************************************************************/
static GtkWidget *objprop_get_widget(struct objprop *op)
{
  if (!op) {
    return NULL;
  }
  if (!op->widget) {
    objprop_setup_widget(op);
  }
  return op->widget;
}

/****************************************************************************
  Stores the given widget under the given name. This function will have no
  effect if objprop_get_widget does not return a valid GtkWidget instance.
****************************************************************************/
static void objprop_set_child_widget(struct objprop *op,
                                     const char *widget_name,
                                     GtkWidget *widget)
{
  GtkWidget *w;

  if (!op || !widget_name || !widget) {
    return;
  }

  w = objprop_get_widget(op);
  if (!w) {
    freelog(LOG_ERROR, "Cannot store child widget %p under name "
            "\"%s\" using objprop_set_child_widget for object "
            "property %d (%s) because objprop_get_widget does "
            "not return a valid widget.",
            widget, widget_name, objprop_get_id(op),
            objprop_get_name(op));
    return;
  }

  g_object_set_data(G_OBJECT(w), widget_name, widget);
}

/****************************************************************************
  Retrieves the widget stored under the given name, or returns NULL and
  logs an error message if not found.
****************************************************************************/
static GtkWidget *objprop_get_child_widget(struct objprop *op,
                                           const char *widget_name)
{
  GtkWidget *w, *child;

  if (!op || !widget_name) {
    return NULL;
  }

  w = objprop_get_widget(op);
  if (!w) {
    freelog(LOG_ERROR, "Cannot retrieve child widget under name "
            "\"%s\" using objprop_get_child_widget for object "
            "property %d (%s) because objprop_get_widget does "
            "not return a valid widget.",
            widget_name, objprop_get_id(op), objprop_get_name(op));
    return NULL;
  }

  child = g_object_get_data(G_OBJECT(w), widget_name);
  if (!child) {
    freelog(LOG_ERROR, "Child widget \"%s\" not found for object "
            "property %d (%s) via objprop_get_child_widget.",
            widget_name, objprop_get_id(op), objprop_get_name(op));
    return NULL;
  }

  return child;
}

/****************************************************************************
  Store the extviewer struct for later retrieval.
****************************************************************************/
static void objprop_set_extviewer(struct objprop *op,
                                  struct extviewer *ev)
{
  if (!op) {
    return;
  }
  op->extviewer = ev;
}

/****************************************************************************
  Return the stored extviewer, or NULL if none exists.
****************************************************************************/
static struct extviewer *objprop_get_extviewer(struct objprop *op)
{
  if (!op) {
    return NULL;
  }
  return op->extviewer;
}

/****************************************************************************
  Get the property page in which this property is installed.
****************************************************************************/
static struct property_page *objprop_get_property_page(const struct objprop *op)
{
  if (!op) {
    return NULL;
  }
  return op->parent_page;
}

/****************************************************************************
  Create a new object property.
****************************************************************************/
static struct objprop *objprop_new(int id,
                                   const char *name,
                                   int flags,
                                   int valtype,
                                   struct property_page *parent)
{
  struct objprop *op;

  op = fc_calloc(1, sizeof(*op));
  op->id = id;
  op->name = name;
  op->flags = flags;
  op->valtype = valtype;
  op->column_id = -1;
  op->parent_page = parent;

  return op;
}

/****************************************************************************
  Create "extended property viewer". This is used for viewing/editing
  complex property values (e.g. arrays, bitvectors, etc.).
****************************************************************************/
static struct extviewer *extviewer_new(struct objprop *op)
{
  struct extviewer *ev;
  GtkWidget *hbox, *vbox, *label, *button, *scrollwin, *image;
  GtkWidget *view = NULL;
  GtkTreeSelection *sel;
  GtkListStore *store = NULL;
  int propid;

  if (!op) {
    return NULL;
  }

  ev = fc_calloc(1, sizeof(*ev));
  ev->objprop = op;

  propid = objprop_get_id(op);


  /* Create the panel widget. */

  switch (propid) {
  case OPID_TILE_SPECIALS:
  case OPID_CITY_BUILDINGS:
  case OPID_PLAYER_INVENTIONS:
    hbox = gtk_hbox_new(FALSE, 4);
    ev->panel_widget = hbox;

    label = gtk_label_new(NULL);
    gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
    gtk_box_pack_start(GTK_BOX(hbox), label, TRUE, TRUE, 0);
    ev->panel_label = label;
    break;

  case OPID_PLAYER_NATION:
    vbox = gtk_vbox_new(FALSE, 4);
    ev->panel_widget = vbox;

    label = gtk_label_new(NULL);
    gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
    gtk_box_pack_start(GTK_BOX(vbox), label, TRUE, TRUE, 0);
    ev->panel_label = label;

    hbox = gtk_hbox_new(FALSE, 4);
    gtk_box_pack_start(GTK_BOX(vbox), hbox, TRUE, TRUE, 0);

    image = gtk_image_new();
    gtk_misc_set_alignment(GTK_MISC(image), 0.0, 0.5);
    gtk_box_pack_start(GTK_BOX(hbox), image, TRUE, TRUE, 0);
    ev->panel_image = image;
    break;

  default:
    hbox = gtk_hbox_new(FALSE, 4);
    ev->panel_widget = hbox;
    break;
  }

  button = gtk_button_new_with_label(_("Edit"));
  g_signal_connect(button, "clicked",
                   G_CALLBACK(extviewer_panel_button_clicked), ev);
  gtk_box_pack_start(GTK_BOX(hbox), button, FALSE, FALSE, 0);
  ev->panel_button = button;

  
  /* Create the data store. */

  switch (propid) {

  case OPID_TILE_SPECIALS:
  case OPID_PLAYER_INVENTIONS:
    store = gtk_list_store_new(3, G_TYPE_BOOLEAN,
                               G_TYPE_INT,
                               G_TYPE_STRING);
    break;

  case OPID_CITY_BUILDINGS:
    store = gtk_list_store_new(4, G_TYPE_BOOLEAN,
                               G_TYPE_INT,
                               G_TYPE_STRING,
                               G_TYPE_STRING);
    break;

  case OPID_PLAYER_NATION:
    store = gtk_list_store_new(4, G_TYPE_BOOLEAN,
                               G_TYPE_INT,
                               GDK_TYPE_PIXBUF,
                               G_TYPE_STRING);
    break;

  default:
    freelog(LOG_ERROR, "Unhandled request to create data store "
            "for property %d (%s) in extviewer_new().",
            propid, objprop_get_name(op));
    break;
  }

  ev->store = store;


  /* Create the view widget. */

  vbox = gtk_vbox_new(FALSE, 4);
  gtk_container_set_border_width(GTK_CONTAINER(vbox), 4);
  ev->view_widget = vbox;

  label = gtk_label_new(objprop_get_name(op));
  gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
  gtk_box_pack_start(GTK_BOX(vbox), label, FALSE, FALSE, 0);
  ev->view_label = label;

  if (store) {
    scrollwin = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(scrollwin),
                                        GTK_SHADOW_ETCHED_IN);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrollwin),
                                   GTK_POLICY_AUTOMATIC,
                                   GTK_POLICY_AUTOMATIC);
    gtk_box_pack_start(GTK_BOX(vbox), scrollwin, TRUE, TRUE, 0);

    view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
    gtk_tree_view_set_rules_hint(GTK_TREE_VIEW(view), TRUE);
    gtk_container_add(GTK_CONTAINER(scrollwin), view);

    sel = gtk_tree_view_get_selection(GTK_TREE_VIEW(view));
    gtk_tree_selection_set_mode(sel, GTK_SELECTION_MULTIPLE);
  }

  switch (propid) {

  case OPID_TILE_SPECIALS:
    /* TRANS: As in "this tile special is present". */
    add_column(view, 0, _("Present"), G_TYPE_BOOLEAN, TRUE, FALSE,
               G_CALLBACK(extviewer_view_cell_toggled), ev);
    add_column(view, 1, _("ID"), G_TYPE_INT,
               FALSE, FALSE, NULL, NULL);
    add_column(view, 2, _("Name"), G_TYPE_STRING,
               FALSE, FALSE, NULL, NULL);
    break;

  case OPID_CITY_BUILDINGS:
    /* TRANS: As in "this building is present". */
    add_column(view, 0, _("Present"), G_TYPE_BOOLEAN, TRUE, FALSE,
               G_CALLBACK(extviewer_view_cell_toggled), ev);
    add_column(view, 1, _("ID"), G_TYPE_INT,
               FALSE, FALSE, NULL, NULL);
    add_column(view, 2, _("Name"), G_TYPE_STRING,
               FALSE, FALSE, NULL, NULL);
    add_column(view, 3, _("Turn Built"), G_TYPE_STRING,
               FALSE, FALSE, NULL, NULL);
    break;

  case OPID_PLAYER_NATION:
    /* TRANS: As in "the player has set this nation". */
    add_column(view, 0, _("Set"), G_TYPE_BOOLEAN, TRUE, TRUE,
               G_CALLBACK(extviewer_view_cell_toggled), ev);
    add_column(view, 1, _("ID"), G_TYPE_INT,
               FALSE, FALSE, NULL, NULL);
    add_column(view, 2, _("Flag"), GDK_TYPE_PIXBUF,
               FALSE, FALSE, NULL, NULL);
    add_column(view, 3, _("Name"), G_TYPE_STRING,
               FALSE, FALSE, NULL, NULL);
    break;

  case OPID_PLAYER_INVENTIONS:
    /* TRANS: As in "this invention is known". */
    add_column(view, 0, _("Known"), G_TYPE_BOOLEAN, TRUE, FALSE,
               G_CALLBACK(extviewer_view_cell_toggled), ev);
    add_column(view, 1, _("ID"), G_TYPE_INT,
               FALSE, FALSE, NULL, NULL);
    add_column(view, 2, _("Name"), G_TYPE_STRING,
               FALSE, FALSE, NULL, NULL);
    break;

  default:
    freelog(LOG_ERROR, "Unhandled request to configure view widget "
            "for property %d (%s) in extviewer_new().",
            propid, objprop_get_name(op));
    break;
  }

  gtk_widget_show_all(ev->panel_widget);
  gtk_widget_show_all(ev->view_widget);

  return ev;
}

/****************************************************************************
  Returns the object property that is displayed by this extviewer.
****************************************************************************/
static struct objprop *extviewer_get_objprop(struct extviewer *ev)
{
  if (!ev) {
    return NULL;
  }
  return ev->objprop;
}

/****************************************************************************
  Returns the "panel widget" for this extviewer, i.e. the widget the
  is to be placed into the properties panel.
****************************************************************************/
static GtkWidget *extviewer_get_panel_widget(struct extviewer *ev)
{
  if (!ev) {
    return NULL;
  }
  return ev->panel_widget;
}

/****************************************************************************
  Returns the "view widget" for this extviewer, i.e. the widget the
  is to be used for viewing/editing a complex property.
****************************************************************************/
static GtkWidget *extviewer_get_view_widget(struct extviewer *ev)
{
  if (!ev) {
    return NULL;
  }
  return ev->view_widget;
}

/****************************************************************************
  Set the widgets in the extended property viewer to display the given value.
****************************************************************************/
static void extviewer_refresh_widgets(struct extviewer *ev,
                                      struct propval *pv)
{
  struct objprop *op;
  int propid, valtype;
  int id, turn_built;
  bool present;
  char buf[128];
  const char *name;
  GdkPixbuf *pixbuf;
  GtkListStore *store;
  GtkTreeIter iter;

  if (!ev) {
    return;
  }

  op = extviewer_get_objprop(ev);
  propid = objprop_get_id(op);
  valtype = objprop_get_valtype(op);

  if (propval_equal(pv, ev->pv_cached)) {
    return;
  }
  propval_free(ev->pv_cached);
  ev->pv_cached = propval_copy(pv);
  store = ev->store;


  switch (propid) {

  case OPID_TILE_SPECIALS:
    gtk_list_store_clear(store);
    tile_special_type_iterate(spe) {
      id = spe;
      name = special_name_translation(spe);
      present = BV_ISSET(pv->data.v_bv_special, spe);
      gtk_list_store_append(store, &iter);
      gtk_list_store_set(store, &iter, 0, present, 1, id, 2, name, -1);
    } tile_special_type_iterate_end;
    propval_as_string(pv, buf, sizeof(buf));
    gtk_label_set_text(GTK_LABEL(ev->panel_label), buf);
    break;

  case OPID_CITY_BUILDINGS:
    gtk_list_store_clear(store);
    improvement_iterate(pimprove) {
      if (is_special_improvement(pimprove)) {
        continue;
      }
      id = improvement_index(pimprove);
      name = improvement_name_translation(pimprove);
      turn_built = pv->data.v_built[id].turn;
      present = turn_built >= 0;
      built_status_to_string(buf, sizeof(buf), &pv->data.v_built[id]);

      gtk_list_store_append(store, &iter);
      gtk_list_store_set(store, &iter, 0, present, 1, id, 2, name,
                         3, buf, -1);
    } improvement_iterate_end;
    propval_as_string(pv, buf, sizeof(buf));
    gtk_label_set_text(GTK_LABEL(ev->panel_label), buf);
    break;

  case OPID_PLAYER_NATION:
    gtk_list_store_clear(store);
    nations_iterate(pnation) {
      present = (pnation == pv->data.v_nation);
      id = nation_index(pnation);
      pixbuf = get_flag(pnation);
      name = nation_adjective_translation(pnation);
      gtk_list_store_append(store, &iter);
      gtk_list_store_set(store, &iter, 0, present, 1, id,
                         2, pixbuf, 3, name, -1);
      if (pixbuf) {
        g_object_unref(pixbuf);
      }
    } nations_iterate_end;
    gtk_label_set_text(GTK_LABEL(ev->panel_label),
                       nation_adjective_translation(pv->data.v_nation));
    pixbuf = get_flag(pv->data.v_nation);
    gtk_image_set_from_pixbuf(GTK_IMAGE(ev->panel_image), pixbuf);
    if (pixbuf) {
      g_object_unref(pixbuf);
    }
    break;

  case OPID_PLAYER_INVENTIONS:
    gtk_list_store_clear(store);
    advance_iterate(A_FIRST, padvance) {
      id = advance_index(padvance);
      present = pv->data.v_inventions[id];
      name = advance_name_translation(padvance);
      gtk_list_store_append(store, &iter);
      gtk_list_store_set(store, &iter, 0, present, 1, id, 2, name, -1);
    } advance_iterate_end;
    propval_as_string(pv, buf, sizeof(buf));
    gtk_label_set_text(GTK_LABEL(ev->panel_label), buf);
    break;

  default:
    freelog(LOG_ERROR, "Unhandled request to refresh widgets "
            "extviwer_refresh_widgets() for objprop id=%d "
            "name \"%s\".", propid, objprop_get_name(op));
    break;
  }
}

/****************************************************************************
  Clear the display widgets.
****************************************************************************/
static void extviewer_clear_widgets(struct extviewer *ev)
{
  struct objprop *op;
  int propid, valtype;

  if (!ev) {
    return;
  }

  op = extviewer_get_objprop(ev);
  propid = objprop_get_id(op);
  valtype = objprop_get_valtype(op);

  propval_free(ev->pv_cached);
  ev->pv_cached = NULL;

  switch (propid) {
  case OPID_TILE_SPECIALS:
  case OPID_CITY_BUILDINGS:
  case OPID_PLAYER_INVENTIONS:
    gtk_list_store_clear(ev->store);
    gtk_label_set_text(GTK_LABEL(ev->panel_label), NULL);
    break;
  case OPID_PLAYER_NATION:
    gtk_list_store_clear(ev->store);
    gtk_label_set_text(GTK_LABEL(ev->panel_label), NULL);
    gtk_image_set_from_pixbuf(GTK_IMAGE(ev->panel_image), NULL);
    break;
  default:
    freelog(LOG_ERROR, "Unhandled request to clear widgets "
            "in extviwer_clear_widgets() for objprop id=%d "
            "name \"%s\".", propid, objprop_get_name(op));
    break;
  }
}

/****************************************************************************
  Handle the extviewer's panel button click. This should set the property
  page to display the view widget for this complex property.
****************************************************************************/
static void extviewer_panel_button_clicked(GtkButton *button,
                                           gpointer userdata)
{
  struct extviewer *ev;
  struct property_page *pp;
  struct objprop *op;
  
  ev = userdata;
  if (!ev) {
    return;
  }

  op = extviewer_get_objprop(ev);
  pp = objprop_get_property_page(op);
  property_page_show_extviewer(pp, ev);
}

/****************************************************************************
  Handle toggling of a boolean cell value in the extviewer's tree view.
****************************************************************************/
static void extviewer_view_cell_toggled(GtkCellRendererToggle *cell,
                                        gchar *path,
                                        gpointer userdata)
{
  struct extviewer *ev;
  struct objprop *op;
  struct property_page *pp;
  int propid;
  GtkTreeModel *model;
  GtkTreeIter iter;
  int id, old_id, turn_built;
  struct propval *pv;
  bool active, present;
  char buf[64];
  GdkPixbuf *pixbuf = NULL;

  ev = userdata;
  if (!ev) {
    return;
  }

  pv = ev->pv_cached;
  if (!pv) {
    return;
  }

  op = extviewer_get_objprop(ev);
  propid = objprop_get_id(op);
  active = gtk_cell_renderer_toggle_get_active(cell);
  pp = objprop_get_property_page(op);

  model = GTK_TREE_MODEL(ev->store);
  if (!gtk_tree_model_get_iter_from_string(model, &iter, path)) {
    return;
  }
  present = !active;


  switch (propid) {

  case OPID_TILE_SPECIALS:
    gtk_tree_model_get(model, &iter, 1, &id, -1);
    if (!(0 <= id && id < S_LAST)) {
      return;
    }
    if (present) {
      BV_SET(pv->data.v_bv_special, id);
    } else {
      BV_CLR(pv->data.v_bv_special, id);
    }
    gtk_list_store_set(ev->store, &iter, 0, present, -1);
    propval_as_string(pv, buf, sizeof(buf));
    gtk_label_set_text(GTK_LABEL(ev->panel_label), buf);
    break;

  case OPID_CITY_BUILDINGS:
    gtk_tree_model_get(model, &iter, 1, &id, -1);
    if (!(0 <= id && id < B_LAST)) {
      return;
    }
    turn_built = present ? game.info.turn : I_NEVER;
    pv->data.v_built[id].turn = turn_built;
    built_status_to_string(buf, sizeof(buf), &pv->data.v_built[id]);
    gtk_list_store_set(ev->store, &iter, 0, present, 3, buf, -1);
    propval_as_string(pv, buf, sizeof(buf));
    gtk_label_set_text(GTK_LABEL(ev->panel_label), buf);
    break;

  case OPID_PLAYER_NATION:
    gtk_tree_model_get(model, &iter, 1, &id, -1);
    if (!(0 <= id && id < nation_count()) || !present) {
      return;
    }
    old_id = nation_index(pv->data.v_nation);
    pv->data.v_nation = nation_by_number(id);
    gtk_list_store_set(ev->store, &iter, 0, TRUE, -1);
    gtk_tree_model_iter_nth_child(model, &iter, NULL, old_id);
    gtk_list_store_set(ev->store, &iter, 0, FALSE, -1);
    gtk_label_set_text(GTK_LABEL(ev->panel_label),
                       nation_adjective_translation(pv->data.v_nation));
    pixbuf = get_flag(pv->data.v_nation);
    gtk_image_set_from_pixbuf(GTK_IMAGE(ev->panel_image), pixbuf);
    if (pixbuf) {
      g_object_unref(pixbuf);
    }
    break;

  case OPID_PLAYER_INVENTIONS:
    gtk_tree_model_get(model, &iter, 1, &id, -1);
    if (!(A_FIRST <= id && id < advance_count())) {
      return;
    }
    pv->data.v_inventions[id] = present;
    gtk_list_store_set(ev->store, &iter, 0, present, -1);
    propval_as_string(pv, buf, sizeof(buf));
    gtk_label_set_text(GTK_LABEL(ev->panel_label), buf);
    break;

  default:
    return;
    break;
  }

  property_page_change_value(pp, op, pv);
}

/****************************************************************************
  Install all object properties that this page type can view/edit.
****************************************************************************/
static void property_page_setup_objprops(struct property_page *pp)
{

#define ADDPROP(ARG_id, ARG_name, ARG_flags, ARG_valtype) do {\
  struct objprop *MY_op = objprop_new(ARG_id, ARG_name,\
                                      ARG_flags, ARG_valtype, pp);\
  hash_insert(pp->objprop_table, &MY_op->id, MY_op);\
} while (0)

  switch (pp->objtype) {

  case OBJTYPE_TILE:
    ADDPROP(OPID_TILE_IMAGE, _("Image"),
            OPF_IN_LISTVIEW | OPF_HAS_WIDGET, VALTYPE_PIXBUF);
    ADDPROP(OPID_TILE_ADDRESS, _("Address"),
            OPF_HAS_WIDGET, VALTYPE_STRING);
    ADDPROP(OPID_TILE_TERRAIN, _("Terrain"),
            OPF_IN_LISTVIEW | OPF_HAS_WIDGET, VALTYPE_STRING);
    ADDPROP(OPID_TILE_RESOURCE, _("Resource"),
            OPF_IN_LISTVIEW | OPF_HAS_WIDGET, VALTYPE_STRING);
    ADDPROP(OPID_TILE_INDEX, _("Index"),
            OPF_IN_LISTVIEW | OPF_HAS_WIDGET, VALTYPE_INT);
    ADDPROP(OPID_TILE_X, Q_("?coordinate:X"),
            OPF_IN_LISTVIEW | OPF_HAS_WIDGET, VALTYPE_INT);
    ADDPROP(OPID_TILE_Y, Q_("?coordinate:Y"),
            OPF_IN_LISTVIEW | OPF_HAS_WIDGET, VALTYPE_INT);
    /* TRANS: The coordinate X in native coordinates.
     * The freeciv coordinate system is described in doc/HACKING. */
    ADDPROP(OPID_TILE_NAT_X, _("NAT_X"),
            OPF_IN_LISTVIEW | OPF_HAS_WIDGET, VALTYPE_INT);
    /* TRANS: The coordinate Y in native coordinates.
     * The freeciv coordinate system is described in doc/HACKING. */
    ADDPROP(OPID_TILE_NAT_Y, _("NAT_Y"),
            OPF_IN_LISTVIEW | OPF_HAS_WIDGET, VALTYPE_INT);
    ADDPROP(OPID_TILE_CONTINENT, _("Continent"),
            OPF_IN_LISTVIEW | OPF_HAS_WIDGET, VALTYPE_INT);
    ADDPROP(OPID_TILE_XY, Q_("?coordinates:X,Y"),
            OPF_IN_LISTVIEW | OPF_HAS_WIDGET, VALTYPE_STRING);
    ADDPROP(OPID_TILE_SPECIALS, _("Specials"), OPF_IN_LISTVIEW
            | OPF_HAS_WIDGET | OPF_EDITABLE, VALTYPE_BV_SPECIAL);
    break;

  case OBJTYPE_UNIT:
    ADDPROP(OPID_UNIT_IMAGE, _("Image"),
            OPF_IN_LISTVIEW | OPF_HAS_WIDGET, VALTYPE_PIXBUF);
    ADDPROP(OPID_UNIT_ADDRESS, _("Address"),
            OPF_HAS_WIDGET, VALTYPE_STRING);
    ADDPROP(OPID_UNIT_TYPE, _("Type"),
            OPF_IN_LISTVIEW | OPF_HAS_WIDGET, VALTYPE_STRING);
    ADDPROP(OPID_UNIT_ID, _("ID"),
            OPF_IN_LISTVIEW | OPF_HAS_WIDGET, VALTYPE_INT);
    ADDPROP(OPID_UNIT_XY, Q_("?coordinates:X,Y"),
            OPF_IN_LISTVIEW | OPF_HAS_WIDGET, VALTYPE_STRING);
    ADDPROP(OPID_UNIT_MOVES_LEFT, _("Moves Left"),
            OPF_IN_LISTVIEW | OPF_HAS_WIDGET | OPF_EDITABLE, VALTYPE_INT);
    break;

  case OBJTYPE_CITY:
    ADDPROP(OPID_CITY_IMAGE, _("Image"),
            OPF_IN_LISTVIEW | OPF_HAS_WIDGET, VALTYPE_PIXBUF);
    ADDPROP(OPID_CITY_NAME, _("Name"),
            OPF_IN_LISTVIEW | OPF_HAS_WIDGET | OPF_EDITABLE, VALTYPE_STRING);
    ADDPROP(OPID_CITY_ADDRESS, _("Address"),
            OPF_HAS_WIDGET, VALTYPE_STRING);
    ADDPROP(OPID_CITY_ID, _("ID"),
            OPF_IN_LISTVIEW | OPF_HAS_WIDGET, VALTYPE_INT);
    ADDPROP(OPID_CITY_XY, Q_("?coordinates:X,Y"),
            OPF_IN_LISTVIEW | OPF_HAS_WIDGET, VALTYPE_STRING);
    ADDPROP(OPID_CITY_SIZE, _("Size"),
            OPF_IN_LISTVIEW | OPF_HAS_WIDGET | OPF_EDITABLE, VALTYPE_INT);
    ADDPROP(OPID_CITY_BUILDINGS, _("Buildings"), OPF_IN_LISTVIEW
            | OPF_HAS_WIDGET | OPF_EDITABLE, VALTYPE_BUILT_ARRAY);
    break;

  case OBJTYPE_PLAYER:
    ADDPROP(OPID_PLAYER_NAME, _("Name"), OPF_IN_LISTVIEW
            | OPF_HAS_WIDGET | OPF_EDITABLE, VALTYPE_STRING);
    ADDPROP(OPID_PLAYER_ADDRESS, _("Address"),
            OPF_HAS_WIDGET, VALTYPE_STRING);
    ADDPROP(OPID_PLAYER_NATION, _("Nation"), OPF_IN_LISTVIEW
            | OPF_HAS_WIDGET | OPF_EDITABLE, VALTYPE_NATION);
    ADDPROP(OPID_PLAYER_INVENTIONS, _("Inventions"), OPF_IN_LISTVIEW
            | OPF_HAS_WIDGET | OPF_EDITABLE, VALTYPE_INVENTIONS_ARRAY);
    break;

  default:
    break;
  }

#undef ADDPROP

}

/****************************************************************************
  Callback for when a property page's listview's selection changes.
****************************************************************************/
static void property_page_selection_changed(GtkTreeSelection *sel,
                                            gpointer userdata)
{
  struct property_page *pp;
  struct objbind *ob = NULL;

  pp = userdata;
  if (!pp) {
    return;
  }

  if (gtk_tree_selection_count_selected_rows(sel) < 1) {
    property_page_set_focused_objbind(pp, NULL);
  }

  ob = property_page_get_focused_objbind(pp);
  property_page_objprop_iterate(pp, op) {
    objprop_refresh_widget(op, ob);
  } property_page_objprop_iterate_end;
}

/****************************************************************************
  Monitor which rows are to be selected, so we know which objbind to display
  in the properties panel.
****************************************************************************/
static gboolean property_page_selection_func(GtkTreeSelection *sel,
                                             GtkTreeModel *model,
                                             GtkTreePath *sel_path,
                                             gboolean currently_selected,
                                             gpointer data)
{
  struct property_page *pp;
  struct objbind *ob = NULL, *old_ob;
  GtkTreeIter iter;

  pp = data;
  if (!pp || !sel_path) {
    return TRUE;
  }

  if (!gtk_tree_model_get_iter(model, &iter, sel_path)) {
    return TRUE;
  }

  old_ob = property_page_get_focused_objbind(pp);
  gtk_tree_model_get(model, &iter, 0, &ob, -1);
  if (currently_selected) {
    if (ob == old_ob) {
      GList *rows, *p;
      GtkTreePath *path;
      struct objbind *new_ob = NULL;

      rows = gtk_tree_selection_get_selected_rows(sel, NULL);
      for (p = rows; p != NULL; p = p->next) {
        path = p->data;
        if (gtk_tree_model_get_iter(model, &iter, path)) {
          struct objbind *test_ob = NULL;
          gtk_tree_model_get(model, &iter, 0, &test_ob, -1);
          if (test_ob == ob) {
            continue;
          }
          new_ob = test_ob;
          break;
        }
      }
      g_list_foreach(rows, (GFunc) gtk_tree_path_free, NULL);
      g_list_free(rows);

      property_page_set_focused_objbind(pp, new_ob);
    }
  } else {
    property_page_set_focused_objbind(pp, ob);
  }

  return TRUE;
}

/****************************************************************************
  Callback to handle text changing in the quick find entry widget.
****************************************************************************/
static void property_page_quick_find_entry_changed(GtkWidget *entry,
                                                   gpointer userdata)
{
  struct property_page *pp;
  const gchar *text, *name;
  GtkWidget *w;

  pp = userdata;
  text = gtk_entry_get_text(GTK_ENTRY(entry));

  property_page_objprop_iterate(pp, op) {
    if (!objprop_has_widget(op)) {
      continue;
    }
    w = objprop_get_widget(op);
    if (!w) {
      continue;
    }
    name = objprop_get_name(op);
    if (text[0] == '\0' || mystrcasestr(name, text)) {
      gtk_widget_show(w);
    } else {
      gtk_widget_hide(w);
    }
  } property_page_objprop_iterate_end;
}

/****************************************************************************
  Create and return a property page of the given object type.
  Return NULL if the page could not be created.
****************************************************************************/
static struct property_page *property_page_new(int objtype)
{
  struct property_page *pp;
  GtkWidget *vbox, *vbox2, *hbox, *hbox2, *paned, *frame, *w;
  GtkWidget *scrollwin, *view, *label, *entry, *viewport, *notebook;
  GtkAdjustment *hadj, *vadj;
  GtkTreeSelection *sel;
  GtkCellRenderer *cell;
  GtkTreeViewColumn *col;
  int num_columns = 0;
  GType *gtype_array;
  int col_id = 1;
  const char *attr_type_str, *name;

  if (!(0 <= objtype && objtype < NUM_OBJTYPES)) {
    return NULL;
  }

  pp = fc_calloc(1, sizeof(struct property_page));
  pp->objtype = objtype;

  pp->objprop_table = hash_new(hash_fval_int,
                               hash_fcmp_int);
  property_page_setup_objprops(pp);

  pp->objbind_table = hash_new_full(hash_fval_keyval,
                                    hash_fcmp_keyval, NULL,
                                    (hash_free_fn_t) objbind_free);

  property_page_objprop_iterate(pp, op) {
    if (objprop_show_in_listview(op)) {
      num_columns++;
    }
  } property_page_objprop_iterate_end;

  /* Column zero in the store holds an objbind
   * pointer and is never displayed. */
  num_columns++;
  gtype_array = fc_malloc(num_columns * sizeof(GType));
  gtype_array[0] = G_TYPE_POINTER;
    
  property_page_objprop_iterate(pp, op) {
    if (objprop_show_in_listview(op)) {
      gtype_array[col_id] = objprop_get_gtype(op);
      objprop_set_column_id(op, col_id);
      col_id++;
    }
  } property_page_objprop_iterate_end;

  pp->object_store = gtk_list_store_newv(num_columns, gtype_array);
  free(gtype_array);


  paned = gtk_hpaned_new();
  pp->widget = paned;

  /* Left side object list view. */

  scrollwin = gtk_scrolled_window_new(NULL, NULL);
  gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(scrollwin),
                                      GTK_SHADOW_ETCHED_IN);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrollwin),
                                 GTK_POLICY_AUTOMATIC,
                                 GTK_POLICY_AUTOMATIC);
  gtk_paned_pack1(GTK_PANED(paned), scrollwin, TRUE, TRUE);

  view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(pp->object_store));
  gtk_tree_view_set_rules_hint(GTK_TREE_VIEW(view), TRUE);

  property_page_objprop_iterate(pp, op) {
    if (!objprop_show_in_listview(op)) {
      continue;
    }

    attr_type_str = objprop_get_attribute_type_string(op);
    if (!attr_type_str) {
      continue;
    }
    col_id = objprop_get_column_id(op);
    if (col_id < 0) {
      continue;
    }
    name = objprop_get_name(op);
    if (!name) {
      continue;
    }
    cell = objprop_create_cell_renderer(op);
    if (!cell) {
      continue;
    }

    col = gtk_tree_view_column_new_with_attributes(name, cell,
                                                   attr_type_str, col_id,
                                                   NULL);

    gtk_tree_view_column_set_sizing(col, GTK_TREE_VIEW_COLUMN_GROW_ONLY);
    gtk_tree_view_column_set_resizable(col, TRUE);
    gtk_tree_view_column_set_reorderable(col, TRUE);
    if (objprop_is_sortable(op)) {
      gtk_tree_view_column_set_clickable(col, TRUE);
      gtk_tree_view_column_set_sort_column_id(col, col_id);
    } else {
      gtk_tree_view_column_set_clickable(col, FALSE);
    }
    gtk_tree_view_append_column(GTK_TREE_VIEW(view), col);

  } property_page_objprop_iterate_end;

  sel = gtk_tree_view_get_selection(GTK_TREE_VIEW(view));
  gtk_tree_selection_set_mode(sel, GTK_SELECTION_MULTIPLE);
  g_signal_connect(sel, "changed",
                   G_CALLBACK(property_page_selection_changed), pp);
  gtk_tree_selection_set_select_function(sel,
      property_page_selection_func, pp, NULL);

  gtk_container_add(GTK_CONTAINER(scrollwin), view);
  pp->object_view = view;


  /* Right side properties panel. */

  hbox = gtk_hbox_new(FALSE, 0);
  gtk_container_set_border_width(GTK_CONTAINER(hbox), 4);
  gtk_paned_pack2(GTK_PANED(paned), hbox, TRUE, TRUE);

  vbox = gtk_vbox_new(FALSE, 0);
  gtk_box_pack_start(GTK_BOX(hbox), vbox, FALSE, FALSE, 0);

  /* Extended property viewer to the right of the properties panel.
   * This needs to be created before property widgets, since some
   * might try to append themselves to this notebook. */
  notebook = gtk_notebook_new();
  gtk_notebook_set_show_tabs(GTK_NOTEBOOK(notebook), FALSE);
  gtk_notebook_set_show_border(GTK_NOTEBOOK(notebook), FALSE);
  gtk_box_pack_start(GTK_BOX(hbox), notebook, TRUE, TRUE, 0);
  pp->extviewer_notebook = notebook;

  frame = gtk_frame_new(_("Properties"));
  gtk_container_set_border_width(GTK_CONTAINER(frame), 4);
  gtk_box_pack_start(GTK_BOX(vbox), frame, TRUE, TRUE, 0);

  scrollwin = gtk_scrolled_window_new(NULL, NULL);
  gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(scrollwin),
                                      GTK_SHADOW_NONE);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrollwin),
                                 GTK_POLICY_AUTOMATIC,
                                 GTK_POLICY_AUTOMATIC);
  gtk_container_add(GTK_CONTAINER(frame), scrollwin);

  vbox2 = gtk_vbox_new(FALSE, 4);
  gtk_container_set_border_width(GTK_CONTAINER(vbox2), 4);

  hadj = gtk_scrolled_window_get_hadjustment(GTK_SCROLLED_WINDOW(scrollwin));
  vadj = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(scrollwin));
  viewport = gtk_viewport_new(hadj, vadj);
  gtk_viewport_set_shadow_type(GTK_VIEWPORT(viewport), GTK_SHADOW_NONE);
  gtk_container_add(GTK_CONTAINER(viewport), vbox2);
  gtk_container_add(GTK_CONTAINER(scrollwin), viewport);
  
  property_page_objprop_iterate(pp, op) {
    if (!objprop_has_widget(op)) {
      continue;
    }
    w = objprop_get_widget(op);
    if (!w) {
      continue;
    }
    gtk_box_pack_start(GTK_BOX(vbox2), w, FALSE, FALSE, 0);
  } property_page_objprop_iterate_end;

  hbox2 = gtk_hbox_new(FALSE, 4);
  gtk_container_set_border_width(GTK_CONTAINER(hbox2), 4);
  gtk_box_pack_start(GTK_BOX(vbox), hbox2, FALSE, FALSE, 4);

  label = gtk_label_new(_("Quick Find:"));
  gtk_box_pack_start(GTK_BOX(hbox2), label, FALSE, FALSE, 0);

  entry = gtk_entry_new();
  g_signal_connect(entry, "changed",
      G_CALLBACK(property_page_quick_find_entry_changed), pp);
  gtk_box_pack_start(GTK_BOX(hbox2), entry, TRUE, TRUE, 0);


  gtk_paned_set_position(GTK_PANED(paned), 300);

  return pp;
}

/****************************************************************************
  Returns the translated name of the property page's object type.
****************************************************************************/
static const char *property_page_get_name(const struct property_page *pp)
{
  if (!pp) {
    return "";
  }
  return objtype_get_name(pp->objtype);
}

/****************************************************************************
  Create a pixbuf containing an image of the given tile. The image will
  only be of the layers containing terrains, resources and specials.

  May return NULL on error or bad input.
  NB: You must call g_object_unref on the non-NULL return value when you
  no longer need it.
****************************************************************************/
static GdkPixbuf *create_tile_pixbuf(struct tile *ptile)
{
  int layers[] = {
    LAYER_BACKGROUND,
    LAYER_TERRAIN1,
    LAYER_TERRAIN2,
    LAYER_TERRAIN3,
    LAYER_WATER,
    LAYER_ROADS,
    LAYER_SPECIAL1,
    LAYER_SPECIAL2,
    LAYER_SPECIAL3
  };
  int num_layers = ARRAY_SIZE(layers);

  return create_pixbuf_from_layers(ptile, NULL, NULL, layers, num_layers);
}

/****************************************************************************
  Create a pixbuf containing an image of the given unit.

  May return NULL on error or bad input.
  NB: You must call g_object_unref on the non-NULL return value when you
  no longer need it.
****************************************************************************/
static GdkPixbuf *create_unit_pixbuf(struct unit *punit)
{
  int layers[] = {
    LAYER_UNIT,
    LAYER_FOCUS_UNIT,
  };
  int num_layers = ARRAY_SIZE(layers);

  return create_pixbuf_from_layers(NULL, punit, NULL,
                                   layers, num_layers);
}

/****************************************************************************
  Create a pixbuf containing an image of the given city.

  May return NULL on error or bad input.
  NB: You must call g_object_unref on the non-NULL return value when you
  no longer need it.
****************************************************************************/
static GdkPixbuf *create_city_pixbuf(struct city *pcity)
{
  int layers[] = {
    LAYER_BACKGROUND,
    LAYER_TERRAIN1,
    LAYER_TERRAIN2,
    LAYER_TERRAIN3,
    LAYER_WATER,
    LAYER_ROADS,
    LAYER_SPECIAL1,
    LAYER_CITY1,
    LAYER_SPECIAL2,
    LAYER_CITY2,
    LAYER_SPECIAL3
  };
  int num_layers = ARRAY_SIZE(layers);

  return create_pixbuf_from_layers(city_tile(pcity), NULL, pcity,
                                   layers, num_layers);
}

/****************************************************************************
  Create a pixbuf containing an image of the given tile, unit or city
  restricted to the layers listed in 'layers'.

  May return NULL on error or bad input.
  NB: You must call g_object_unref on the non-NULL return value when you
  no longer need it.
****************************************************************************/
static GdkPixbuf *create_pixbuf_from_layers(struct tile *ptile,
                                            struct unit *punit,
                                            struct city *pcity,
                                            int *layers,
                                            int num_layers)
{
  struct canvas canvas;
  int w, h, i, fh, fw, canvas_x, canvas_y;
  GdkPixbuf *pixbuf;

  fw = tileset_full_tile_width(tileset);
  fh = tileset_full_tile_height(tileset);
  w = tileset_tile_width(tileset);
  h = tileset_tile_height(tileset);

  pixbuf = gdk_pixbuf_new(GDK_COLORSPACE_RGB, TRUE, 8, fw, fh);
  gdk_pixbuf_fill(pixbuf, 0x00000000);

  canvas.type = CANVAS_PIXBUF;
  canvas.v.pixbuf = pixbuf;

  canvas_x = 0;
  canvas_y = 0;

  canvas_y += (fh - h);

  for (i = 0; i < num_layers; i++) {
    put_one_element(&canvas, layers[i],
                    ptile, NULL, NULL, punit, pcity,
                    canvas_x, canvas_y, NULL);
  }
  return pixbuf;
}

/****************************************************************************
  Remove all object binds (i.e. objects listed) in the property page.
****************************************************************************/
static void property_page_clear_objbinds(struct property_page *pp)
{
  if (!pp) {
    return;
  }

  gtk_list_store_clear(pp->object_store);
  hash_delete_all_entries(pp->objbind_table);
  property_page_set_focused_objbind(pp, NULL);
}

/****************************************************************************
  Create a new object bind to the given object and register it with the
  given property page.
****************************************************************************/
static void property_page_add_objbind(struct property_page *pp,
                                      gpointer object_data)
{
  struct objbind *ob;
  int id;
  gpointer key;

  if (!pp) {
    return;
  }

  id = objtype_get_id_from_object(pp->objtype, object_data);
  key = GINT_TO_POINTER(id);
  if (hash_key_exists(pp->objbind_table, key)) {
    return;
  }

  ob = objbind_new(pp->objtype, object_data);
  objbind_bind_properties(ob, pp);

  hash_insert(pp->objbind_table, key, ob);
}

/****************************************************************************
  Create zero or more object binds from the objects on the given tile to
  the properties contained in the given property page.
****************************************************************************/
static void property_page_add_objbinds_from_tile(struct property_page *pp,
                                                 const struct tile *ptile)
{

  if (!pp || !ptile) {
    return;
  }

  switch (pp->objtype) {

  case OBJTYPE_TILE:
    property_page_add_objbind(pp, (gpointer) ptile);
    break;

  case OBJTYPE_UNIT:
    unit_list_iterate(ptile->units, punit) {
      property_page_add_objbind(pp, punit);
    } unit_list_iterate_end;
    break;

  case OBJTYPE_CITY:
    if (tile_city(ptile)) {
      property_page_add_objbind(pp, tile_city(ptile));
    }
    break;

  default:
    break;
  }
}

/****************************************************************************
  Set the column value in the list store of the property page.
  Returns TRUE if data was enetered into the store.

  NB: This must match the conversion in objprop_get_gtype.
****************************************************************************/
static bool property_page_set_store_value(struct property_page *pp,
                                          struct objprop *op,
                                          struct objbind *ob,
                                          GtkTreeIter *iter)
{
  int col_id;
  struct propval *pv;
  int valtype;
  char buf[128];
  GdkPixbuf *pixbuf = NULL;

  if (!pp || !pp->object_store || !op || !ob) {
    return FALSE;
  }

  if (!objprop_show_in_listview(op)) {
    return FALSE;
  }

  col_id = objprop_get_column_id(op);
  if (col_id < 0) {
    return FALSE;
  }

  pv = objbind_get_value_from_object(ob, op);
  if (!pv) {
    return FALSE;
  }

  valtype = objprop_get_valtype(op);

  switch (valtype) {
  case VALTYPE_INT:
    gtk_list_store_set(pp->object_store, iter, col_id, pv->data.v_int, -1);
    break;
  case VALTYPE_STRING:
    gtk_list_store_set(pp->object_store, iter, col_id, pv->data.v_string, -1);
    break;
  case VALTYPE_PIXBUF:
    gtk_list_store_set(pp->object_store, iter, col_id, pv->data.v_pixbuf, -1);
    break;
  case VALTYPE_BUILT_ARRAY:
  case VALTYPE_INVENTIONS_ARRAY:
  case VALTYPE_BV_SPECIAL:
    propval_as_string(pv, buf, sizeof(buf));
    gtk_list_store_set(pp->object_store, iter, col_id, buf, -1);
    break;
  case VALTYPE_NATION:
    pixbuf = get_flag(pv->data.v_nation);
    gtk_list_store_set(pp->object_store, iter, col_id, pixbuf, -1);
    if (pixbuf) {
      g_object_unref(pixbuf);
    }
    break;
  default:
    break;
  }

  propval_free(pv);

  return TRUE;
}

/****************************************************************************
  Inserts any objbinds owned by this proprety page into the page's list
  store if they are not there already and refreshes all property widgets.
****************************************************************************/
static void property_page_fill_widgets(struct property_page *pp)
{
  struct objbind *ob;

  if (!pp || !pp->objbind_table) {
    return;
  }

  if (pp->object_store) {
    GtkTreeIter iter;
    GtkTreeRowReference *rr;
    GtkTreeModel *model;
    GtkTreePath *path;
    
    model = GTK_TREE_MODEL(pp->object_store);

    property_page_objbind_iterate(pp, ob) {
      if (objbind_get_rowref(ob)) {
        continue;
      }
      gtk_list_store_append(pp->object_store, &iter);
      gtk_list_store_set(pp->object_store, &iter, 0, ob, -1);
      path = gtk_tree_model_get_path(model, &iter);
      rr = gtk_tree_row_reference_new(model, path);
      gtk_tree_path_free(path);
      objbind_set_rowref(ob, rr);

      property_page_objprop_iterate(pp, op) {
        property_page_set_store_value(pp, op, ob, &iter);
      } property_page_objprop_iterate_end;
    } property_page_objbind_iterate_end;
  }

  ob = property_page_get_focused_objbind(pp);
  property_page_objprop_iterate(pp, op) {
    objprop_refresh_widget(op, ob);
  } property_page_objprop_iterate_end;
}

/****************************************************************************
  Get the objbind corresponding to the object that is currently in view
  (i.e. in the information/properties panels) or NULL if none.
****************************************************************************/
static struct objbind *property_page_get_focused_objbind(struct property_page *pp)
{
  if (!pp) {
    return NULL;
  }
  return pp->focused_objbind;
}

/****************************************************************************
  Set the objbind that should be shown in the properties panel. Does not
  refresh property widgets.
****************************************************************************/
static void property_page_set_focused_objbind(struct property_page *pp,
                                              struct objbind *ob)
{
  if (!pp) {
    return;
  }
  pp->focused_objbind = ob;
}

/****************************************************************************
  Returns the objbind whose object corresponds to the given id, or NULL
  if no such objbind exists.
****************************************************************************/
static struct objbind *property_page_get_objbind(struct property_page *pp,
                                                 int object_id)
{
  struct objbind *ob;

  if (!pp || !pp->objbind_table) {
    return NULL;
  }

  ob = hash_lookup_data(pp->objbind_table, GINT_TO_POINTER(object_id));
  return ob;
}

/****************************************************************************
  Removes all of the current objbinds and extracts new ones from the
  supplied list of tiles.
****************************************************************************/
static void property_page_load_tiles(struct property_page *pp,
                                     const struct tile_list *tiles)
{
  if (!pp || !tiles) {
    return;
  }

  tile_list_iterate(tiles, ptile) {
    property_page_add_objbinds_from_tile(pp, ptile);
  } tile_list_iterate_end;

  property_page_fill_widgets(pp);
}

/****************************************************************************
  Return the number of current bound objects to this property page.
****************************************************************************/
static int property_page_get_num_objbinds(const struct property_page *pp)
{
  if (!pp || !pp->objbind_table) {
    return 0;
  }
  return hash_num_entries(pp->objbind_table);
}

/****************************************************************************
  Called when a user sets a new value for the given property via the GUI.
  Refreshes the properties widget if anything changes.
****************************************************************************/
static void property_page_change_value(struct property_page *pp,
                                       struct objprop *op,
                                       struct propval *pv)
{
  GtkTreeSelection *sel;
  GtkTreeModel *model;
  GList *rows, *p;
  GtkTreePath *path;
  GtkTreeIter iter;
  struct objbind *ob;

  if (!pp || !op || !pp->object_view) {
    return;
  }

  if (objprop_is_readonly(op)) {
    return;
  }

  sel = gtk_tree_view_get_selection(GTK_TREE_VIEW(pp->object_view));
  rows = gtk_tree_selection_get_selected_rows(sel, &model);

  for (p = rows; p != NULL; p = p->next) {
    path = p->data;
    if (gtk_tree_model_get_iter(model, &iter, path)) {
      gtk_tree_model_get(model, &iter, 0, &ob, -1);
      objbind_set_modified_value(ob, op, pv);
    }
    gtk_tree_path_free(path);
  }
  g_list_free(rows);

  ob = property_page_get_focused_objbind(pp);
  objprop_refresh_widget(op, ob);
}

/****************************************************************************
  Send all modified values of all selected properties.
****************************************************************************/
static void property_page_send_values(struct property_page *pp)
{
  GtkTreeSelection *sel;
  GtkTreeModel *model;
  GList *rows, *p;
  GtkTreePath *path;
  GtkTreeIter iter;
  struct objbind *ob;
  union packetdata packet;

  if (!pp || !pp->object_view) {
    return;
  }

  sel = gtk_tree_view_get_selection(GTK_TREE_VIEW(pp->object_view));
  if (gtk_tree_selection_count_selected_rows(sel) < 1) {
    return;
  }

  packet = property_page_new_packet(pp);
  if (!packet.v_pointer) {
    return;
  }

  rows = gtk_tree_selection_get_selected_rows(sel, &model);
  connection_do_buffer(&client.conn);
  for (p = rows; p != NULL; p = p->next) {
    path = p->data;
    if (gtk_tree_model_get_iter(model, &iter, path)) {
      gtk_tree_model_get(model, &iter, 0, &ob, -1);
      if (objbind_has_modified_properties(ob)) {
        objbind_pack_current_values(ob, packet);
        property_page_objprop_iterate(pp, op) {
          if (objprop_is_readonly(op)) {
            continue;
          }
          objbind_pack_modified_value(ob, op, packet);
        } property_page_objprop_iterate_end;
        property_page_send_packet(pp, packet);
      }
    }
    gtk_tree_path_free(path);
  }
  connection_do_unbuffer(&client.conn);
  g_list_free(rows);

  property_page_free_packet(pp, packet);
}

/****************************************************************************
  Returns pointer to a packet suitable for this page's object type. Result
  should be freed using property_page_free_packet when no longer needed.
****************************************************************************/
static union packetdata property_page_new_packet(struct property_page *pp)
{
  union packetdata packet = {NULL,};

  if (!pp) {
    return packet;
  }

  switch (pp->objtype) {
  case OBJTYPE_TILE:
    packet.tile = fc_calloc(1, sizeof(*packet.tile));
    break;
  case OBJTYPE_UNIT:
    packet.unit = fc_calloc(1, sizeof(*packet.unit));
    break;
  case OBJTYPE_CITY:
    packet.city = fc_calloc(1, sizeof(*packet.city));
    break;
  case OBJTYPE_PLAYER:
    packet.player = fc_calloc(1, sizeof(*packet.player));
    break;
  default:
    break;
  }

  return packet;
}

/****************************************************************************
  Sends the given packet.
****************************************************************************/
static void property_page_send_packet(struct property_page *pp,
                                      union packetdata packet)
{
  if (!pp || !packet.v_pointer) {
    return;
  }

  switch (pp->objtype) {
  case OBJTYPE_TILE:
    send_packet_edit_tile(&client.conn, packet.tile);
    break;
  case OBJTYPE_UNIT:
    send_packet_edit_unit(&client.conn, packet.unit);
    break;
  case OBJTYPE_CITY:
    send_packet_edit_city(&client.conn, packet.city);
    break;
  case OBJTYPE_PLAYER:
    send_packet_edit_player(&client.conn, packet.player);
    break;
  default:
    break;
  }
}

/****************************************************************************
  Free any resources being used by the packet.
****************************************************************************/
static void property_page_free_packet(struct property_page *pp,
                                      union packetdata packet)
{
  if (!packet.v_pointer) {
    return;
  }

  free(packet.v_pointer);
  packet.v_pointer = NULL;
}

/****************************************************************************
  Reload the displayed values of all properties for the selected bound
  objects. Hence, deletes all their stored modified values.
****************************************************************************/
static void property_page_reset_objbinds(struct property_page *pp)
{
  GtkTreeSelection *sel;
  GtkTreeModel *model;
  GtkTreeIter iter;
  GtkTreePath *path;
  GList *rows, *p;
  struct objbind *ob;

  if (!pp || !pp->object_view) {
    return;
  }

  sel = gtk_tree_view_get_selection(GTK_TREE_VIEW(pp->object_view));
  if (gtk_tree_selection_count_selected_rows(sel) < 1) {
    return;
  }

  rows = gtk_tree_selection_get_selected_rows(sel, &model);
  for (p = rows; p != NULL; p = p->next) {
    path = p->data;
    if (gtk_tree_model_get_iter(model, &iter, path)) {
      gtk_tree_model_get(model, &iter, 0, &ob, -1);
      objbind_clear_all_modified_values(ob);
      property_page_objprop_iterate(pp, op) {
        property_page_set_store_value(pp, op, ob, &iter);
      } property_page_objprop_iterate_end;
    }
    gtk_tree_path_free(path);
  }
  g_list_free(rows);

  ob = property_page_get_focused_objbind(pp);
  property_page_objprop_iterate(pp, op) {
    objprop_refresh_widget(op, ob);
  } property_page_objprop_iterate_end;
}

/****************************************************************************
  Update objbinds and widgets according to how the object given by
  'object_id' has changed. If the object no longer exists then the
  objbind is removed from the property page.
****************************************************************************/
static void property_page_object_changed(struct property_page *pp,
                                         int object_id,
                                         bool removed)
{
  struct objbind *ob;
  GtkTreeRowReference *rr;

  ob = property_page_get_objbind(pp, object_id);
  if (!ob) {
    return;
  }

  rr = objbind_get_rowref(ob);
  if (rr && gtk_tree_row_reference_valid(rr)) {
    GtkTreePath *path;
    GtkTreeIter iter;
    GtkTreeModel *model;

    model = gtk_tree_row_reference_get_model(rr);
    path = gtk_tree_row_reference_get_path(rr);

    if (gtk_tree_model_get_iter(model, &iter, path)) {
      if (removed) {
        gtk_list_store_remove(pp->object_store, &iter);
      } else {
        property_page_objprop_iterate(pp, op) {
          property_page_set_store_value(pp, op, ob, &iter);
        } property_page_objprop_iterate_end;
      }
    }

    gtk_tree_path_free(path);
  }

  if (removed) {
    hash_delete_entry(pp->objbind_table, GINT_TO_POINTER(object_id));
    return;
  }

  if (ob == property_page_get_focused_objbind(pp)) {
    property_page_objprop_iterate(pp, op) {
      objprop_refresh_widget(op, ob);
    } property_page_objprop_iterate_end;
  }
}

/****************************************************************************
  Add the extviewer's view widget to the property page so that it can
  be shown in the extended property view panel.
****************************************************************************/
static void property_page_add_extviewer(struct property_page *pp,
                                        struct extviewer *ev)
{
  GtkWidget *w;

  if (!pp || !ev) {
    return;
  }

  w = extviewer_get_view_widget(ev);
  if (!w) {
    return;
  }
  gtk_notebook_append_page(GTK_NOTEBOOK(pp->extviewer_notebook), w, NULL);
}

/****************************************************************************
  Make the given extended property viewer's view widget visible in the
  property page.
****************************************************************************/
static void property_page_show_extviewer(struct property_page *pp,
                                         struct extviewer *ev)
{
  GtkWidget *w;
  GtkNotebook *notebook;
  int page;

  if (!pp || !ev) {
    return;
  }

  w = extviewer_get_view_widget(ev);
  if (!w) {
    return;
  }

  notebook = GTK_NOTEBOOK(pp->extviewer_notebook);
  page = gtk_notebook_page_num(notebook, w);
  gtk_notebook_set_current_page(notebook, page);
}

/****************************************************************************
  Create and add a property page for the given object type
  to the property editor. Returns TRUE if successful.
****************************************************************************/
static bool property_editor_add_page(struct property_editor *pe,
                                     int objtype)
{
  struct property_page *pp;
  GtkWidget *label;
  const char *name;

  if (!pe || !pe->notebook) {
    return FALSE;
  }
  
  if (!(0 <= objtype && objtype < NUM_OBJTYPES)) {
    return FALSE;
  }

  pp = property_page_new(objtype);
  if (!pp) {
    return FALSE;
  }

  name = property_page_get_name(pp);
  label = gtk_label_new(name);
  gtk_notebook_append_page(GTK_NOTEBOOK(pe->notebook),
                           pp->widget, label);

  pe->property_pages[objtype] = pp;

  return TRUE;
}

/****************************************************************************
  Handles the 'clicked' signal for the "Apply" button in the property editor.
****************************************************************************/
static void property_editor_apply_button_clicked(GtkButton *button,
                                                 gpointer userdata)
{
  struct property_editor *pe;
  struct property_page *pp;
  int objtype;

  pe = userdata;
  if (!pe) {
    return;
  }

  objtype = gtk_notebook_get_current_page(GTK_NOTEBOOK(pe->notebook));
  if (!(0 <= objtype && objtype < NUM_OBJTYPES)) {
    return;
  }

  pp = pe->property_pages[objtype];
  if (!pp) {
    return;
  }

  property_page_send_values(pp);
}

/****************************************************************************
  Handles the 'clicked' signal for the "Refresh" button in the
  property editor.
****************************************************************************/
static void property_editor_refresh_button_clicked(GtkButton *button,
                                                   gpointer userdata)
{
  struct property_editor *pe;
  struct property_page *pp;
  int objtype;

  pe = userdata;
  if (!pe) {
    return;
  }

  objtype = gtk_notebook_get_current_page(GTK_NOTEBOOK(pe->notebook));
  if (!(0 <= objtype && objtype < NUM_OBJTYPES)) {
    return;
  }

  pp = pe->property_pages[objtype];
  if (!pp) {
    return;
  }

  property_page_reset_objbinds(pp);
}

/****************************************************************************
  Handle a change in the active item of the combobox, set by the user.
  NB: This function will also be called if the active item is changed
  via the API, so use enable/disable_widget_callback if needed.
****************************************************************************/
static void property_editor_combo_changed(GtkComboBox *combo,
                                          gpointer userdata)
{
  struct property_editor *pe;
  int objtype;

  pe = userdata;
  if (!pe || !pe->notebook) {
    return;
  }

  objtype = gtk_combo_box_get_active(combo);
  if (!(0 <= objtype && objtype < NUM_OBJTYPES)) {
    return;
  }
  gtk_notebook_set_current_page(GTK_NOTEBOOK(pe->notebook), objtype);
}

/****************************************************************************
  Create and return the property editor widget bundle.
****************************************************************************/
static struct property_editor *property_editor_new(void)
{
  struct property_editor *pe;
  GtkWidget *notebook, *button, *label, *combo;
  GtkWidget *hbox, *vbox;
  GtkSizeGroup *sizegroup;
  int objtype;
  const char *name;

  pe = fc_calloc(1, sizeof(*pe));

  hbox = gtk_hbox_new(FALSE, 4);
  pe->widget = hbox;

  label = gtk_label_new(_("Property Editor"));
  gtk_notebook_append_page(GTK_NOTEBOOK(bottom_notebook),
                           hbox, label);

  notebook = gtk_notebook_new();
  gtk_notebook_set_show_tabs(GTK_NOTEBOOK(notebook), FALSE);
  gtk_box_pack_start(GTK_BOX(hbox), notebook, TRUE, TRUE, 0);
  pe->notebook = notebook;

  for (objtype = 0; objtype < NUM_OBJTYPES; objtype++) {
    property_editor_add_page(pe, objtype);
  }

  vbox = gtk_vbox_new(FALSE, 8);
  gtk_container_set_border_width(GTK_CONTAINER(vbox), 4);
  gtk_box_pack_start(GTK_BOX(hbox), vbox, FALSE, FALSE, 0);

  sizegroup = gtk_size_group_new(GTK_SIZE_GROUP_BOTH);

  button = gtk_button_new_from_stock(GTK_STOCK_APPLY);
  gtk_size_group_add_widget(sizegroup, button);
  g_signal_connect(button, "clicked",
                   G_CALLBACK(property_editor_apply_button_clicked), pe);
  gtk_box_pack_start(GTK_BOX(vbox), button, FALSE, FALSE, 0);

  button = gtk_button_new_from_stock(GTK_STOCK_REFRESH);
  gtk_size_group_add_widget(sizegroup, button);
  g_signal_connect(button, "clicked",
                   G_CALLBACK(property_editor_refresh_button_clicked), pe);
  gtk_box_pack_start(GTK_BOX(vbox), button, FALSE, FALSE, 0);

  combo = gtk_combo_box_new_text();
  for (objtype = 0; objtype < NUM_OBJTYPES; objtype++) {
    name = property_page_get_name(pe->property_pages[objtype]);
    if (!name) {
      continue;
    }
    gtk_combo_box_append_text(GTK_COMBO_BOX(combo), name);
  }

  g_signal_connect(combo, "changed",
                   G_CALLBACK(property_editor_combo_changed), pe);
  gtk_size_group_add_widget(sizegroup, combo);
  gtk_box_pack_start(GTK_BOX(vbox), combo, FALSE, FALSE, 24);
  pe->combo = combo;

  button = gtk_button_new_from_stock(GTK_STOCK_CLOSE);
  gtk_size_group_add_widget(sizegroup, button);
  g_signal_connect_swapped(button, "clicked",
                           G_CALLBACK(gtk_widget_hide_on_delete), pe->widget);
  gtk_box_pack_end(GTK_BOX(vbox), button, FALSE, FALSE, 0);

  gtk_widget_show_all(pe->widget);

  return pe;
}

/****************************************************************************
  Get the property editor for the client's GUI.
****************************************************************************/
struct property_editor *editprop_get_property_editor(void)
{
  if (!the_property_editor) {
    the_property_editor = property_editor_new();
  }
  return the_property_editor;
}

/****************************************************************************
  Refresh the given property editor according to the given list of tiles.
****************************************************************************/
void property_editor_load_tiles(struct property_editor *pe,
                                const struct tile_list *tiles)
{
  struct property_page *pp;
  int objtype;

  if (!pe || !tiles) {
    return;
  }

  disable_widget_callback(pe->combo,
                          G_CALLBACK(property_editor_combo_changed));

  for (objtype = 0; objtype < NUM_OBJTYPES; objtype++) {
    pp = pe->property_pages[objtype];
    property_page_load_tiles(pp, tiles);
    if (property_page_get_num_objbinds(pp) > 0) {
      gtk_notebook_set_current_page(GTK_NOTEBOOK(pe->notebook), objtype);
      gtk_combo_box_set_active(GTK_COMBO_BOX(pe->combo), objtype);
    }
  }
  
  enable_widget_callback(pe->combo,
                         G_CALLBACK(property_editor_combo_changed));
}

/****************************************************************************
  Show the property editor to the user.
****************************************************************************/
void property_editor_popup(struct property_editor *pe)
{
  int page;

  if (!pe || !pe->widget || !bottom_notebook) {
    return;
  }

  gtk_widget_show(pe->widget);
  page = gtk_notebook_page_num(GTK_NOTEBOOK(bottom_notebook),
                               pe->widget);
  gtk_notebook_set_current_page(GTK_NOTEBOOK(bottom_notebook), page);
}

/****************************************************************************
  Hide the property editor window.
****************************************************************************/
void property_editor_popdown(struct property_editor *pe)
{
  if (!pe || !pe->widget) {
    return;
  }
  gtk_widget_hide(pe->widget);
}

/****************************************************************************
  Handle a notification from the client core that some object has changed
  state at the server side (including being removed).
****************************************************************************/
void property_editor_handle_object_changed(struct property_editor *pe,
                                           int objtype,
                                           int object_id,
                                           bool remove)
{
  struct property_page *pp;

  if (!pe) {
    return;
  }

  if (!(0 <= objtype && objtype < NUM_OBJTYPES)) {
    return;
  }

  pp = pe->property_pages[objtype];
  property_page_object_changed(pp, object_id, remove);
}

/****************************************************************************
  Clear all property pages in the given property editor.
****************************************************************************/
void property_editor_clear(struct property_editor *pe)
{
  int objtype;
  struct property_page *pp;

  if (!pe) {
    return;
  }

  for (objtype = 0; objtype < NUM_OBJTYPES; objtype++) {
    pp = pe->property_pages[objtype];
    property_page_clear_objbinds(pp);
  }
}

/****************************************************************************
  Clear the player property page, load the current game players into it, and
  make it the current shown notebook page.
****************************************************************************/
void property_editor_reload_players(struct property_editor *pe)
{
  int objtype;
  struct property_page *pp;

  if (!pe) {
    return;
  }

  objtype = OBJTYPE_PLAYER;
  pp = pe->property_pages[objtype];
  if (!pp) {
    return;
  }

  property_page_clear_objbinds(pp);

  players_iterate(pplayer) {
    property_page_add_objbind(pp, pplayer);
  } players_iterate_end;

  property_page_fill_widgets(pp);
  gtk_notebook_set_current_page(GTK_NOTEBOOK(pe->notebook), objtype);

  disable_widget_callback(pe->combo,
                          G_CALLBACK(property_editor_combo_changed));
  gtk_combo_box_set_active(GTK_COMBO_BOX(pe->combo), objtype);
  enable_widget_callback(pe->combo,
                         G_CALLBACK(property_editor_combo_changed));
}
