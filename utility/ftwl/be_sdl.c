/**********************************************************************
 Freeciv - Copyright (C) 2004 - The Freeciv Project
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
#include <errno.h>
#include <stdio.h>

#include <SDL/SDL.h>

#include "back_end.h"
#include "be_common_24.h"

#include "shared.h"

#include "fcintl.h"
#include "log.h"
#include "mem.h"

// fixme
#include "timing.h"
#include "widget.h"

static SDL_Surface *screen;
static int other_fd = -1;

#define P IMAGE_GET_ADDRESS

/*************************************************************************
  ...
*************************************************************************/
void be_init(const struct ct_size *screen_size, bool fullscreen)
{
  Uint32 flags = SDL_HWSURFACE | (fullscreen ? SDL_FULLSCREEN : 0);

  char device[20];

  /* auto center new windows in X enviroment */
  putenv((char *) "SDL_VIDEO_CENTERED=yes");

  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_NOPARACHUTE) < 0) {
    freelog(LOG_FATAL, _("Unable to initialize SDL library: %s"),
	    SDL_GetError());
    exit(1);
  }
  atexit(SDL_Quit);

  freelog(LOG_NORMAL, "Using Video Output: %s",
	  SDL_VideoDriverName(device, sizeof(device)));
  {
    const SDL_VideoInfo *info = SDL_GetVideoInfo();
    freelog(LOG_NORMAL, "Video memory of driver: %dkb", info->video_mem);
    freelog(LOG_NORMAL, "Preferred depth: %d bits per pixel",
	    info->vfmt->BitsPerPixel);
  }

#if 0
  {
    SDL_Rect **modes;
    int i;

    modes = SDL_ListModes(NULL, flags);
    if (modes == (SDL_Rect **) 0) {
      printf("No modes available!\n");
      exit(-1);
    }
    if (modes == (SDL_Rect **) - 1) {
      printf("All resolutions available.\n");
    } else {
      /* Print valid modes */
      printf("Available Modes\n");
      for (i = 0; modes[i]; ++i)
	printf("  %d x %d\n", modes[i]->w, modes[i]->h);
    }
  }
#endif

  screen =
      SDL_SetVideoMode(screen_size->width, screen_size->height, 0, flags);
  if (screen == NULL) {
    freelog(LOG_FATAL, _("Can't set video mode: %s"), SDL_GetError());
    exit(1);
  }

  freelog(LOG_NORMAL, "Got a screen with size (%dx%d) and %d bits per pixel",
	  screen->w, screen->h, screen->format->BitsPerPixel);
  freelog(LOG_NORMAL, "  format: red=0x%x green=0x%x blue=0x%x mask=0x%x",
	  screen->format->Rmask, screen->format->Gmask,
	  screen->format->Bmask, screen->format->Amask);
  freelog(LOG_NORMAL, "  format: bits-per-pixel=%d bytes-per-pixel=%d",
	  screen->format->BitsPerPixel, screen->format->BytesPerPixel);
  SDL_EnableUNICODE(1);
}

/*************************************************************************
  ...
*************************************************************************/
static bool copy_event(struct be_event *event, SDL_Event * sdl_event)
{
  switch (sdl_event->type) {
  case SDL_VIDEOEXPOSE:
    event->type = BE_EXPOSE;
    break;

  case SDL_MOUSEMOTION:
    event->type = BE_MOUSE_MOTION;
    event->position.x = sdl_event->motion.x;
    event->position.y = sdl_event->motion.y;

    break;
  case SDL_MOUSEBUTTONUP:
  case SDL_MOUSEBUTTONDOWN:
    event->type =
	sdl_event->type ==
	SDL_MOUSEBUTTONDOWN ? BE_MOUSE_PRESSED : BE_MOUSE_RELEASED;
    event->position.x = sdl_event->button.x;
    event->position.y = sdl_event->button.y;
    switch (sdl_event->button.button) {
    case SDL_BUTTON_LEFT:
      event->button = BE_MB_LEFT;
      break;
    case SDL_BUTTON_MIDDLE:
      event->button = BE_MB_MIDDLE;
      break;
    case SDL_BUTTON_RIGHT:
      event->button = BE_MB_RIGHT;
      break;
    default:
      assert(0);
    }
    break;
  case SDL_KEYDOWN:
    {
      SDLKey key = sdl_event->key.keysym.sym;

      event->key.alt = sdl_event->key.keysym.mod & KMOD_ALT;
      event->key.control = sdl_event->key.keysym.mod & KMOD_CTRL;
      event->key.shift = sdl_event->key.keysym.mod & KMOD_SHIFT;

      if (key == SDLK_BACKSPACE) {
	event->key.type = BE_KEY_BACKSPACE;
      } else if (key == SDLK_RETURN) {
	event->key.type = BE_KEY_RETURN;
      } else if (key == SDLK_KP_ENTER) {
	event->key.type = BE_KEY_ENTER;
      } else if (key == SDLK_DELETE) {
	event->key.type = BE_KEY_DELETE;
      } else if (key == SDLK_LEFT) {
	event->key.type = BE_KEY_LEFT;
      } else if (key == SDLK_RIGHT) {
	event->key.type = BE_KEY_RIGHT;
      } else if (key == SDLK_ESCAPE) {
	event->key.type = BE_KEY_ESCAPE;
      } else {
	Uint16 unicode = sdl_event->key.keysym.unicode;

	if (unicode == 0) {
	  return FALSE;
	}
	if ((unicode & 0xFF80) != 0) {
	  printf("An International Character '%c' %d.\n", (char) unicode,
		 (char) unicode);
	  return FALSE;
	}

	event->key.type = BE_KEY_NORMAL;
	event->key.key = unicode & 0x7F;
	event->key.shift = FALSE;
      }
      event->type = BE_KEY_PRESSED;
      SDL_GetMouseState(&event->position.x, &event->position.y);
    }
    break;
  case SDL_QUIT:
    exit(0);

  default:
    //printf("got event %d\n", sdl_event->type);
    return false;
  }
  return TRUE;
}

/*************************************************************************
  ...
*************************************************************************/
void be_next_event(struct be_event *event, struct timeval *timeout)
{
  /*
   * There are 3 event sources
   * 1) data on the network socket
   * 2) timeout
   * 3) SDL keyboard and mouse events
   */
  Uint32 timeout_end =
      SDL_GetTicks() + timeout->tv_sec * 1000 + timeout->tv_usec / 1000;

  for (;;) {
    /* Test the network socket. */
    if (other_fd != -1) {
      fd_set readfds, exceptfds;
      int ret;
      struct timeval zero_timeout;

      /* No event available: block on input socket until one is */
      FD_ZERO(&readfds);
      FD_ZERO(&exceptfds);

      FD_SET(other_fd, &readfds);
      FD_SET(other_fd, &exceptfds);

      zero_timeout.tv_sec = 0;
      zero_timeout.tv_usec = 0;

      ret = select(other_fd + 1, &readfds, NULL, &exceptfds, &zero_timeout);
      if (ret > 0 && (FD_ISSET(other_fd, &readfds) ||
		      FD_ISSET(other_fd, &exceptfds))) {
	event->type = BE_DATA_OTHER_FD;
	event->socket = other_fd;
	return;
      }
    }

    /* The timeout */
    if (SDL_GetTicks() >= timeout_end) {
      event->type = BE_TIMEOUT;
      return;
    }

    /* Normal SDL events */
    {
      SDL_Event sdl_event;
      while (SDL_PollEvent(&sdl_event)) {
	if (copy_event(event, &sdl_event)) {
	  return;
	}
      }
    }

    /* Wait 10ms and do polling */
    SDL_Delay(10);
  }
}

/*************************************************************************
  ...
*************************************************************************/
void be_add_net_input(int sock)
{
  other_fd = sock;
}

/*************************************************************************
  ...
*************************************************************************/
void be_remove_net_input(void)
{
  other_fd = -1;
}

#define COMP_565_RED(x)		((((x)>>3)&0x1f)<<11)
#define COMP_565_GREEN(x)	((((x)>>2)&0x3f)<< 5)
#define COMP_565_BLUE(x)	((((x)>>3)&0x1f)<< 0)

/*************************************************************************
  ...
*************************************************************************/
static void fill_surface_from_image_565(SDL_Surface * surface,
					const struct image *image)
{
  int x, y;
  unsigned short *pdest = (unsigned short *) surface->pixels;
  int extra_per_line = surface->pitch / 2 - image->width;

  for (y = 0; y < image->height; y++) {
    for (x = 0; x < image->width; x++) {
      unsigned char *psrc = P(image, x, y);
      unsigned short new_value =
	  (COMP_565_RED(psrc[0]) | COMP_565_GREEN(psrc[1]) |
	   COMP_565_BLUE(psrc[2]));
      *pdest = new_value;
      pdest++;
    }
    pdest += extra_per_line;
  }
}

#define COMP_555_RED(x)		((((x)>>3)&0x1f)<<10)
#define COMP_555_GREEN(x)	((((x)>>3)&0x1f)<< 5)
#define COMP_555_BLUE(x)	((((x)>>3)&0x1f)<< 0)

/*************************************************************************
  ...
*************************************************************************/
static void fill_surface_from_image_555(SDL_Surface * surface,
					const struct image *image)
{
  int x, y;
  unsigned short *pdest = (unsigned short *) surface->pixels;
  int extra_per_line = surface->pitch / 2 - image->width;

  for (y = 0; y < image->height; y++) {
    for (x = 0; x < image->width; x++) {
      unsigned char *psrc = P(image, x, y);
      unsigned short new_value =
	  (COMP_555_RED(psrc[0]) | COMP_555_GREEN(psrc[1]) |
	   COMP_555_BLUE(psrc[2]));
      *pdest = new_value;
      pdest++;
    }
    pdest += extra_per_line;
  }
}

/*************************************************************************
  ...
*************************************************************************/
static void fill_surface_from_image_8888(SDL_Surface * surface,
					const struct image *image)
{
  int x, y;
  unsigned char *pdest = (unsigned char *) surface->pixels;
  int extra_per_line = surface->pitch - image->width*4;

  for (y = 0; y < image->height; y++) {
    for (x = 0; x < image->width; x++) {
      unsigned char *psrc = P(image, x, y);

      pdest[0] = psrc[2];
      pdest[1] = psrc[1];
      pdest[2] = psrc[0];

      pdest += 4;
    }
    pdest += extra_per_line;
  }
}

/*************************************************************************
  ...
*************************************************************************/
void be_copy_osda_to_screen(struct osda *src)
{
  assert(screen->w == src->image->width && screen->h == src->image->height);

  SDL_LockSurface(screen);

  if (screen->format->Rmask == 0xf800 && screen->format->Gmask == 0x7e0 &&
      screen->format->Bmask == 0x1f && screen->format->Amask == 0 &&
      screen->format->BitsPerPixel == 16
      && screen->format->BytesPerPixel == 2) {
    fill_surface_from_image_565(screen, src->image);
  }else if (screen->format->Rmask == 0x7c00 && screen->format->Gmask == 0x3e0 &&
      screen->format->Bmask == 0x1f && screen->format->Amask == 0 &&
      screen->format->BitsPerPixel == 16
      && screen->format->BytesPerPixel == 2) {
    fill_surface_from_image_555(screen, src->image);
  } else if (screen->format->Rmask == 0xff0000
	     && screen->format->Gmask == 0xff00
	     && screen->format->Bmask == 0xff
	     && screen->format->Amask == 0
	     && screen->format->BitsPerPixel == 32
	     && screen->format->BytesPerPixel == 4) {
    fill_surface_from_image_8888(screen, src->image);
  } else {
    fprintf(stderr, "ERROR: unknown screen format: red=0x%x, "
	    "green=0x%x, blue=0x%x, alpha=0x%x bits-per-pixel=%d "
	    "bytes-per-pixel=%d\n",
	    screen->format->Rmask, screen->format->Gmask,
	    screen->format->Bmask, screen->format->Amask,
	    screen->format->BitsPerPixel, screen->format->BytesPerPixel);
    assert(0);
  }

  SDL_UnlockSurface(screen);
  SDL_UpdateRect(screen, 0, 0, 0, 0);
}

/*************************************************************************
  ...
*************************************************************************/
void be_screen_get_size(struct ct_size *size)
{
  size->width = screen->w;
  size->height = screen->h;
}

/*************************************************************************
  ...
*************************************************************************/
bool be_supports_fullscreen(void)
{
    return TRUE;
}
