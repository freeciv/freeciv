/***********************************************************************
 Freeciv - Copyright (C) 1996 - A Kjeldberg, L Gregersen, P Unold
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
***********************************************************************/

/***********************************************************************
                          gui_string.c  -  description
                             -------------------
    begin                : June 30 2002
    copyright            : (C) 2002 by Rafał Bursig
    email                : Rafał Bursig <bursig@poczta.fm>
***********************************************************************/

#ifdef HAVE_CONFIG_H
#include <fc_config.h>
#endif

/* SDL3 */
#include <SDL3/SDL.h>

/* utility */
#include "fcintl.h"
#include "log.h"
#include "mem.h"

/* client/gui-sdl3 */
#include "colors.h"
#include "graphics.h"
#include "gui_main.h"
#include "themespec.h"
#include "utf8string.h"

#include "gui_string.h"

/* =================================================== */

static struct ttf_font_chain {
  struct ttf_font_chain *next;
  TTF_Font *font;
  Uint16 ptsize;                /* Size of font */
  Uint16 count;                 /* Number of strings alliased with this font */
} *font_tab = NULL;

static char *font_with_full_path = NULL;

static TTF_Font *load_font(Uint16 ptsize);

static SDL_Surface *create_utf8_surf(utf8_str *pstr);
static SDL_Surface *create_utf8_multi_surf(utf8_str *pstr);

/* Adjust font sizes on 320x240 screen */
#ifdef GUI_SDL3_SMALL_SCREEN
  static int adj_font(int size);
#else  /* GUI_SDL3_SMALL_SCREEN */
  #define adj_font(size) size
#endif /* GUI_SDL3_SMALL_SCREEN */

#define ptsize_default() adj_font(default_font_size(active_theme))

/**********************************************************************//**
  Adjust font sizes for small screen.
**************************************************************************/
#ifdef GUI_SDL3_SMALL_SCREEN
static int adj_font(int size)
{
  switch (size) {
  case 24:
    return 12;
  case 20:
    return 12;
  case 16:
    return 10;
  case 14:
    return 8;
  case 13:
    return 8;
  case 12:
    return 8;
  case 11:
    return 7;
  case 10:
    return 7;
  case 8:
    return 6;
  default:
    return size;
  }
}
#endif /* GUI_SDL3_SMALL_SCREEN */

/**********************************************************************//**
  Calculate display size of string.
**************************************************************************/
void utf8_str_size(utf8_str *pstr, SDL_Rect *fill)
{
  if (pstr != NULL && pstr->text != NULL && pstr->text[0] != '\0') {
    char *current = pstr->text;
    char c = *current;
    bool new_line = FALSE;
    int w, h;

    /* Find '\n' */
    while (c != '\0') {
      if (c == '\n') {
        new_line = TRUE;
        break;
      }
      current++;
      c = *current;
    }

    if (!((pstr->style & 0x0F) & TTF_STYLE_NORMAL)) {
      TTF_SetFontStyle(pstr->font, (pstr->style & 0x0F));
    }

    if (new_line) {
      int ww, hh, count = 0;
      char **utf8_texts = create_new_line_utf8strs(pstr->text);

      w = 0;
      h = 0;
      while (utf8_texts[count]) {
        if (!TTF_GetStringSize(pstr->font, utf8_texts[count], 0,
                               &ww, &hh)) {
          do {
            FC_FREE(utf8_texts[count]);
            count++;
          } while (utf8_texts[count]);
          log_error("TTF_GetStringSize() return ERROR!");
        }
        w = MAX(w, ww);
        h += hh;
        FC_FREE(utf8_texts[count]);
        count++;
      }
    } else {
      if (!TTF_GetStringSize(pstr->font, pstr->text, 0,
                             &w, &h)) {
        log_error("TTF_GetStringSize() return ERROR!");
      }
    }

    if (!((pstr->style & 0x0F) & TTF_STYLE_NORMAL)) {
      TTF_SetFontStyle(pstr->font, TTF_STYLE_NORMAL);
    }

    fill->w = w;
    fill->h = h;
  } else {
    fill->w = 0;
    fill->h = (pstr ? TTF_GetFontHeight(pstr->font) : 0);
  }
}

/**********************************************************************//**
  Convert font_origin to ptsize value
**************************************************************************/
static Uint16 fonto_ptsize(enum font_origin origin)
{
  int def;

  switch (origin) {
  case FONTO_DEFAULT:
    /* Rely on create_utf8_str() default */
    return 0;
  case FONTO_DEFAULT_PLUS:
    def = ptsize_default();
    return adj_font(MAX(def + 0, def * 1.1)); /* Same as def, when def < 10 */
  case FONTO_ATTENTION:
    def = ptsize_default();
    return adj_font(MAX(def + 1, def * 1.2));
  case FONTO_ATTENTION_PLUS:
    def = ptsize_default();
    return adj_font(MAX(def + 1, def * 1.2)); /* Same as FONTO_ATTENTION, when def < 10 */
  case FONTO_HEADING:
    def = ptsize_default();
    return adj_font(MAX(def + 2, def * 1.4));
  case FONTO_BIG:
    def = ptsize_default();
    return adj_font(MAX(def + 3, def * 1.6));
  case FONTO_MAX:
    def = ptsize_default();
    return adj_font(MAX(def + 7, def * 2.4));
  }

  return 0;
}

/**********************************************************************//**
  Create utf8_str struct with ptsize font. If ptsize is zero,
  use theme's default font size.

  Font will be loaded or aliased with existing font of that size.
  in_text must be allocated in memory (fc_malloc() / fc_calloc())
**************************************************************************/
utf8_str *create_utf8_str(char *in_text, size_t n_alloc, Uint16 ptsize)
{
  utf8_str *str = fc_calloc(1, sizeof(utf8_str));

  if (ptsize == 0) {
    str->ptsize = ptsize_default();
  } else {
    str->ptsize = ptsize;
  }

  if ((str->font = load_font(str->ptsize)) == NULL) {
    log_error("create_utf8_str(): load_font() failed");
    FC_FREE(str);

    return NULL;
  }

  str->style = TTF_STYLE_NORMAL;
  str->bgcol = (SDL_Color) {0, 0, 0, 0};
  str->fgcol = *get_theme_color(COLOR_THEME_TEXT);
  str->render = 2;

  /* in_text must be allocated in memory (fc_malloc() / fc_calloc() ) */
  str->text = in_text;
  str->n_alloc = n_alloc;

  return str;
}

/**********************************************************************//**
  Create utf8_str struct with font size from given origin.

  Font will be loaded or aliased with existing font of that size.
  in_text must be allocated in memory (fc_malloc() / fc_calloc())
**************************************************************************/
utf8_str *create_utf8_str_fonto(char *in_text, size_t n_alloc,
                                enum font_origin origin)
{
  return create_utf8_str(in_text, n_alloc, fonto_ptsize(origin));
}

/**********************************************************************//**
  Convert char array to utf8_str. Pointer to target string is needed
  as parameter, but also returned for convenience.
**************************************************************************/
utf8_str *copy_chars_to_utf8_str(utf8_str *pstr, const char *pchars)
{
  size_t n;

  fc_assert_ret_val(pstr != NULL, NULL);
  fc_assert_ret_val(pchars != NULL, NULL);

  n = (strlen(pchars) + 1);

  if (n > pstr->n_alloc) {
    /* Allocated more if this is only a small increase on before: */
    size_t n1 = (3 * pstr->n_alloc) / 2;

    pstr->n_alloc = (n > n1) ? n : n1;
    pstr->text = fc_realloc(pstr->text, pstr->n_alloc);
  }

  fc_snprintf(pstr->text, pstr->n_alloc, "%s", pchars);

  return pstr;
}

/**********************************************************************//**
  Blit text to surface.
**************************************************************************/
int write_utf8(SDL_Surface *dest, Sint16 x, Sint16 y,
               utf8_str *pstr)
{
  SDL_Rect dst_rect = { x, y, 0, 0 };
  SDL_Surface *text = create_text_surf_from_utf8(pstr);

  if (alphablit(text, NULL, dest, &dst_rect, 255) < 0) {
    log_error("write_utf8(): couldn't blit text to display: %s",
              SDL_GetError());
    FREESURFACE(text);

    return -1;
  }

  FREESURFACE(text);

  return 0;
}

/**********************************************************************//**
  Create Text Surface from utf8_str
**************************************************************************/
static SDL_Surface *create_utf8_surf(utf8_str *pstr)
{
  SDL_Surface *text = NULL;

  if (pstr == NULL) {
    return NULL;
  }

  if (!((pstr->style & 0x0F) & TTF_STYLE_NORMAL)) {
    TTF_SetFontStyle(pstr->font, (pstr->style & 0x0F));
  }

  switch (pstr->render) {
  case 0:
    text = TTF_RenderText_Shaded(pstr->font,
                                 pstr->text, 0, pstr->fgcol,
                                 pstr->bgcol);
    break;
  case 1:
    text = TTF_RenderText_LCD(pstr->font, pstr->text, 0,
                              pstr->fgcol, pstr->bgcol);
    break;
  case 2:
    text = TTF_RenderText_Blended(pstr->font, pstr->text, 0, pstr->fgcol);
    break;
  }

  if (text != NULL) {
    log_debug("create_utf8_surf: Font is generally %d big, and "
              "string is %d big",
              TTF_GetFontHeight(pstr->font), text->h);
    log_debug("create_utf8_surf: String is %d length", text->w);
  } else {
    log_debug("create_utf8_surf: text NULL");
    text = create_surf(0, 0);
  }

  if (!((pstr->style & 0x0F) & TTF_STYLE_NORMAL)) {
    TTF_SetFontStyle(pstr->font, TTF_STYLE_NORMAL);
  }

  return text;
}

/**********************************************************************//**
  Create surface with multiline text drawn.
**************************************************************************/
static SDL_Surface *create_utf8_multi_surf(utf8_str *pstr)
{
  SDL_Rect des = {0, 0, 0, 0};
  SDL_Surface *text = NULL, **tmp = NULL;
  Uint16 i, w = 0, count = 0;
  Uint32 color;
  char *buf = pstr->text;
  char **utf8_texts = create_new_line_utf8strs(pstr->text);

  while (utf8_texts[count]) {
    count++;
  }

  tmp = fc_calloc(count, sizeof(SDL_Surface *));

  for (i = 0; i < count; i++) {
    pstr->text = utf8_texts[i];
    tmp[i] = create_utf8_surf(pstr);

    /* Find max len */
    if (tmp[i]->w > w) {
      w = tmp[i]->w;
    }
  }

  pstr->text = buf;

  /* Create and fill surface */

  if (!SDL_GetSurfaceColorKey(tmp[0], &color)) {
    color = SDL_MapRGBA(SDL_GetPixelFormatDetails(tmp[0]->format), NULL,
                        0, 0, 0, 0);
  }

  switch (pstr->render) {
  case 1:
    text = create_surf(w, count * tmp[0]->h);
    SDL_FillSurfaceRect(text, NULL, color);
    SDL_SetSurfaceColorKey(text, true, color);
    break;
  case 2:
    text = create_surf_with_format(tmp[0]->format,
                                   w, count * tmp[0]->h);
    SDL_FillSurfaceRect(text, NULL, color);
    break;
  default:
    text = create_surf(w, count * tmp[0]->h);
    SDL_FillSurfaceRect(text, NULL, color);
    break;
  }

  /* Blit (default: center left) */
  for (i = 0; i < count; i++) {
    if (pstr->style & SF_CENTER) {
      des.x = (w - tmp[i]->w) / 2;
    } else {
      if (pstr->style & SF_CENTER_RIGHT) {
        des.x = w - tmp[i]->w;
      } else {
        des.x = 0;
      }
    }

    alphablit(tmp[i], NULL, text, &des, 255);
    des.y += tmp[i]->h;
  }


  /* Free Memory */
  for (i = 0; i < count; i++) {
    FC_FREE(utf8_texts[i]);
    FREESURFACE(tmp[i]);
  }

  FC_FREE(tmp);

  return text;
}

/**********************************************************************//**
  Generic function to create surface with any kind of text, single line or
  multiline, drawn.
**************************************************************************/
SDL_Surface *create_text_surf_from_utf8(utf8_str *pstr)
{
  if (pstr != NULL && pstr->text != NULL) {
    char *current = pstr->text;
    char c = *(pstr->text);

    /* Find '\n' */
    while (c != '\0') {
      if (c == '\n') {
        return create_utf8_multi_surf(pstr);
      }
      current++;
      c = *current;
    }

    return create_utf8_surf(pstr);
  }

  return NULL;
}

/**********************************************************************//**
  Wrap text to make it fit to given screen width.
**************************************************************************/
bool convert_utf8_str_to_const_surface_width(utf8_str *pstr, int width)
{
  SDL_Rect size;
  bool converted = FALSE;

  fc_assert_ret_val(pstr != NULL, FALSE);
  fc_assert_ret_val(pstr->text != NULL, FALSE);

  utf8_str_size(pstr, &size);
  if (size.w > width) {
    /* Cut string length to w length by replacing space " " with new line "\n" */
    bool resize = FALSE;
    int len = 0;
    char *ptr_rev, *ptr = pstr->text;

    converted = TRUE;

    do {
      if (!resize) {
        char utf8char[9];
        int i;
        int fw, fh;

        if (*ptr == '\0') {
          resize = TRUE;
          continue;
        }

        if (*ptr == '\n') {
          len = 0;
          ptr++;
          continue;
        }

        utf8char[0] = ptr[0];
        for (i = 1; i < 8 && (ptr[i] & (128 + 64)) == 128; i++) {
          utf8char[i] = ptr[i];
        }
        utf8char[i] = '\0';
        if (!((pstr->style & 0x0F) & TTF_STYLE_NORMAL)) {
          TTF_SetFontStyle(pstr->font, (pstr->style & 0x0F));
        }
        TTF_GetStringSize(pstr->font, utf8char, 0, &fw, &fh);
        if (!((pstr->style & 0x0F) & TTF_STYLE_NORMAL)) {
          TTF_SetFontStyle(pstr->font, TTF_STYLE_NORMAL);
        }

        len += fw;

        if (len > width) {
          ptr_rev = ptr;
          while (ptr_rev != pstr->text) {
            if (*ptr_rev == ' ') {
              *ptr_rev = '\n';
              utf8_str_size(pstr, &size);
              len = 0;
              break;
            }
            if (*ptr_rev == '\n') {
              resize = TRUE;
              break;
            }
            ptr_rev--;
          }
          if (ptr_rev == pstr->text) {
            resize = TRUE;
          }
        }

        ptr += i;
      } else {
        if (pstr->ptsize > 8) {
          change_ptsize_utf8(pstr, pstr->ptsize - 1);
          utf8_str_size(pstr, &size);
        } else {
          log_error("Can't convert string to const width");
          break;
        }
      }

    } while (size.w > width);
  }

  return converted;
}

/**********************************************************************//**
  Create surface with text drawn to it. Wrap text as needed to make it
  fit in given width.
**************************************************************************/
SDL_Surface *create_text_surf_smaller_than_w(utf8_str *pstr, int w)
{
  int ptsize;
  SDL_Surface *text;

  fc_assert_ret_val(pstr != NULL, NULL);

  ptsize = pstr->ptsize;
  convert_utf8_str_to_const_surface_width(pstr, w);
  text = create_text_surf_from_utf8(pstr);
  if (pstr->ptsize != ptsize) {
    change_ptsize_utf8(pstr, ptsize);
  }

  return text;
}

/**********************************************************************//**
  Change font size of text.
**************************************************************************/
void change_ptsize_utf8(utf8_str *pstr, Uint16 new_ptsize)
{
  TTF_Font *buf;

  if (new_ptsize == 0) {
    new_ptsize = ptsize_default();
  }

  if (pstr->ptsize == new_ptsize) {
    return;
  }

  if ((buf = load_font(new_ptsize)) == NULL) {
    log_error("change_ptsize: load_font() failed");
    return;
  }

  unload_font(pstr->ptsize);
  pstr->ptsize = new_ptsize;
  pstr->font = buf;
}

/**********************************************************************//**
  Change font size of text to that from given origin.
**************************************************************************/
void change_fonto_utf8(utf8_str *pstr, enum font_origin origin)
{
  change_ptsize_utf8(pstr, fonto_ptsize(origin));
}

/* =================================================== */

/**********************************************************************//**
  Load font of given pointsize.
**************************************************************************/
static TTF_Font *load_font(Uint16 ptsize)
{
  struct ttf_font_chain *font_tab_tmp = font_tab;
  TTF_Font *font_tmp = NULL;

  /* Find existing font and return pointer to it */
  if (font_tab != NULL) {

    while (font_tab_tmp != NULL) {
      if (font_tab_tmp->ptsize == ptsize) {
        font_tab_tmp->count++;

        return font_tab_tmp->font;
      }

      font_tab_tmp = font_tab_tmp->next;
    }
  }

  if (!font_with_full_path) {
    const char *path = theme_font_filename(active_theme);

    font_with_full_path = fc_strdup(path);
    fc_assert_ret_val(font_with_full_path != NULL, NULL);
  }

  /* Load Font */
  if ((font_tmp = TTF_OpenFont(font_with_full_path, ptsize)) == NULL) {
    log_error("load_font: Couldn't load %d pt font from %s: %s",
              ptsize, font_with_full_path, SDL_GetError());
    return NULL;
  }

  /* Add new font to list */
  if (font_tab == NULL) {
    font_tab_tmp = fc_calloc(1, sizeof(struct ttf_font_chain));
    font_tab = font_tab_tmp;
  } else {
    /* Go to end of chain */
    font_tab_tmp = font_tab;
    while (font_tab_tmp->next) {
      font_tab_tmp = font_tab_tmp->next;
    }

    font_tab_tmp->next = fc_calloc(1, sizeof(struct ttf_font_chain));
    font_tab_tmp = font_tab_tmp->next;
  }

  font_tab_tmp->ptsize = ptsize;
  font_tab_tmp->count = 1;
  font_tab_tmp->font = font_tmp;
  font_tab_tmp->next = NULL;

  return font_tmp;
}

/**********************************************************************//**
  Free font of given pointsize.
**************************************************************************/
void unload_font(Uint16 ptsize)
{
  struct ttf_font_chain *font_tab_prev = NULL;
  struct ttf_font_chain *font_tab_tmp = font_tab;

  if (font_tab == NULL) {
    log_error("unload_font: Trying unload from empty Font ARRAY");
    return;
  }

  while (font_tab_tmp != NULL) {
    if (font_tab_tmp->ptsize == ptsize) {
      break;
    }
    font_tab_prev = font_tab_tmp;
    font_tab_tmp = font_tab_tmp->next;
  }

  if (font_tab_tmp == NULL) {
    log_error("unload_font: Trying unload Font which is "
              "not included in Font ARRAY");
    return;
  }

  if (--font_tab_tmp->count > 0) {
    return;
  }

  TTF_CloseFont(font_tab_tmp->font);
  font_tab_tmp->font = NULL;

  if (font_tab_prev == NULL) {
    font_tab = font_tab_tmp->next;
  } else {
    font_tab_prev->next = font_tab_tmp->next;
  }

  font_tab_tmp->next = NULL;

  FC_FREE(font_tab_tmp);
}

/**********************************************************************//**
  Free all fonts.
**************************************************************************/
void free_font_system(void)
{
  struct ttf_font_chain *font_tab_tmp;

  FC_FREE(font_with_full_path);
  while (font_tab) {
    if (font_tab->next) {
      font_tab_tmp = font_tab;
      font_tab = font_tab->next;
      if (font_tab_tmp->font) {
        TTF_CloseFont(font_tab_tmp->font);
      }
      FC_FREE(font_tab_tmp);
    } else {
      if (font_tab->font) {
        TTF_CloseFont(font_tab->font);
      }
      FC_FREE(font_tab);
    }
  }
}
