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
#include <config.h>
#endif

#include <assert.h>

#include <unistd.h>
#include <string.h>
#include <sys/stat.h>


#include "mem.h"
#include "shared.h"
#include "support.h"

#include "themes_common.h"

#include "themes_g.h"

/***************************************************************************
  A theme is a portion of client data, which for following reasons should
  be separated from a tileset:
  - Theme is not only graphic related
  - Theme can be changed independently from tileset
  - Theme implementation is gui specific and most themes can not be shared
    between different guis.
  Theme is recognized by its name.
  
  Theme is stored in a directory called like the theme. The directory contains
  some data files. Each gui defines its own format in the
  get_useable_themes_in_directory() function.
****************************************************************************/

/* A directory containing a list of usable themes */
struct theme_directory {
  /* Path on the filesystem */
  char *path; 
  /* Array of theme names */
  char **themes;
  /* Themes array length */
  int num_themes;
};

/* List of all directories with themes */
static int num_directories;
struct theme_directory *directories;

/****************************************************************************
  Initialized themes data
****************************************************************************/
void init_themes(void)
{
  char **directories_paths = fc_malloc(sizeof(char *) * 2);
  /* length of the directories_paths array */
  int count = 0;
  /* allocated size of the directories_paths array */
  int t_size = 2;


  int i;

  struct stat stat_result;
  int gui_directories_count;
  char **gui_directories;

  char *directory_tokens[strlen(DEFAULT_DATA_PATH)];
  int num_tokens;

  /* Directories are separated with : or ; */
  num_tokens = get_tokens(DEFAULT_DATA_PATH,
                          directory_tokens,
			  strlen(DEFAULT_DATA_PATH),
			  ":;");
  
  for (i = 0; i < num_tokens; i++) {
    char buf[strlen(DEFAULT_DATA_PATH) + 16];

    /* Check if there's 'themes' subdirectory */
    my_snprintf(buf, sizeof(buf), "%s/themes", directory_tokens[i]);
    
    free(directory_tokens[i]);
    
    if (stat(buf, &stat_result) != 0) {
      continue;
    }
    if (!S_ISDIR(stat_result.st_mode)) {
      continue;
    }

    /* Increase array size if needed */
    if (t_size == count) {
      directories_paths =
	  fc_realloc(directories_paths, t_size * 2 * sizeof(char *));
      t_size *= 2;
    }

    directories_paths[count] = mystrdup(buf);
    count++;
  }

  /* Add gui specific directories */
  gui_directories =
      get_gui_specific_themes_directories(&gui_directories_count);

  for (i = 0; i < gui_directories_count; i++) {
    if (t_size == count) {
      directories_paths =
	  fc_realloc(directories_paths, t_size * 2 * sizeof(char *));
      t_size *= 2;
    }
    /* We are responsible for freeing the memory, so we don't need to copy */
    directories_paths[count++] = gui_directories[i];
  }
  free(gui_directories);

  /* Load useable themes in those directories */
  directories = fc_malloc(sizeof(struct theme_directory) * count);
  for (i = 0; i < count; i++) {
    directories[i].path = directories_paths[i];

    /* Gui specific function must search for themes */
    directories[i].themes =
	get_useable_themes_in_directory(directories_paths[i],
					&(directories[i].num_themes));
  }
  num_directories = count;
  free(directories_paths);
}

/****************************************************************************
  Return an array of useable theme names. The array is NULL terminated and 
  the caller is responsible for freeing the array.
****************************************************************************/
const char **get_themes_list(void)
{
  int size = 0;
  int i, j, k, c;
  const char **themes;

  for (i = 0; i < num_directories; i++) {
    size += directories[i].num_themes;
  }

  themes = fc_malloc(sizeof(char *) * (size + 1));

  /* Copy theme names from all directories, but remove duplicates */
  c = 0;
  for (i = 0; i < num_directories; i++) {
    for (j = 0; j < directories[i].num_themes; j++) {
      for (k = 0; k < c; k++) {
	if (strcmp(themes[k], directories[i].themes[j]) == 0) {
	  break;
	}
      }
      if (k == c) {
	themes[c++] = directories[i].themes[j];
      }
    }
  }
  themes[c] = NULL;
  return themes;
}

/****************************************************************************
  Loads a theme with the given name. First matching directory will be used.
****************************************************************************/
void load_theme(const char *theme_name)
{
  int i, j;
  for (i = 0; i < num_directories; i++) {
    for (j = 0; j < directories[i].num_themes; j++) {
      if (strcmp(theme_name, directories[i].themes[j]) == 0) {
	gui_load_theme(directories[i].path, directories[i].themes[j]);
	return;
      }
    }
  }
}
