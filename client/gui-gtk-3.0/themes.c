/********************************************************************** 
 Freeciv - Copyright (C) 2005 The Freeciv Team
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
#include <fc_config.h>
#endif

#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <gtk/gtk.h>

/* utility */
#include "mem.h"
#include "string_vector.h"
#include "support.h"

/* client */
#include "themes_common.h"

/* gui-gtk-3.0 */
#include "gui_main.h"

#include "themes_g.h"

/*****************************************************************************
  Loads a gtk theme directory/theme_name
*****************************************************************************/
void gui_load_theme(const char *directory, const char *theme_name)
{
  GtkCssProvider *css_provider;
  GError *error = NULL;
  char buf[strlen(directory) + strlen(theme_name) + 32];
  /* Gtk theme is a directory containing gtk-3.0/gtkrc file */
  fc_snprintf(buf, sizeof(buf), "%s/%s/gtk-3.0/gtk.css", directory,
              theme_name);
  css_provider = gtk_css_provider_new();
  gtk_css_provider_load_from_file(css_provider, g_file_new_for_path(buf), &error);
  if (error) {
    g_warning("%s\n", error->message);
    return;
  }
  gtk_style_context_add_provider_for_screen(
      gtk_widget_get_screen(toplevel),
      GTK_STYLE_PROVIDER(css_provider),
      GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
}

/*****************************************************************************
  Clears a theme (sets default system theme)
*****************************************************************************/
void gui_clear_theme(void)
{
  bool theme_loaded;

  /* try to load user defined theme */
  theme_loaded = load_theme(gui_gtk3_default_theme_name);

  /* no user defined theme loaded -> try to load Freeciv default theme */
  if (!theme_loaded) {
    theme_loaded = load_theme(FC_GTK3_DEFAULT_THEME_NAME);
    if (theme_loaded) {
      sz_strlcpy(gui_gtk3_default_theme_name, FC_GTK3_DEFAULT_THEME_NAME);
    }
  }
    
  /* still no theme loaded -> load system default theme */
  if (!theme_loaded) {
    gtk_style_context_add_provider_for_screen(
        gtk_widget_get_screen(toplevel),
        GTK_STYLE_PROVIDER(gtk_css_provider_get_default()),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
  }
}

/*****************************************************************************
  Each gui has its own themes directories.
  For gtk3 these are:
  - /usr/share/themes
  - ~/.themes
  Returns an array containing these strings and sets array size in count.
  The caller is responsible for freeing the array and the paths.
*****************************************************************************/
char **get_gui_specific_themes_directories(int *count)
{
  gchar *standard_dir;
  char *home_dir;
  const struct strvec *data_dirs = get_data_dirs();
  char **directories = fc_malloc((2 + strvec_size(data_dirs))
                                 * sizeof(char *));

  *count = 0;

  /* Freeciv-specific GTK3 themes directories */
  strvec_iterate(data_dirs, dir_name) {
    char buf[strlen(dir_name) + strlen("/themes/gui-gtk-3.0") + 1];

    fc_snprintf(buf, sizeof(buf), "%s/themes/gui-gtk-3.0", dir_name);

    directories[(*count)++] = fc_strdup(buf);
  } strvec_iterate_end;

  /* standard GTK+ themes directory (e.g. /usr/share/themes) */
  standard_dir = gtk_rc_get_theme_dir();
  directories[(*count)++] = fc_strdup(standard_dir);
  g_free(standard_dir);

  /* user GTK+ themes directory (~/.themes) */
  home_dir = user_home_dir();
  if (home_dir) {
    char buf[strlen(home_dir) + 16];
    
    fc_snprintf(buf, sizeof(buf), "%s/.themes/", home_dir);
    directories[(*count)++] = fc_strdup(buf);
  }

  return directories;
}

/*****************************************************************************
  Return an array of names of usable themes in the given directory.
  Array size is stored in count.
  Useable theme for gtk+ is a directory which contains file gtk-3.0/gtkrc.
  The caller is responsible for freeing the array and the names
*****************************************************************************/
char **get_useable_themes_in_directory(const char *directory, int *count)
{
  DIR *dir;
  struct dirent *entry;
  
  char **theme_names = fc_malloc(sizeof(char *) * 2);
  /* Allocated memory size */
  int t_size = 2;


  *count = 0;

  dir = fc_opendir(directory);
  if (!dir) {
    /* This isn't directory or we can't list it */
    return theme_names;
  }

  while ((entry = readdir(dir))) {
    char buf[strlen(directory) + strlen(entry->d_name) + 32];
    struct stat stat_result;

    fc_snprintf(buf, sizeof(buf),
                "%s/%s/gtk-3.0/gtk.css", directory, entry->d_name);

    if (fc_stat(buf, &stat_result) != 0) {
      /* File doesn't exist */
      continue;
    }
    
    if (!S_ISREG(stat_result.st_mode)) {
      /* Not a regular file */
      continue;
    }
    
    /* Otherwise it's ok */
    
    /* Increase array size if needed */
    if (*count == t_size) {
      theme_names = fc_realloc(theme_names, t_size * 2 * sizeof(char *));
      t_size *= 2;
    }

    theme_names[*count] = fc_strdup(entry->d_name);
    (*count)++;
  }

  closedir(dir);

  return theme_names;
}
