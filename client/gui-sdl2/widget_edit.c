/***********************************************************************
 Freeciv - Copyright (C) 2006 - The Freeciv Project
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

/* SDL2 */
#ifdef SDL2_PLAIN_INCLUDE
#include <SDL.h>
#else  /* SDL2_PLAIN_INCLUDE */
#include <SDL2/SDL.h>
#endif /* SDL2_PLAIN_INCLUDE */

/* gui-sdl2 */
#include "colors.h"
#include "graphics.h"
#include "gui_id.h"
#include "gui_tilespec.h"
#include "mapview.h"
#include "themespec.h"
#include "utf8string.h"
#include "widget.h"
#include "widget_p.h"

struct Utf8Char {
  struct Utf8Char *next;
  struct Utf8Char *prev;
  int bytes;
  char chr[7];
  SDL_Surface *pTsurf;
};

struct text_edit {
  struct Utf8Char *begin_text_chain;
  struct Utf8Char *end_text_chain;
  struct Utf8Char *input_chain;
  SDL_Surface *pBg;
  struct widget *pwidget;
  int ChainLen;
  int Start_X;
  int Truelength;
  int InputChain_X;
};

static size_t chainlen(const struct Utf8Char *pChain);
static void del_chain(struct Utf8Char *pChain);
static struct Utf8Char *text2chain(const char *text_in);
static char *chain2text(const struct Utf8Char *in_chain, size_t len, size_t *size);

static int (*baseclass_redraw)(struct widget *pwidget);

/**********************************************************************//**
  Draw the text being edited.
**************************************************************************/
static int redraw_edit_chain(struct text_edit *edt)
{
  struct Utf8Char *input_chain_tmp;
  SDL_Rect dest, dest_copy = {0, 0, 0, 0};
  int start_mod_x;
  int ret;

  dest_copy.x = edt->pwidget->size.x;
  dest_copy.y = edt->pwidget->size.y;

  ret = (*baseclass_redraw)(edt->pwidget);
  if (ret != 0) {
    return ret;
  }

  /* blit theme */
  dest = dest_copy;

  alphablit(edt->pBg, NULL, edt->pwidget->dst->surface, &dest, 255);

  /* set start parameters */
  input_chain_tmp = edt->begin_text_chain;
  start_mod_x = 0;

  dest_copy.y += (edt->pBg->h - input_chain_tmp->pTsurf->h) / 2;
  dest_copy.x += edt->Start_X;

  /* draw loop */
  while (input_chain_tmp) {
    dest_copy.x += start_mod_x;
    /* check if we draw inside of edit rect */
    if (dest_copy.x > edt->pwidget->size.x + edt->pBg->w - 4) {
      break;
    }

    if (dest_copy.x > edt->pwidget->size.x) {
      dest = dest_copy;
      alphablit(input_chain_tmp->pTsurf, NULL, edt->pwidget->dst->surface, &dest,
                255);
    }

    start_mod_x = input_chain_tmp->pTsurf->w;

    /* draw cursor */
    if (input_chain_tmp == edt->input_chain) {
      dest = dest_copy;

      create_line(edt->pwidget->dst->surface,
                  dest.x - 1, dest.y + (edt->pBg->h / 8),
                  dest.x - 1, dest.y + edt->pBg->h - (edt->pBg->h / 4),
                  get_theme_color(COLOR_THEME_EDITFIELD_CARET));
      /* save active element position */
      edt->InputChain_X = dest_copy.x;
    }

    input_chain_tmp = input_chain_tmp->next;
  } /* while - draw loop */

  widget_flush(edt->pwidget);

  return 0;
}

/**********************************************************************//**
  Create Edit Field surface ( with Text) and blit them to Main.screen,
  on position 'edit_widget->size.x , edit_widget->size.y'

  Graphic is taken from 'edit_widget->theme'
  Text is taken from	'edit_widget->string_utf8'

  if flag 'FW_DRAW_THEME_TRANSPARENT' is set theme will be blit
  transparent ( Alpha = 128 )

  function return Hight of created surfaces or (-1) if theme surface can't
  be created.
**************************************************************************/
static int redraw_edit(struct widget *edit_widget)
{
  if (get_wstate(edit_widget) == FC_WS_PRESSED) {
    return redraw_edit_chain((struct text_edit *)edit_widget->data.ptr);
  } else {
    int ret;
    SDL_Rect rDest = {edit_widget->size.x, edit_widget->size.y, 0, 0};
    SDL_Surface *pedit = NULL;
    SDL_Surface *text;

    ret = (*baseclass_redraw)(edit_widget);
    if (ret != 0) {
      return ret;
    }

    if (edit_widget->string_utf8->text != NULL
        && get_wflags(edit_widget) & WF_PASSWD_EDIT) {
      char *backup = edit_widget->string_utf8->text;
      size_t len = strlen(backup) + 1;
      char *cbuf = fc_calloc(1, len);

      memset(cbuf, '*', len - 1);
      cbuf[len - 1] = '\0';
      edit_widget->string_utf8->text = cbuf;
      text = create_text_surf_from_utf8(edit_widget->string_utf8);
      FC_FREE(cbuf);
      edit_widget->string_utf8->text = backup;
    } else {
      text = create_text_surf_from_utf8(edit_widget->string_utf8);
    }

    pedit = create_bcgnd_surf(edit_widget->theme, get_wstate(edit_widget),
                              edit_widget->size.w, edit_widget->size.h);

    if (!pedit) {
      return -1;
    }

    /* blit theme */
    alphablit(pedit, NULL, edit_widget->dst->surface, &rDest, 255);

    /* set position and blit text */
    if (text) {
      rDest.y += (pedit->h - text->h) / 2;
      /* blit centred text to botton */
      if (edit_widget->string_utf8->style & SF_CENTER) {
        rDest.x += (pedit->w - text->w) / 2;
      } else {
        if (edit_widget->string_utf8->style & SF_CENTER_RIGHT) {
          rDest.x += pedit->w - text->w - adj_size(5);
        } else {
          rDest.x += adj_size(5); /* center left */
        }
      }

      alphablit(text, NULL, edit_widget->dst->surface, &rDest, 255);
    }
    /* text */
    ret = pedit->h;

    /* Free memory */
    FREESURFACE(text);
    FREESURFACE(pedit);

    return ret;
  }

  return 0;
}

/**********************************************************************//**
  Return length of Utf8Char chain.
  WARRNING: if struct Utf8Char has 1 member and Utf8Char->chr == 0 then
  this function return 1 ( not 0 like in strlen )
**************************************************************************/
static size_t chainlen(const struct Utf8Char *pChain)
{
  size_t length = 0;

  if (pChain) {
    while (TRUE) {
      length++;
      if (pChain->next == NULL) {
	break;
      }
      pChain = pChain->next;
    }
  }

  return length;
}

/**********************************************************************//**
  Delete Utf8Char structure.
**************************************************************************/
static void del_chain(struct Utf8Char *pChain)
{
  int i, len = 0;

  if (!pChain) {
    return;
  }

  len = chainlen(pChain);

  if (len > 1) {
    pChain = pChain->next;
    for (i = 0; i < len - 1; i++) {
      FREESURFACE(pChain->prev->pTsurf);
      FC_FREE(pChain->prev);
      pChain = pChain->next;
    }
  }

  FC_FREE(pChain);
}

/**********************************************************************//**
  Convert utf8 string to Utf8Char structure.
  Memory alocation -> after all use need call del_chain(...) !
**************************************************************************/
static struct Utf8Char *text2chain(const char *text_in)
{
  int i, len;
  struct Utf8Char *out_chain = NULL;
  struct Utf8Char *chr_tmp = NULL;
  int j;

  if (text_in == NULL) {
    return NULL;
  }

  len = strlen(text_in);

  if (len == 0) {
    return NULL;
  }

  out_chain = fc_calloc(1, sizeof(struct Utf8Char));
  out_chain->chr[0] = text_in[0];
  for (j = 1; (text_in[j] & (128 + 64)) == 128; j++) {
    out_chain->chr[j] = text_in[j];
  }
  out_chain->bytes = j;
  chr_tmp = out_chain;

  for (i = 1; i < len; i += j) {
    chr_tmp->next = fc_calloc(1, sizeof(struct Utf8Char));
    chr_tmp->next->chr[0] = text_in[i];
    for (j = 1; (text_in[i + j] & (128 + 64)) == 128; j++) {
      chr_tmp->next->chr[j] = text_in[i + j];
    }
    chr_tmp->next->bytes  = j;
    chr_tmp->next->prev = chr_tmp;
    chr_tmp = chr_tmp->next;
  }

  return out_chain;
}

/**********************************************************************//**
  Convert Utf8Char structure to chars
  WARNING: Do not free Utf8Char structure but allocates new char array.
**************************************************************************/
static char *chain2text(const struct Utf8Char *in_chain, size_t len,
                        size_t *size)
{
  int i;
  char *pOutText = NULL;
  int oi = 0;
  int total_size = 0;

  if (!(len && in_chain)) {
    return pOutText;
  }

  pOutText = fc_calloc(8, len + 1);
  for (i = 0; i < len; i++) {
    int j;

    for (j = 0; j < in_chain->bytes && i < len; j++) {
      pOutText[oi++] = in_chain->chr[j];
    }

    total_size += in_chain->bytes;
    in_chain = in_chain->next;
  }

  *size = total_size;

  return pOutText;
}

/* =================================================== */

/**********************************************************************//**
  Create ( malloc ) Edit Widget structure.

  Edit Theme graphic is taken from current_theme->Edit surface;
  Text is taken from 'pstr'.

  'length' parameter determinate width of Edit rect.

  This function determinate future size of Edit ( width, height ) and
  save this in: pwidget->size rectangle ( SDL_Rect )

  function return pointer to allocated Edit Widget.
**************************************************************************/
struct widget *create_edit(SDL_Surface *background, struct gui_layer *pdest,
                           utf8_str *pstr, int length, Uint32 flags)
{
  SDL_Rect buf = {0, 0, 0, 0};
  struct widget *pedit = widget_new();

  pedit->theme = current_theme->edit;
  pedit->theme2 = background; /* FIXME: make somewhere use of it */
  pedit->string_utf8 = pstr;
  set_wflag(pedit, (WF_FREE_STRING | WF_FREE_GFX | flags));
  set_wstate(pedit, FC_WS_DISABLED);
  set_wtype(pedit, WT_EDIT);
  pedit->mod = KMOD_NONE;

  baseclass_redraw = pedit->redraw;
  pedit->redraw = redraw_edit;

  if (pstr != NULL) {
    pedit->string_utf8->style |= SF_CENTER;
    utf8_str_size(pstr, &buf);
    buf.h += adj_size(4);
  }

  length = MAX(length, buf.w + adj_size(10));

  correct_size_bcgnd_surf(current_theme->edit, &length, &buf.h);

  pedit->size.w = length;
  pedit->size.h = buf.h;

  if (pdest) {
    pedit->dst = pdest;
  } else {
    pedit->dst = add_gui_layer(pedit->size.w, pedit->size.h);
  }

  return pedit;
}

/**********************************************************************//**
  set new x, y position and redraw edit.
**************************************************************************/
int draw_edit(struct widget *pedit, Sint16 start_x, Sint16 start_y)
{
  pedit->size.x = start_x;
  pedit->size.y = start_y;

  if (get_wflags(pedit) & WF_RESTORE_BACKGROUND) {
    refresh_widget_background(pedit);
  }

  return redraw_edit(pedit);
}

/**********************************************************************//**
  This functions are pure madness :)
  Create Edit Field surface ( with Text) and blit them to Main.screen,
  on position 'edit_widget->size.x , edit_widget->size.y'

  Main role of this functions are been text input to GUI.
  This code allow you to add, del unichar from unistring.

  Graphic is taken from 'edit_widget->theme'
  OldText is taken from	'edit_widget->string_utf8'

  NewText is returned to 'edit_widget->string_utf8' ( after free OldText )

  if flag 'FW_DRAW_THEME_TRANSPARENT' is set theme will be blit
  transparent ( Alpha = 128 )

  NOTE: This functions can return NULL in 'edit_widget->string_utf8->text' but
        never free 'edit_widget->string_utf8' struct.
**************************************************************************/
static Uint16 edit_key_down(SDL_Keysym key, void *data)
{
  struct text_edit *edt = (struct text_edit *)data;
  struct Utf8Char *input_chain_tmp;
  bool Redraw = FALSE;

  /* find which key is pressed */
  switch (key.sym) {
    case SDLK_ESCAPE:
      /* exit from loop without changes */
      return ED_ESC;
    case SDLK_RETURN:
    case SDLK_KP_ENTER:
      /* exit from loop */
      return ED_RETURN;
      /*
    case SDLK_KP6:
      if (key.mod & KMOD_NUM) {
	goto INPUT;
      }
      */
    case SDLK_RIGHT:
    {
      /* move cursor right */
      if (edt->input_chain->next) {
        if (edt->InputChain_X >= (edt->pwidget->size.x + edt->pBg->w - adj_size(10))) {
          edt->Start_X -= edt->input_chain->pTsurf->w -
            (edt->pwidget->size.x + edt->pBg->w - adj_size(5) - edt->InputChain_X);
        }

	edt->input_chain = edt->input_chain->next;
	Redraw = TRUE;
      }
    }
    break;
    /*
    case SDLK_KP4:
      if (key.mod & KMOD_NUM) {
        goto INPUT;
      }
    */
    case SDLK_LEFT:
    {
      /* move cursor left */
      if (edt->input_chain->prev) {
        edt->input_chain = edt->input_chain->prev;
        if ((edt->InputChain_X <=
             (edt->pwidget->size.x + adj_size(9))) && (edt->Start_X != adj_size(5))) {
          if (edt->InputChain_X != (edt->pwidget->size.x + adj_size(5))) {
            edt->Start_X += (edt->pwidget->size.x - edt->InputChain_X + adj_size(5));
          }

          edt->Start_X += (edt->input_chain->pTsurf->w);
        }
        Redraw = TRUE;
      }
    }
    break;
    /*
    case SDLK_KP7:
      if (key.mod & KMOD_NUM) {
	goto INPUT;
      }
    */
    case SDLK_HOME:
    {
      /* move cursor to begin of chain (and edit field) */
      edt->input_chain = edt->begin_text_chain;
      Redraw = TRUE;
      edt->Start_X = adj_size(5);
    }
    break;
    /*
    case SDLK_KP1:
      if (key.mod & KMOD_NUM) {
	goto INPUT;
      }
    */
    case SDLK_END:
    {
      /* move cursor to end of chain (and edit field) */
      edt->input_chain = edt->end_text_chain;
      Redraw = TRUE;

      if (edt->pwidget->size.w - edt->Truelength < 0) {
        edt->Start_X = edt->pwidget->size.w - edt->Truelength - adj_size(5);
      }
    }
    break;
    case SDLK_BACKSPACE:
    {
      /* del element of chain (and move cursor left) */
      if (edt->input_chain->prev) {
        if ((edt->InputChain_X <=
             (edt->pwidget->size.x + adj_size(9))) && (edt->Start_X != adj_size(5))) {
          if (edt->InputChain_X != (edt->pwidget->size.x + adj_size(5))) {
            edt->Start_X += (edt->pwidget->size.x - edt->InputChain_X + adj_size(5));
          }
          edt->Start_X += (edt->input_chain->prev->pTsurf->w);
        }

        if (edt->input_chain->prev->prev) {
          edt->input_chain->prev->prev->next = edt->input_chain;
          input_chain_tmp = edt->input_chain->prev->prev;
          edt->Truelength -= edt->input_chain->prev->pTsurf->w;
          FREESURFACE(edt->input_chain->prev->pTsurf);
          FC_FREE(edt->input_chain->prev);
          edt->input_chain->prev = input_chain_tmp;
        } else {
          edt->Truelength -= edt->input_chain->prev->pTsurf->w;
          FREESURFACE(edt->input_chain->prev->pTsurf);
          FC_FREE(edt->input_chain->prev);
          edt->begin_text_chain = edt->input_chain;
        }

        edt->ChainLen--;
        Redraw = TRUE;
      }
    }
    break;
    /*
    case SDLK_KP_PERIOD:
      if (key.mod & KMOD_NUM) {
	goto INPUT;
      }
    */
    case SDLK_DELETE:
    {
      /* del element of chain */
      if (edt->input_chain->next && edt->input_chain->prev) {
        edt->input_chain->prev->next = edt->input_chain->next;
        edt->input_chain->next->prev = edt->input_chain->prev;
        input_chain_tmp = edt->input_chain->next;
        edt->Truelength -= edt->input_chain->pTsurf->w;
        FREESURFACE(edt->input_chain->pTsurf);
        FC_FREE(edt->input_chain);
        edt->input_chain = input_chain_tmp;
        edt->ChainLen--;
        Redraw = TRUE;
      }

      if (edt->input_chain->next && !edt->input_chain->prev) {
        edt->input_chain = edt->input_chain->next;
        edt->Truelength -= edt->input_chain->prev->pTsurf->w;
        FREESURFACE(edt->input_chain->prev->pTsurf);
        FC_FREE(edt->input_chain->prev);
        edt->begin_text_chain = edt->input_chain;
        edt->ChainLen--;
        Redraw = TRUE;
      }
    }
    break;
  default:
    break;
  } /* key pressed switch */

  if (Redraw) {
    redraw_edit_chain(edt);
  }

  return ID_ERROR;
}

/**********************************************************************//**
  Handle textinput strings coming to the edit widget
**************************************************************************/
static Uint16 edit_textinput(char *text, void *data)
{
  struct text_edit *edt = (struct text_edit *)data;
  struct Utf8Char *input_chain_tmp;
  int i;

  for (i = 0; text[i] != '\0';) {
    int charlen = 1;
    unsigned char leading = text[i++];
    int sum = 128 + 64;
    int addition = 32;

    /* add new element of chain (and move cursor right) */
    if (edt->input_chain != edt->begin_text_chain) {
      input_chain_tmp = edt->input_chain->prev;
      edt->input_chain->prev = fc_calloc(1, sizeof(struct Utf8Char));
      edt->input_chain->prev->next = edt->input_chain;
      edt->input_chain->prev->prev = input_chain_tmp;
      input_chain_tmp->next = edt->input_chain->prev;
    } else {
      edt->input_chain->prev = fc_calloc(1, sizeof(struct Utf8Char));
      edt->input_chain->prev->next = edt->input_chain;
      edt->begin_text_chain = edt->input_chain->prev;
    }

    edt->input_chain->prev->chr[0] = leading;
    /* UTF-8 multibyte handling */
    while (leading >= sum) {
      edt->input_chain->prev->chr[charlen++] = text[i++];
      sum += addition;
      addition /= 2;
    }
    edt->input_chain->prev->chr[charlen] = '\0';
    edt->input_chain->prev->bytes = charlen;

    if (get_wflags(edt->pwidget) & WF_PASSWD_EDIT) {
      char passwd_chr[2] = {'*', '\0'};

      edt->input_chain->prev->pTsurf =
        TTF_RenderUTF8_Blended(edt->pwidget->string_utf8->font,
                               passwd_chr,
                               edt->pwidget->string_utf8->fgcol);
    } else {
      edt->input_chain->prev->pTsurf =
        TTF_RenderUTF8_Blended(edt->pwidget->string_utf8->font,
                               edt->input_chain->prev->chr,
                               edt->pwidget->string_utf8->fgcol);
    }
    edt->Truelength += edt->input_chain->prev->pTsurf->w;

    if (edt->InputChain_X >= edt->pwidget->size.x + edt->pBg->w - adj_size(10)) {
      if (edt->input_chain == edt->end_text_chain) {
        edt->Start_X = edt->pBg->w - adj_size(5) - edt->Truelength;
      } else {
        edt->Start_X -= edt->input_chain->prev->pTsurf->w -
          (edt->pwidget->size.x + edt->pBg->w - adj_size(5) - edt->InputChain_X);
      }
    }

    edt->ChainLen++;
  }

  redraw_edit_chain(edt);

  return ID_ERROR;
}

/**********************************************************************//**
  Handle mouse down events on edit widget.
**************************************************************************/
static Uint16 edit_mouse_button_down(SDL_MouseButtonEvent *button_event,
                                     void *data)
{
  struct text_edit *edt = (struct text_edit *)data;

  if (button_event->button == SDL_BUTTON_LEFT) {
    if (!(button_event->x >= edt->pwidget->size.x
          && button_event->x < edt->pwidget->size.x + edt->pBg->w
          && button_event->y >= edt->pwidget->size.y
          && button_event->y < edt->pwidget->size.y + edt->pBg->h)) {
      /* exit from loop */
      return (Uint16)ED_MOUSE;
    }
  }

  return (Uint16)ID_ERROR;
}

/**********************************************************************//**
  Handle active edit. Returns what should happen to the edit
  next.
**************************************************************************/
enum edit_return_codes edit_field(struct widget *edit_widget)
{
  struct text_edit edt;
  struct Utf8Char ___last;
  struct Utf8Char *input_chain_tmp = NULL;
  enum edit_return_codes ret;
  void *backup = edit_widget->data.ptr;

  edt.pwidget = edit_widget;
  edt.ChainLen = 0;
  edt.Truelength = 0;
  edt.Start_X = adj_size(5);
  edt.InputChain_X = 0;

  edit_widget->data.ptr = (void *)&edt;

#if 0
  SDL_EnableUNICODE(1);
  SDL_EnableKeyRepeat(SDL_DEFAULT_REPEAT_DELAY, SDL_DEFAULT_REPEAT_INTERVAL);
#endif /* 0 */

  edt.pBg = create_bcgnd_surf(edit_widget->theme, 2,
                               edit_widget->size.w, edit_widget->size.h);

  /* Creating Chain */
  edt.begin_text_chain = text2chain(edit_widget->string_utf8->text);

  /* Creating Empty (Last) pice of Chain */
  edt.input_chain = &___last;
  edt.end_text_chain = edt.input_chain;
  edt.end_text_chain->chr[0] = 32; /* spacebar */
  edt.end_text_chain->chr[1] = 0;  /* spacebar */
  edt.end_text_chain->next = NULL;
  edt.end_text_chain->prev = NULL;

  /* set font style (if any ) */
  if (!((edit_widget->string_utf8->style & 0x0F) & TTF_STYLE_NORMAL)) {
    TTF_SetFontStyle(edit_widget->string_utf8->font,
                     (edit_widget->string_utf8->style & 0x0F));
  }

  edt.end_text_chain->pTsurf =
      TTF_RenderUTF8_Blended(edit_widget->string_utf8->font,
                             edt.end_text_chain->chr,
                             edit_widget->string_utf8->fgcol);

  /* create surface for each font in chain and find chain length */
  if (edt.begin_text_chain) {
    input_chain_tmp = edt.begin_text_chain;

    while (TRUE) {
      edt.ChainLen++;

      if (get_wflags(edit_widget) & WF_PASSWD_EDIT) {
        const char passwd_chr[2] = {'*', '\0'};

        input_chain_tmp->pTsurf =
          TTF_RenderUTF8_Blended(edit_widget->string_utf8->font,
                                 passwd_chr,
                                 edit_widget->string_utf8->fgcol);
      } else {
        input_chain_tmp->pTsurf =
          TTF_RenderUTF8_Blended(edit_widget->string_utf8->font,
                                 input_chain_tmp->chr,
                                 edit_widget->string_utf8->fgcol);
      }

      edt.Truelength += input_chain_tmp->pTsurf->w;

      if (input_chain_tmp->next == NULL) {
	break;
      }

      input_chain_tmp = input_chain_tmp->next;
    }
    /* set terminator of list */
    input_chain_tmp->next = edt.input_chain;
    edt.input_chain->prev = input_chain_tmp;
    input_chain_tmp = NULL;
  } else {
    edt.begin_text_chain = edt.input_chain;
  }

  redraw_edit_chain(&edt);
  
  set_wstate(edit_widget, FC_WS_PRESSED);
  {
    /* local loop */  
    Uint16 rety = gui_event_loop((void *)&edt, NULL,
                                 edit_key_down, NULL, edit_textinput, NULL, NULL, NULL,
                                 edit_mouse_button_down, NULL, NULL);

    if (edt.begin_text_chain == edt.end_text_chain) {
      edt.begin_text_chain = NULL;
    }

    if (rety == MAX_ID) {
      ret = ED_FORCE_EXIT;
    } else {
      ret = (enum edit_return_codes) rety;

      /* this is here because we have no knowledge that edit_widget exist
         or nor in force exit mode from gui loop */

      /* reset font settings */
      if (!((edit_widget->string_utf8->style & 0x0F) & TTF_STYLE_NORMAL)) {
        TTF_SetFontStyle(edit_widget->string_utf8->font, TTF_STYLE_NORMAL);
      }

      if (ret != ED_ESC) {
        size_t len = 0;

        FC_FREE(edit_widget->string_utf8->text);
        edit_widget->string_utf8->text =
          chain2text(edt.begin_text_chain, edt.ChainLen, &len);
        edit_widget->string_utf8->n_alloc = len + 1;
      }

      edit_widget->data.ptr = backup;
      set_wstate(edit_widget, FC_WS_NORMAL);
    }
  }

  FREESURFACE(edt.end_text_chain->pTsurf);

  del_chain(edt.begin_text_chain);

  FREESURFACE(edt.pBg);

  /* disable repeat key */

#if 0
  SDL_EnableKeyRepeat(0, SDL_DEFAULT_REPEAT_INTERVAL);

  /* disable Unicode */
  SDL_EnableUNICODE(0);
#endif /* 0 */

  return ret;
}
