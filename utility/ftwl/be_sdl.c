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

static SDL_Surface *screen;
static int other_fd = -1;

/* SDL interprets each pixel as a 32-bit number, so our masks must depend
 * on the endianness (byte order) of the machine */
#if SDL_BYTEORDER == SDL_BIG_ENDIAN
static Uint32 rmask = 0xff000000;
static Uint32 gmask = 0x00ff0000;
static Uint32 bmask = 0x0000ff00;
static Uint32 amask = 0x000000ff;
#else
static Uint32 rmask = 0x000000ff;
static Uint32 gmask = 0x0000ff00;
static Uint32 bmask = 0x00ff0000;
static Uint32 amask = 0xff000000;
#endif

/*************************************************************************
  Initialize video mode and SDL.
*************************************************************************/
void be_init(const struct ct_size *screen_size, bool fullscreen)
{
  Uint32 flags = SDL_SWSURFACE | (fullscreen ? SDL_FULLSCREEN : 0)
                 | SDL_ANYFORMAT;

  char device[20];

  /* auto center new windows in X enviroment */
  putenv((char *) "SDL_VIDEO_CENTERED=yes");

  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_NOPARACHUTE) < 0) {
    freelog(LOG_FATAL, _("Unable to initialize SDL library: %s"),
	    SDL_GetError());
    exit(EXIT_FAILURE);
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
      SDL_SetVideoMode(screen_size->width, screen_size->height, 32, flags);
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
  SDL_EnableKeyRepeat(SDL_DEFAULT_REPEAT_DELAY, SDL_DEFAULT_REPEAT_INTERVAL);
#if 0
  SDL_EventState(SDL_KEYUP, SDL_IGNORE);
  SDL_EventState(SDL_ACTIVEEVENT, SDL_IGNORE);
#endif
}

/*************************************************************************
  ...
*************************************************************************/
static inline bool copy_event(struct be_event *event, SDL_Event *sdl_event)
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
      } else if (key == SDLK_DOWN) {
	event->key.type = BE_KEY_DOWN;
      } else if (key == SDLK_UP) {
	event->key.type = BE_KEY_UP;
      } else if (key == SDLK_ESCAPE) {
	event->key.type = BE_KEY_ESCAPE;
      } else {
	Uint16 unicode = sdl_event->key.keysym.unicode;

	if (unicode == 0) {
          freelog(LOG_NORMAL, "unicode == 0");
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
    exit(EXIT_SUCCESS);

  default:
    // freelog(LOG_NORMAL, "ignored event %d\n", sdl_event->type);
    return FALSE;
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
    SDL_Event sdl_event;

    event->type = BE_NO_EVENT;
    event->key.key = 0;
    event->key.type = NUM_BE_KEYS;

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
    while (SDL_PollEvent(&sdl_event)) {
      if (copy_event(event, &sdl_event)) {
#if 0
        /* For debugging sdl slowness - remove me when done */
        if (event->type == BE_KEY_PRESSED) {
          printf("sending event %d:%d\n", event->key.key, event->key.type);
        }
#endif
        return;
      }
    }

    /* Wait 10ms and do polling */
    SDL_Delay(10);
    assert(event->type == BE_NO_EVENT);
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

/*************************************************************************
  Copy our osda to the screen.  No alpha-blending here.
*************************************************************************/
void be_copy_osda_to_screen(struct osda *src)
{
  SDL_Surface *buf;

  buf = SDL_CreateRGBSurfaceFrom(src->image->data, src->image->width,
                                 src->image->height, 32, src->image->pitch,
                                 rmask, gmask, bmask, amask);
  SDL_BlitSurface(buf, NULL, screen, NULL);
  SDL_Flip(screen);
  SDL_FreeSurface(buf);
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
