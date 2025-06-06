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
                          graphics.c  -  description
                             -------------------
    begin                : Mon Jul 1 2002
    copyright            : (C) 2000 by Michael Speck
                         : (C) 2002 by Rafał Bursig
    email                : Michael Speck <kulkanie@gmx.net>
                         : Rafał Bursig <bursig@poczta.fm>
***********************************************************************/

#ifdef HAVE_CONFIG_H
#include <fc_config.h>
#endif

/* SDL2 */
#ifdef SDL2_PLAIN_INCLUDE
#include <SDL_image.h>
#include <SDL_ttf.h>
#else  /* SDL2_PLAIN_INCLUDE */
#include <SDL2/SDL_image.h>
#include <SDL2/SDL_ttf.h>
#endif /* SDL2_PLAIN_INCLUDE */

/* utility */
#include "fcintl.h"
#include "log.h"

/* client */
#include "tilespec.h"

/* gui-sdl2 */
#include "colors.h"
#include "gui_tilespec.h"
#include "mapview.h"
#include "themebackgrounds.h"
#include "themespec.h"

#include "graphics.h"

/* ------------------------------ */

struct sdl2_data main_data;

static SDL_Surface *main_surface = NULL;

static bool render_dirty = TRUE;

/**********************************************************************//**
  Allocate new gui_layer.
**************************************************************************/
struct gui_layer *gui_layer_new(int x, int y, SDL_Surface *surface)
{
  struct gui_layer *result;

  result = fc_calloc(1, sizeof(struct gui_layer));

  result->dest_rect = (SDL_Rect){x, y, 0, 0};
  result->surface = surface;

  return result;
}

/**********************************************************************//**
  Free resources associated with gui_layer.
**************************************************************************/
void gui_layer_destroy(struct gui_layer **gui_layer)
{
  FREESURFACE((*gui_layer)->surface);
  FC_FREE(*gui_layer);
}

/**********************************************************************//**
  Get surface gui_layer.
**************************************************************************/
struct gui_layer *get_gui_layer(SDL_Surface *surface)
{
  int i = 0;

  while ((i < main_data.guis_count) && main_data.guis[i]) {
    if (main_data.guis[i]->surface == surface) {
      return main_data.guis[i];
    }
    i++;
  }

  return NULL;
}

/**********************************************************************//**
  Buffer allocation function.
  This function is call by "create_window(...)" function and allocate
  buffer layer for this function.

  Pointer for this buffer is put in buffer array on last position that
  flush functions will draw this layer last.
**************************************************************************/
struct gui_layer *add_gui_layer(int width, int height)
{
  struct gui_layer *gui_layer = NULL;
  SDL_Surface *buffer;

  buffer = create_surf(width, height, SDL_SWSURFACE);
  gui_layer = gui_layer_new(0, 0, buffer);

  /* add to buffers array */
  if (main_data.guis) {
    int i;

    /* find NULL element */
    for (i = 0; i < main_data.guis_count; i++) {
      if (!main_data.guis[i]) {
        main_data.guis[i] = gui_layer;
        return gui_layer;
      }
    }
    main_data.guis_count++;
    main_data.guis = fc_realloc(main_data.guis,
                                main_data.guis_count * sizeof(struct gui_layer *));
    main_data.guis[main_data.guis_count - 1] = gui_layer;
  } else {
    main_data.guis = fc_calloc(1, sizeof(struct gui_layer *));
    main_data.guis[0] = gui_layer;
    main_data.guis_count = 1;
  }

  return gui_layer;
}

/**********************************************************************//**
  Free buffer layer ( call by popdown_window_group_dialog(...) func )
  Func. Free buffer layer and clear buffer array entry.
**************************************************************************/
void remove_gui_layer(struct gui_layer *gui_layer)
{
  int i;

  for (i = 0; i < main_data.guis_count - 1; i++) {
    if (main_data.guis[i] && (main_data.guis[i] == gui_layer)) {
      gui_layer_destroy(&main_data.guis[i]);
      main_data.guis[i] = main_data.guis[i + 1];
      main_data.guis[i + 1] = NULL;
    } else {
      if (!main_data.guis[i]) {
        main_data.guis[i] = main_data.guis[i + 1];
        main_data.guis[i + 1] = NULL;
      }
    }
  }

  if (main_data.guis[main_data.guis_count - 1]) {
    gui_layer_destroy(&main_data.guis[main_data.guis_count - 1]);
  }
}

/**********************************************************************//**
  Translate dest_rect from global screen to gui_layer's coordinates.
**************************************************************************/
void screen_rect_to_layer_rect(struct gui_layer *gui_layer,
                               SDL_Rect *dest_rect)
{
  if (gui_layer) {
    dest_rect->x = dest_rect->x - gui_layer->dest_rect.x;
    dest_rect->y = dest_rect->y - gui_layer->dest_rect.y;
  }
}

/**********************************************************************//**
  Translate dest_rect from gui_layer's to global screen coordinates.
**************************************************************************/
void layer_rect_to_screen_rect(struct gui_layer *gui_layer,
                               SDL_Rect *dest_rect)
{
  if (gui_layer) {
    dest_rect->x = dest_rect->x + gui_layer->dest_rect.x;
    dest_rect->y = dest_rect->y + gui_layer->dest_rect.y;
  }
}

/* ============ Freeciv sdl graphics function =========== */

/**********************************************************************//**
  Execute alphablit.
**************************************************************************/
int alphablit(SDL_Surface *src, SDL_Rect *srcrect,
              SDL_Surface *dst, SDL_Rect *dstrect,
              unsigned char alpha_mod)
{
  int ret;

  if (src == NULL || dst == NULL) {
    return 1;
  }

  SDL_SetSurfaceAlphaMod(src, alpha_mod);

  ret = SDL_BlitSurface(src, srcrect, dst, dstrect);

  if (ret) {
    log_error("SDL_BlitSurface() fails: %s", SDL_GetError());
  }

  return ret;
}

/**********************************************************************//**
  Execute alphablit to the main surface
**************************************************************************/
int screen_blit(SDL_Surface *src, SDL_Rect *srcrect, SDL_Rect *dstrect,
                unsigned char alpha_mod)
{
  render_dirty = TRUE;

  return alphablit(src, srcrect, main_surface, dstrect, alpha_mod);
}

/**********************************************************************//**
  Create new surface (prect->w x prect->h size) and copy prect area of
  psource.
  if prect == NULL then create copy of entire psource.
**************************************************************************/
SDL_Surface *crop_rect_from_surface(SDL_Surface *psource,
                                    SDL_Rect *prect)
{
  SDL_Surface *new_surf = create_surf_with_format(psource->format,
                                              prect ? prect->w : psource->w,
                                              prect ? prect->h : psource->h,
                                              SDL_SWSURFACE);

  if (alphablit(psource, prect, new_surf, NULL, 255) != 0) {
    FREESURFACE(new_surf);

    return NULL;
  }

  return new_surf;
}

/**********************************************************************//**
  Reduce the alpha of the final surface proportional to the alpha of the mask.
  Thus if the mask has 50% alpha the final image will be reduced by 50% alpha.

  mask_offset_x, mask_offset_y is the offset of the mask relative to the
  origin of the source image.  The pixel at (mask_offset_x, mask_offset_y)
  in the mask image will be used to clip pixel (0, 0) in the source image
  which is pixel (-x, -y) in the new image.
**************************************************************************/
SDL_Surface *mask_surface(SDL_Surface *src, SDL_Surface *mask,
                          int mask_offset_x, int mask_offset_y)
{
  SDL_Surface *dest = NULL;
  int row, col;
  Uint32 *src_pixel = NULL;
  Uint32 *dest_pixel = NULL;
  Uint32 *mask_pixel = NULL;
  unsigned char src_alpha, mask_alpha;

  dest = copy_surface(src);

  lock_surf(src);
  lock_surf(mask);
  lock_surf(dest);

  src_pixel = (Uint32 *)src->pixels;
  dest_pixel = (Uint32 *)dest->pixels;

  for (row = 0; row < src->h; row++) {
    mask_pixel = (Uint32 *)mask->pixels
                 + mask->w * (row + mask_offset_y)
                 + mask_offset_x;

    for (col = 0; col < src->w; col++) {
      src_alpha = (*src_pixel & src->format->Amask) >> src->format->Ashift;
      mask_alpha = (*mask_pixel & mask->format->Amask) >> mask->format->Ashift;

      *dest_pixel = (*src_pixel & ~src->format->Amask)
        | (((src_alpha * mask_alpha) / 255) << dest->format->Ashift);

      src_pixel++; dest_pixel++; mask_pixel++;
    }
  }

  unlock_surf(dest);
  unlock_surf(mask);
  unlock_surf(src);

  return dest;
}

/**********************************************************************//**
  Load a surface from file putting it in software mem.
**************************************************************************/
SDL_Surface *load_surf(const char *fname)
{
  SDL_Surface *buf;

  if (!fname) {
    return NULL;
  }

  if ((buf = IMG_Load(fname)) == NULL) {
    log_error(_("load_surf: Failed to load graphic file %s!"), fname);

    return NULL;
  }

  return buf;
}

/**********************************************************************//**
  Create an surface with format
  MUST NOT BE USED IF NO SDLSCREEN IS SET
**************************************************************************/
SDL_Surface *create_surf_with_format(SDL_PixelFormat *pf,
                                     int width, int height,
                                     Uint32 flags)
{
  SDL_Surface *surf = SDL_CreateRGBSurface(flags, width, height,
                                           pf->BitsPerPixel,
                                           pf->Rmask,
                                           pf->Gmask,
                                           pf->Bmask, pf->Amask);

  if (surf == NULL) {
    log_error(_("Unable to create Sprite (Surface) of size "
                "%d x %d %d Bits in format %d"),
              width, height, pf->BitsPerPixel, flags);
    return NULL;
  }

  return surf;
}

/**********************************************************************//**
  Create surface with the same format as main window
**************************************************************************/
SDL_Surface *create_surf(int width, int height, Uint32 flags)
{
  return create_surf_with_format(main_surface->format, width, height, flags);
}

/**********************************************************************//**
  Convert surface to the main window format.
**************************************************************************/
SDL_Surface *convert_surf(SDL_Surface *surf_in)
{
  return SDL_ConvertSurface(surf_in, main_surface->format, 0);
}

/**********************************************************************//**
  Create an surface with screen format and fill with color.
  If pcolor == NULL surface is filled with transparent white A = 128
**************************************************************************/
SDL_Surface *create_filled_surface(Uint16 w, Uint16 h, Uint32 flags,
                                   SDL_Color *pcolor)
{
  SDL_Surface *new_surf;
  SDL_Color color = {255, 255, 255, 128};

  new_surf = create_surf(w, h, flags);

  if (!new_surf) {
    return NULL;
  }

  if (!pcolor) {
    /* pcolor->unused == ALPHA */
    pcolor = &color;
  }

  SDL_FillRect(new_surf, NULL,
               SDL_MapRGBA(new_surf->format, pcolor->r, pcolor->g, pcolor->b,
                           pcolor->a));

  if (pcolor->a != 255) {
    SDL_SetSurfaceAlphaMod(new_surf, pcolor->a);
  }

  return new_surf;
}

/**********************************************************************//**
  Fill surface with (0, 0, 0, 0), so the next blitting operation can set
  the per pixel alpha
**************************************************************************/
int clear_surface(SDL_Surface *surf, SDL_Rect *dstrect)
{
  /* SDL_FillRect might change the rectangle, so we create a copy */
  if (dstrect) {
    SDL_Rect _dstrect = *dstrect;

    return SDL_FillRect(surf, &_dstrect, SDL_MapRGBA(surf->format, 0, 0, 0, 0));
  } else {
    return SDL_FillRect(surf, NULL, SDL_MapRGBA(surf->format, 0, 0, 0, 0));
  }
}

/**********************************************************************//**
  Blit entire src [SOURCE] surface to destination [DEST] surface
  on position : [dest_x],[dest_y] using it's actual alpha and
  color key settings.
**************************************************************************/
int blit_entire_src(SDL_Surface *psrc, SDL_Surface *pdest,
                    Sint16 dest_x, Sint16 dest_y)
{
  SDL_Rect dest_rect = { dest_x, dest_y, 0, 0 };

  return alphablit(psrc, NULL, pdest, &dest_rect, 255);
}

/**********************************************************************//**
  Get pixel
  Return the pixel value at (x, y)
  NOTE: The surface must be locked before calling this!
**************************************************************************/
Uint32 get_pixel(SDL_Surface *surf, int x, int y)
{
  if (!surf) {
    return 0x0;
  }

  switch (surf->format->BytesPerPixel) {
  case 1:
    return *(Uint8 *) ((Uint8 *) surf->pixels + y * surf->pitch + x);

  case 2:
    return *((Uint16 *)surf->pixels + y * surf->pitch / sizeof(Uint16) + x);

  case 3:
    {
      /* Here ptr is the address to the pixel we want to retrieve */
      Uint8 *ptr =
        (Uint8 *) surf->pixels + y * surf->pitch + x * 3;

      if (is_bigendian()) {
        return ptr[0] << 16 | ptr[1] << 8 | ptr[2];
      } else {
        return ptr[0] | ptr[1] << 8 | ptr[2] << 16;
      }
    }
  case 4:
    return *((Uint32 *)surf->pixels + y * surf->pitch / sizeof(Uint32) + x);

  default:
    return 0; /* Shouldn't happen, but avoids warnings */
  }
}

/**********************************************************************//**
  Get first pixel
  Return the pixel value at (0, 0)
  NOTE: The surface must be locked before calling this!
**************************************************************************/
Uint32 get_first_pixel(SDL_Surface *surf)
{
  if (!surf) {
    return 0;
  }

  switch (surf->format->BytesPerPixel) {
  case 1:
    return *((Uint8 *)surf->pixels);

  case 2:
    return *((Uint16 *)surf->pixels);

  case 3:
    {
      if (is_bigendian()) {
        return (((Uint8 *)surf->pixels)[0] << 16)
          | (((Uint8 *)surf->pixels)[1] << 8)
          | ((Uint8 *)surf->pixels)[2];
      } else {
        return ((Uint8 *)surf->pixels)[0]
          | (((Uint8 *)surf->pixels)[1] << 8)
          | (((Uint8 *)surf->pixels)[2] << 16);
      }
    }
  case 4:
    return *((Uint32 *)surf->pixels);

  default:
    return 0; /* shouldn't happen, but avoids warnings */
  }
}

/* ===================================================================== */

/**********************************************************************//**
  Initialize sdl with Flags
**************************************************************************/
void init_sdl(int flags)
{
  bool error;

  main_data.screen = NULL;
  main_data.guis = NULL;
  main_data.gui = NULL;
  main_data.map = NULL;
  main_data.dummy = NULL; /* Can't create yet -- hope we don't need it */
  main_data.renderer = NULL;
  main_data.rects_count = 0;
  main_data.guis_count = 0;

  if (SDL_WasInit(SDL_INIT_AUDIO)) {
    error = (SDL_InitSubSystem(flags) < 0);
  } else {
    error = (SDL_Init(flags) < 0);
  }
  if (error) {
    log_fatal(_("Unable to initialize SDL2 library: %s"), SDL_GetError());
    exit(EXIT_FAILURE);
  }

  atexit(SDL_Quit);

  /* Initialize the TTF library */
  if (TTF_Init() < 0) {
    log_fatal(_("Unable to initialize SDL2_ttf library: %s"), SDL_GetError());
    exit(EXIT_FAILURE);
  }

  atexit(TTF_Quit);
}

/**********************************************************************//**
  Free existing surfaces
**************************************************************************/
static void free_surfaces(void)
{
  if (main_data.renderer != NULL) {
    SDL_DestroyRenderer(main_data.renderer);
    main_data.renderer = NULL;

    FREESURFACE(main_surface);
  }
}

/**********************************************************************//**
  Create new main window surfaces.
**************************************************************************/
bool create_surfaces(int width, int height)
{
  unsigned flags;

  free_surfaces();

  if (is_bigendian()) {
    main_surface = SDL_CreateRGBSurface(0, width, height, 32,
                                        0x0000FF00, 0x00FF0000,
                                        0xFF000000, 0x000000FF);
    main_data.map = SDL_CreateRGBSurface(0, width, height, 32,
                                         0x0000FF00, 0x00FF0000,
                                         0xFF000000, 0x000000FF);
  } else {
    main_surface = SDL_CreateRGBSurface(0, width, height, 32,
                                        0x00FF0000, 0x0000FF00,
                                        0x000000FF, 0xFF000000);
    main_data.map = SDL_CreateRGBSurface(0, width, height, 32,
                                         0x00FF0000, 0x0000FF00,
                                         0x000000FF, 0xFF000000);
  }

  if (main_surface == NULL || main_data.map == NULL) {
    log_fatal(_("Failed to create RGB surface: %s"), SDL_GetError());
    return FALSE;
  }

  if (GUI_SDL_OPTION(swrenderer)
      || (sdl2_client_flags & CF_SWRENDERER)) {
    flags = SDL_RENDERER_SOFTWARE;
  } else {
    flags = 0;
  }

  main_data.renderer = SDL_CreateRenderer(main_data.screen, -1, flags);

  if (main_data.renderer == NULL) {
    log_fatal(_("Failed to create renderer: %s"), SDL_GetError());
    return FALSE;
  }

  main_data.maintext = SDL_CreateTexture(main_data.renderer,
                                         SDL_PIXELFORMAT_ARGB8888,
                                         SDL_TEXTUREACCESS_STREAMING,
                                         width, height);

  if (main_data.maintext == NULL) {
    log_fatal(_("Failed to create texture: %s"), SDL_GetError());
    return FALSE;
  }

  if (main_data.gui) {
    FREESURFACE(main_data.gui->surface);
    main_data.gui->surface = create_surf(width, height, SDL_SWSURFACE);
  } else {
    main_data.gui = add_gui_layer(width, height);
  }

  clear_surface(main_data.gui->surface, NULL);

  return TRUE;
}

/**********************************************************************//**
  Free screen buffers
**************************************************************************/
void quit_sdl(void)
{
  FC_FREE(main_data.guis);
  gui_layer_destroy(&main_data.gui);
  FREESURFACE(main_data.map);
  FREESURFACE(main_data.dummy);
  SDL_DestroyTexture(main_data.maintext);

  free_surfaces();
}

/**********************************************************************//**
  Switch to given video mode.
**************************************************************************/
bool set_video_mode(unsigned width, unsigned height, unsigned flags_in)
{
  main_data.screen = SDL_CreateWindow(_("SDL2 Client for Freeciv"),
                                      SDL_WINDOWPOS_UNDEFINED,
                                      SDL_WINDOWPOS_UNDEFINED,
                                      width, height,
                                      0);

  if (main_data.screen == NULL) {
    log_fatal(_("Failed to create main window: %s"), SDL_GetError());
    return FALSE;
  }

  if (flags_in & SDL_WINDOW_FULLSCREEN) {
    SDL_DisplayMode mode;

    /* Use SDL_WINDOW_FULLSCREEN_DESKTOP instead of real SDL_WINDOW_FULLSCREEN */
    SDL_SetWindowFullscreen(main_data.screen, SDL_WINDOW_FULLSCREEN_DESKTOP);
    SDL_GetWindowDisplayMode(main_data.screen, &mode);
    width = mode.w;
    height = mode.h;
  }

  if (!create_surfaces(width, height)) {
    return FALSE;
  }

  return TRUE;
}

/**********************************************************************//**
  Render from main surface with screen renderer.
**************************************************************************/
void update_main_screen(void)
{
  if (render_dirty) {
    SDL_UpdateTexture(main_data.maintext, NULL,
                      main_surface->pixels, main_surface->pitch);
    SDL_RenderClear(main_data.renderer);
    SDL_RenderCopy(main_data.renderer, main_data.maintext, NULL, NULL);
    SDL_RenderPresent(main_data.renderer);

    render_dirty = FALSE;
  }
}

/**********************************************************************//**
  Return width of the main window
**************************************************************************/
int main_window_width(void)
{
  return main_surface->w;
}

/**********************************************************************//**
  Return height of the main window
**************************************************************************/
int main_window_height(void)
{
  return main_surface->h;
}

/**************************************************************************
                           Fill Rect with RGBA color
**************************************************************************/
#define MASK565 0xf7de
#define MASK555 0xfbde

/* 50% alpha (128) */
#define BLEND16_50( d, s , mask )       \
        (((( s & mask ) + ( d & mask )) >> 1) + ( s & d & ( ~mask & 0xffff)))

#define BLEND2x16_50( d, s , mask )     \
        (((( s & (mask | mask << 16)) + ( d & ( mask | mask << 16 ))) >> 1) + \
        ( s & d & ( ~(mask | mask << 16))))

/**********************************************************************//**
  Fill rectangle for 32bit "8888" format surface
**************************************************************************/
static int __FillRectAlpha8888_32bit(SDL_Surface *surf, SDL_Rect *prect,
                                     SDL_Color *pcolor)
{
  register Uint32 A = pcolor->a;
  register Uint32 dSIMD1, dSIMD2;
  register Uint32 sSIMD1, sSIMD2 = SDL_MapRGB(surf->format,
                                              pcolor->r, pcolor->g,
                                              pcolor->b);
  Uint32 y, end, A_Dst, A_Mask = surf->format->Amask;
  Uint32 *start, *pixel;

  sSIMD1 = sSIMD2 & 0x00FF00FF;

  lock_surf(surf);

  if (prect == NULL) {
    end = surf->w * surf->h;
    pixel = (Uint32 *) surf->pixels;
    if (A == 128) { /* 50% A */
      DUFFS_LOOP8(
      {
        dSIMD2 = *pixel;
        A_Dst = dSIMD2 & A_Mask;
        *pixel++ = ((((sSIMD2 & 0x00fefefe) + (dSIMD2 & 0x00fefefe)) >> 1)
                    + (sSIMD2 & dSIMD2 & 0x00010101)) | A_Dst;
      }, end);
    } else {
      sSIMD2 &= 0xFF00;
      sSIMD2 = sSIMD2 >> 8 | sSIMD2 << 8;
      DUFFS_LOOP_DOUBLE2(
        {
          dSIMD2 = *pixel;
          A_Dst = dSIMD2 & A_Mask;
          dSIMD1 = dSIMD2 & 0x00FF00FF;
          dSIMD1 += (sSIMD1 - dSIMD1) * A >> 8;
          dSIMD1 &= 0x00FF00FF;
          dSIMD2 &= 0xFF00;
          dSIMD2 += (((sSIMD2 << 8) & 0xFF00) - dSIMD2) * A >> 8;
          dSIMD2 &= 0xFF00;
          *pixel++ = dSIMD1 | dSIMD2 | A_Dst;
      }, {
          dSIMD1 = *pixel;
          A_Dst = dSIMD1 & A_Mask;
          dSIMD1 &= 0x00FF00FF;
          dSIMD1 += (sSIMD1 - dSIMD1) * A >> 8;
          dSIMD1 &= 0x00FF00FF;

          dSIMD2 = ((*pixel & 0xFF00) >> 8)| ((pixel[1] & 0xFF00) << 8);
          dSIMD2 += (sSIMD2 - dSIMD2) * A >> 8;
          dSIMD2 &= 0x00FF00FF;

          *pixel++ = dSIMD1 | ((dSIMD2 << 8) & 0xFF00) | A_Dst;

          dSIMD1 = *pixel;
          A_Dst = dSIMD1 & A_Mask;
          dSIMD1 &= 0x00FF00FF;
          dSIMD1 += (sSIMD1 - dSIMD1) * A >> 8;
          dSIMD1 &= 0x00FF00FF;

          *pixel++ = dSIMD1 | ((dSIMD2 >> 8) & 0xFF00) | A_Dst;
      }, end);
    }
  } else {
    /* Correct prect size */
    if (prect->x < 0) {
      prect->w += prect->x;
      prect->x = 0;
    } else if (prect->x >= surf->w - prect->w) {
      prect->w = surf->w - prect->x;
    }

    if (prect->y < 0) {
      prect->h += prect->y;
      prect->y = 0;
    } else if (prect->y >= surf->h - prect->h) {
      prect->h = surf->h - prect->y;
    }

    start = pixel = (Uint32 *) surf->pixels +
      (prect->y * (surf->pitch >> 2)) + prect->x;

    if (A == 128) { /* 50% A */
      y = prect->h;
      end = prect->w;
      while (y--) {
        DUFFS_LOOP4(
        {
          dSIMD2 = *pixel;
          A_Dst = dSIMD2 & A_Mask;
          *pixel++ = ((((sSIMD2 & 0x00fefefe) + (dSIMD2 & 0x00fefefe)) >> 1)
                      + (sSIMD2 & dSIMD2 & 0x00010101)) | A_Dst;
        }, end);
        pixel = start + (surf->pitch >> 2);
        start = pixel;
      }
    } else {
      y = prect->h;
      end = prect->w;

      sSIMD2 &= 0xFF00;
      sSIMD2 = sSIMD2 >> 8 | sSIMD2 << 8;

      while (y--) {
        DUFFS_LOOP_DOUBLE2(
        {
          dSIMD2 = *pixel;
          A_Dst = dSIMD2 & A_Mask;
          dSIMD1 = dSIMD2 & 0x00FF00FF;
          dSIMD1 += (sSIMD1 - dSIMD1) * A >> 8;
          dSIMD1 &= 0x00FF00FF;
          dSIMD2 &= 0xFF00;
          dSIMD2 += (((sSIMD2 << 8) & 0xFF00) - dSIMD2) * A >> 8;
          dSIMD2 &= 0xFF00;
          *pixel++ = dSIMD1 | dSIMD2 | A_Dst;
        }, {
          dSIMD1 = *pixel;
          A_Dst = dSIMD1 & A_Mask;
          dSIMD1 &= 0x00FF00FF;
          dSIMD1 += (sSIMD1 - dSIMD1) * A >> 8;
          dSIMD1 &= 0x00FF00FF;

          dSIMD2 = ((*pixel & 0xFF00) >> 8)| ((pixel[1] & 0xFF00) << 8);
          dSIMD2 += (sSIMD2 - dSIMD2) * A >> 8;
          dSIMD2 &= 0x00FF00FF;

          *pixel++ = dSIMD1 | ((dSIMD2 << 8) & 0xFF00) | A_Dst;

          dSIMD1 = *pixel;
          A_Dst = dSIMD1 & A_Mask;
          dSIMD1 &= 0x00FF00FF;
          dSIMD1 += (sSIMD1 - dSIMD1) * A >> 8;
          dSIMD1 &= 0x00FF00FF;

          *pixel++ = dSIMD1 | ((dSIMD2 >> 8) & 0xFF00) | A_Dst;
        }, end);

        pixel = start + (surf->pitch >> 2);
        start = pixel;
      } /* while */
    }
  }

  unlock_surf(surf);

  return 0;
}

/**********************************************************************//**
  Fill rectangle with color with alpha channel.
**************************************************************************/
int fill_rect_alpha(SDL_Surface *surf, SDL_Rect *prect,
                    SDL_Color *pcolor)
{
  if (prect && (prect->x < - prect->w || prect->x >= surf->w
                || prect->y < - prect->h || prect->y >= surf->h)) {
    return -2;
  }

  if (pcolor->a == 255) {
    return SDL_FillRect(surf, prect,
                        SDL_MapRGB(surf->format, pcolor->r, pcolor->g, pcolor->b));
  }

  if (!pcolor->a) {
    return -3;
  }

  return __FillRectAlpha8888_32bit(surf, prect, pcolor);
}

/**********************************************************************//**
  Make rectangle region sane. Return TRUE if result is sane.
**************************************************************************/
bool correct_rect_region(SDL_Rect *prect)
{
  int ww = prect->w, hh = prect->h;

  if (prect->x < 0) {
    ww += prect->x;
    prect->x = 0;
  }

  if (prect->y < 0) {
    hh += prect->y;
    prect->y = 0;
  }

  if (prect->x + ww > main_window_width()) {
    ww = main_window_width() - prect->x;
  }

  if (prect->y + hh > main_window_height()) {
    hh = main_window_height() - prect->y;
  }

  /* End Correction */

  if (ww <= 0 || hh <= 0) {
    return FALSE; /* surprise :) */
  } else {
    prect->w = ww;
    prect->h = hh;
  }

  return TRUE;
}

/**********************************************************************//**
  Return whether coordinates are in rectangle.
**************************************************************************/
bool is_in_rect_area(int x, int y, const SDL_Rect *rect)
{
  return ((x >= rect->x) && (x < rect->x + rect->w)
          && (y >= rect->y) && (y < rect->y + rect->h));
}

/* ===================================================================== */

/**********************************************************************//**
  Get visible rectangle from surface.
**************************************************************************/
void get_smaller_surface_rect(SDL_Surface *surf, SDL_Rect *rect)
{
  int w, h, x, y;
  Uint16 minX, maxX, minY, maxY;
  Uint32 colorkey;
  Uint32 mask;

  fc_assert(surf != NULL);
  fc_assert(rect != NULL);

  minX = surf->w;
  maxX = 0;
  minY = surf->h;
  maxY = 0;

  if (SDL_GetColorKey(surf, &colorkey) < 0) {
    /* Use alpha instead of colorkey */
    mask = surf->format->Amask;
    colorkey = 0;
  } else {
    mask = 0xffffffff;
  }

  lock_surf(surf);

  switch (surf->format->BytesPerPixel) {
  case 1:
  {
    Uint8 *pixel = (Uint8 *)surf->pixels;
    Uint8 *start = pixel;

    x = 0;
    y = 0;
    w = surf->w;
    h = surf->h;
    while (h--) {
      do {
        if (*pixel != colorkey) {
          if (minY > y) {
            minY = y;
          }

          if (minX > x) {
            minX = x;
          }
          break;
        }
        pixel++;
        x++;
      } while (--w > 0);
      w = surf->w;
      x = 0;
      y++;
      pixel = start + surf->pitch;
      start = pixel;
    }

    w = surf->w;
    h = surf->h;
    x = w - 1;
    y = h - 1;
    pixel = (Uint8 *)((Uint8 *)surf->pixels + (y * surf->pitch) + x);
    start = pixel;
    while (h--) {
      do {
        if (*pixel != colorkey) {
          if (maxY < y) {
            maxY = y;
          }

          if (maxX < x) {
            maxX = x;
          }
          break;
        }
        pixel--;
        x--;
      } while (--w > 0);
      w = surf->w;
      x = w - 1;
      y--;
      pixel = start - surf->pitch;
      start = pixel;
    }
    break;
  }
  case 2:
  {
    Uint16 *pixel = (Uint16 *)surf->pixels;
    Uint16 *start = pixel;

    x = 0;
    y = 0;
    w = surf->w;
    h = surf->h;
    while (h--) {
      do {
        if (*pixel != colorkey) {
          if (minY > y) {
            minY = y;
          }

          if (minX > x) {
            minX = x;
          }
          break;
        }
        pixel++;
        x++;
      } while (--w > 0);
      w = surf->w;
      x = 0;
      y++;
      pixel = start + surf->pitch / 2;
      start = pixel;
    }

    w = surf->w;
    h = surf->h;
    x = w - 1;
    y = h - 1;
    pixel = ((Uint16 *)surf->pixels + (y * surf->pitch / 2) + x);
    start = pixel;
    while (h--) {
      do {
        if (*pixel != colorkey) {
          if (maxY < y) {
            maxY = y;
          }

          if (maxX < x) {
            maxX = x;
          }
          break;
        }
        pixel--;
        x--;
      } while (--w > 0);
      w = surf->w;
      x = w - 1;
      y--;
      pixel = start - surf->pitch / 2;
      start = pixel;
    }
    break;
  }
  case 3:
  {
    Uint8 *pixel = (Uint8 *)surf->pixels;
    Uint8 *start = pixel;
    Uint32 color;

    x = 0;
    y = 0;
    w = surf->w;
    h = surf->h;
    while (h--) {
      do {
        if (is_bigendian()) {
          color = (pixel[0] << 16 | pixel[1] << 8 | pixel[2]);
        } else {
          color = (pixel[0] | pixel[1] << 8 | pixel[2] << 16);
        }
        if (color != colorkey) {
          if (minY > y) {
            minY = y;
          }

          if (minX > x) {
            minX = x;
          }
          break;
        }
        pixel += 3;
        x++;
      } while (--w > 0);
      w = surf->w;
      x = 0;
      y++;
      pixel = start + surf->pitch / 3;
      start = pixel;
    }

    w = surf->w;
    h = surf->h;
    x = w - 1;
    y = h - 1;
    pixel = (Uint8 *)((Uint8 *)surf->pixels + (y * surf->pitch) + x * 3);
    start = pixel;
    while (h--) {
      do {
        if (is_bigendian()) {
          color = (pixel[0] << 16 | pixel[1] << 8 | pixel[2]);
        } else {
          color = (pixel[0] | pixel[1] << 8 | pixel[2] << 16);
        }
        if (color != colorkey) {
          if (maxY < y) {
            maxY = y;
          }

          if (maxX < x) {
            maxX = x;
          }
          break;
        }
        pixel -= 3;
        x--;
      } while (--w > 0);
      w = surf->w;
      x = w - 1;
      y--;
      pixel = start - surf->pitch / 3;
      start = pixel;
    }
    break;
  }
  case 4:
  {
    Uint32 *pixel = (Uint32 *)surf->pixels;
    Uint32 *start = pixel;

    x = 0;
    y = 0;
    w = surf->w;
    h = surf->h;
    while (h--) {
      do {
        if (((*pixel) & mask) != colorkey) {
          if (minY > y) {
            minY = y;
          }

          if (minX > x) {
            minX = x;
          }
          break;
        }
        pixel++;
        x++;
      } while (--w > 0);
      w = surf->w;
      x = 0;
      y++;
      pixel = start + surf->pitch / 4;
      start = pixel;
    }

    w = surf->w;
    h = surf->h;
    x = w - 1;
    y = h - 1;
    pixel = ((Uint32 *)surf->pixels + (y * surf->pitch / 4) + x);
    start = pixel;
    while (h--) {
      do {
        if (((*pixel) & mask) != colorkey) {
          if (maxY < y) {
            maxY = y;
          }

          if (maxX < x) {
            maxX = x;
          }
          break;
        }
        pixel--;
        x--;
      } while (--w > 0);
      w = surf->w;
      x = w - 1;
      y--;
      pixel = start - surf->pitch / 4;
      start = pixel;
    }
    break;
  }
  }

  unlock_surf(surf);
  rect->x = minX;
  rect->y = minY;
  rect->w = maxX - minX + 1;
  rect->h = maxY - minY + 1;
}

/**********************************************************************//**
  Create new surface that is just visible part of source surface.
**************************************************************************/
SDL_Surface *crop_visible_part_from_surface(SDL_Surface *psrc)
{
  SDL_Rect src;

  get_smaller_surface_rect(psrc, &src);

  return crop_rect_from_surface(psrc, &src);
}

/**********************************************************************//**
  Scale surface.
**************************************************************************/
SDL_Surface *resize_surface(const SDL_Surface *psrc, Uint16 new_width,
                            Uint16 new_height, int smooth)
{
  if (psrc == NULL) {
    return NULL;
  }

  return zoomSurface((SDL_Surface*)psrc,
                     (double)new_width / psrc->w,
                     (double)new_height / psrc->h,
                     smooth);
}

/**********************************************************************//**
  Resize a surface to fit into a box with the dimensions 'new_width' and a
  'new_height'. If 'scale_up' is FALSE, a surface that already fits into
  the box will not be scaled up to the boundaries of the box.
  If 'absolute_dimensions' is TRUE, the function returns a surface with the
  dimensions of the box and the scaled/original surface centered in it.
**************************************************************************/
SDL_Surface *resize_surface_box(const SDL_Surface *psrc,
                                Uint16 new_width, Uint16 new_height,
                                int smooth, bool scale_up,
                                bool absolute_dimensions)
{
  SDL_Surface *tmp_surface, *result;

  if (psrc == NULL) {
    return NULL;
  }

  if (!(!scale_up && ((new_width >= psrc->w) && (new_height >= psrc->h)))) {
    if ((new_width - psrc->w) <= (new_height - psrc->h)) {
      /* horizontal limit */
      tmp_surface = zoomSurface((SDL_Surface*)psrc,
                               (double)new_width / psrc->w,
                               (double)new_width / psrc->w,
                               smooth);
    } else {
      /* vertical limit */
      tmp_surface = zoomSurface((SDL_Surface*)psrc,
                               (double)new_height / psrc->h,
                               (double)new_height / psrc->h,
                               smooth);
    }
  } else {
    tmp_surface = zoomSurface((SDL_Surface*)psrc,
                             1.0,
                             1.0,
                             smooth);
  }

  if (absolute_dimensions) {
    SDL_Rect area = {
      (new_width - tmp_surface->w) / 2,
      (new_height - tmp_surface->h) / 2,
      0, 0
    };

    result = create_surf(new_width, new_height, SDL_SWSURFACE);
    alphablit(tmp_surface, NULL, result, &area, 255);
    FREESURFACE(tmp_surface);
  } else {
    result = tmp_surface;
  }

  return result;
}

/**********************************************************************//**
  Return copy of the surface
**************************************************************************/
SDL_Surface *copy_surface(SDL_Surface *src)
{
  SDL_Surface *dst;

  dst = SDL_CreateRGBSurface(0, src->w, src->h, src->format->BitsPerPixel,
                             src->format->Rmask, src->format->Gmask,
                             src->format->Bmask, src->format->Amask);

  SDL_BlitSurface(src, NULL, dst, NULL);

  return dst;
}

/* ============ Freeciv game graphics function =========== */

/**********************************************************************//**
  Loading tileset of the specified type
**************************************************************************/
void tileset_type_set(enum ts_type type)
{
}

/**********************************************************************//**
  Create colored frame
**************************************************************************/
void create_frame(SDL_Surface *dest, Sint16 left, Sint16 top,
                  Sint16 width, Sint16 height,
                  SDL_Color *pcolor)
{
  struct color gsdl2_color = { .color = pcolor };
  struct sprite *vertical = create_sprite(1, height, &gsdl2_color);
  struct sprite *horizontal = create_sprite(width, 1, &gsdl2_color);
  SDL_Rect tmp, dst = { .x = left, .y = top, .w = 0, .h = 0 };

  tmp = dst;
  alphablit(vertical->psurface, NULL, dest, &tmp, 255);

  dst.x += width - 1;
  tmp = dst;
  alphablit(vertical->psurface, NULL, dest, &tmp, 255);

  dst.x = left;
  tmp = dst;
  alphablit(horizontal->psurface, NULL, dest, &tmp, 255);

  dst.y += height - 1;
  tmp = dst;
  alphablit(horizontal->psurface, NULL, dest, &tmp, 255);

  free_sprite(horizontal);
  free_sprite(vertical);
}

/**********************************************************************//**
  Create single colored line
**************************************************************************/
void create_line(SDL_Surface *dest, Sint16 x0, Sint16 y0, Sint16 x1, Sint16 y1,
                 SDL_Color *pcolor)
{
  int xl = x0 < x1 ? x0 : x1;
  int xr = x0 < x1 ? x1 : x0;
  int yt = y0 < y1 ? y0 : y1;
  int yb = y0 < y1 ? y1 : y0;
  int w = (xr - xl) + 1;
  int h = (yb - yt) + 1;
  struct sprite *spr;
  SDL_Rect dst = { xl, yt, w, h };
  SDL_Color *pcol;
  struct color gsdl2_color;
  int l = MAX((xr - xl) + 1, (yb - yt) + 1);
  int i;

  pcol = fc_malloc(sizeof(pcol));
  pcol->r = pcolor->r;
  pcol->g = pcolor->g;
  pcol->b = pcolor->b;
  pcol->a = 0; /* Fill with transparency */

  gsdl2_color.color = pcol;
  spr = create_sprite(w, h, &gsdl2_color);

  lock_surf(spr->psurface);

  /* Set off transparency from pixels belonging to the line */
  if ((x0 <= x1 && y0 <= y1)
      || (x1 <= x0 && y1 <= y0)) {
    for (i = 0; i < l; i++) {
      int cx = (xr - xl) * i / l;
      int cy = (yb - yt) * i / l;

      *((Uint32 *)spr->psurface->pixels + spr->psurface->w * cy + cx)
        |= (pcolor->a << spr->psurface->format->Ashift);
    }
  } else {
    for (i = 0; i < l; i++) {
      int cx = (xr - xl) * i / l;
      int cy = yb - yt - (yb - yt) * i / l;

      *((Uint32 *)spr->psurface->pixels + spr->psurface->w * cy + cx)
        |= (pcolor->a << spr->psurface->format->Ashift);
    }
  }

  unlock_surf(spr->psurface);

  alphablit(spr->psurface, NULL, dest, &dst, 255);

  free_sprite(spr);
  free(pcol);
}
