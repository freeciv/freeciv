/**********************************************************************
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
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif 
#include <string.h>  
#include <stdlib.h>
#include <windows.h>
#ifdef HAVE_PNG_H
#include <png.h>
#endif

#include "game.h"
#include "log.h"
#include "mem.h"
#include "shared.h"
#include "support.h"
#include "unit.h"
#include "version.h"
 
#include "climisc.h"
#include "colors.h"
#include "mapview_g.h"
#include "tilespec.h"   

#include "graphics.h"
extern HINSTANCE freecivhinst;
extern HWND root_window;
static HDC hdcsmall;
static HDC hdcbig;
static SPRITE *sprcache;
static HBITMAP bitmapcache;
SPRITE *intro_gfx_sprite=NULL;
SPRITE *radar_gfx_sprite=NULL;
static SPRITE fog_sprite;


/**************************************************************************

**************************************************************************/
void
load_intro_gfx(void)
{
  intro_gfx_sprite=load_gfxfile(main_intro_filename);
  radar_gfx_sprite = load_gfxfile(minimap_intro_filename);     
}

/**************************************************************************

**************************************************************************/
void
load_cursors(void)
{
	/* PORTME */
}

/**************************************************************************

**************************************************************************/
void
free_intro_radar_sprites(void)
{
  if (intro_gfx_sprite)
    {
      free_sprite(intro_gfx_sprite);
      intro_gfx_sprite=NULL;
    }
  if (radar_gfx_sprite)
    {
      free_sprite(radar_gfx_sprite);
      radar_gfx_sprite=NULL;
    }
}

/**************************************************************************

**************************************************************************/
char **
gfx_fileextensions(void)
{
  static char *ext[] =
  {
    "bmp",
#ifdef HAVE_PNG_H
    "png",
#endif
    NULL
  };

  return ext;
}

/**************************************************************************

**************************************************************************/
static void HBITMAP2BITMAP(HBITMAP hbmp,BITMAP *bmp)
{
  int bmpsize;
  GetObject(hbmp,sizeof(BITMAP),bmp);
  bmpsize=bmp->bmHeight*bmp->bmWidthBytes;
  bmp->bmBits=fc_malloc(bmpsize);
  GetBitmapBits(hbmp,bmpsize,bmp->bmBits);
}

/**************************************************************************
For png files only
**************************************************************************/
#ifdef HAVE_PNG_H
static HBITMAP my_convert_alpha(png_bytep *old_buf,
				int width,int height)
{
  HBITMAP mask;
  png_bytep *new_buf;
  png_bytep *mask_buf;
  int x,y;
  int new_rowlen,mask_rowlen;
  new_rowlen=(width*3+3)&(~3);
  mask_rowlen=(width+7)/8;
  mask_rowlen=(mask_rowlen+3)&(~3);
  mask_buf=fc_malloc(sizeof(png_bytep)*height);
  mask_buf[0]=fc_malloc(mask_rowlen*height);
  new_buf=fc_malloc(sizeof(png_bytep)*height);
  new_buf[0]=fc_malloc(new_rowlen*height);
  for (y=1;y<height;y++)
    {
      mask_buf[y]=mask_buf[y-1]+mask_rowlen;
      new_buf[y]=new_buf[y-1]+new_rowlen;
    }
  for(y=0;y<height;y++)
    {
      png_byte mask_or;
      png_bytep oldbuf;
      png_bytep newbuf;
      png_bytep maskbuf;
      maskbuf=mask_buf[y];
      oldbuf=old_buf[y];
      newbuf=new_buf[y];
      mask_or=128;
      *maskbuf=0;
      for (x=0;x<width;x++)
	{
	  *newbuf=oldbuf[2];
	  newbuf++;
	  *newbuf=oldbuf[1];
	  newbuf++;
	  *newbuf=oldbuf[0];
	  newbuf++; 
	  oldbuf=oldbuf+3;
	  if (*oldbuf<128)
	    *maskbuf=*maskbuf|mask_or;
	  mask_or=mask_or>>1;
	  if (!mask_or)
	    {
	      mask_or=128;
	      maskbuf++;
	      *maskbuf=0;
	    }
	  oldbuf++;
	}
    }
  free(old_buf[0]);
  for (y=0;y<height;y++)
    old_buf[y]=new_buf[y];
  mask=CreateBitmap(width,height,1,1,mask_buf[0]);
  free(new_buf);
  free(mask_buf[0]);
  free(mask_buf);
  return mask;
}
#endif 

/**************************************************************************

**************************************************************************/
struct Sprite *
load_gfxfile_bmp(const char *filename)
{
  char filename2[512];
  HBITMAP bitmap;
  HBITMAP mask;
  SPRITE *mysprite;
  char *fname;
  strcpy(filename2,filename);
  if ((fname=strrchr(filename2,'.')))
    fname[0]=0;
  strcat(filename2,"-mask.bmp");
  bitmap=LoadImage(freecivhinst,filename,IMAGE_BITMAP,
		   0,0,LR_LOADFROMFILE);
  if (!bitmap) return NULL;
  
  mask=LoadImage(freecivhinst,filename2,IMAGE_BITMAP,0,0,
		 LR_LOADFROMFILE | LR_MONOCHROME);

  mysprite=fc_malloc(sizeof(struct Sprite));
  GetObject(bitmap,sizeof(BITMAP),&(mysprite->bmp));
  mysprite->has_mask=0;
  mysprite->width=mysprite->bmp.bmWidth;
  mysprite->height=mysprite->bmp.bmHeight;
  HBITMAP2BITMAP(bitmap,&mysprite->bmp);
  if (mask)
    {
      mysprite->has_mask=1;
      HBITMAP2BITMAP(mask,&mysprite->mask);
    }
  DeleteObject(bitmap);
  if (mask) DeleteObject(mask);
  return mysprite;
}


/**************************************************************************

**************************************************************************/
struct Sprite *
load_gfxfile(const char *filename)
{
#ifdef HAVE_PNG_H
  SPRITE *mysprite;
  HDC hdc;
  HBITMAP mask;
  HBITMAP dibitmap;
  BITMAPINFO my_bminf;
  png_bytep *row_pointers;
  png_structp png_ptr;
  png_infop info_ptr;
  png_uint_32 width, height;
  int row;
  int row_len;
  int num_channels;
  int bit_depth, color_type, interlace_type;     
  char header[8];
  FILE *fh;
#endif
  if (strstr(filename,".bmp"))
    return load_gfxfile_bmp(filename);
#ifndef HAVE_PNG_H
  return NULL;
#else
  fh=fopen(filename,"rb");
  if (!fh) 
    return NULL;
  fread(header,1,8,fh);
  if (png_sig_cmp(header, 0,8))
    {
      fclose(fh);
      return NULL;
    }
  png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING,
				   NULL,NULL,NULL);
  if (png_ptr == NULL)
    {
      fclose(fh);
      return NULL;
    }  
  info_ptr = png_create_info_struct(png_ptr);
  if (info_ptr == NULL)
    {
      fclose(fh);
      png_destroy_read_struct(&png_ptr, (png_infopp)NULL, (png_infopp)NULL);
      return NULL;
    }
  if (setjmp(png_ptr->jmpbuf))
    {
      /* Free all of the memory associated with the png_ptr and info_ptr */
      png_destroy_read_struct(&png_ptr, &info_ptr, (png_infopp)NULL);
      fclose(fh);
      /* If we get here, we had a problem reading the file */
      return NULL;
   }     
  png_init_io(png_ptr,fh);   
  png_set_sig_bytes(png_ptr, 8);   
  png_read_info(png_ptr, info_ptr);  
  png_get_IHDR(png_ptr, info_ptr, &width, &height, &bit_depth, &color_type,
	       &interlace_type, NULL, NULL);
  if (color_type == PNG_COLOR_TYPE_PALETTE)
    png_set_expand(png_ptr); 
  num_channels=png_get_channels(png_ptr,info_ptr);
  if (num_channels<4)
    png_set_bgr(png_ptr);
  if (color_type == PNG_COLOR_TYPE_GRAY && bit_depth < 8)
    png_set_expand(png_ptr);       
  png_read_update_info(png_ptr, info_ptr);     
  row_pointers=fc_malloc(sizeof(png_bytep)*height);
  row_len=(png_get_rowbytes(png_ptr,info_ptr)+3)&(~3);
  row_pointers[0]=fc_malloc(height*row_len);
  for (row = 1; row < height; row++)
    {
      row_pointers[row] = row_pointers[row-1]+row_len;
    }  
  png_read_image(png_ptr, row_pointers); 
  png_read_end(png_ptr, info_ptr);  
  fclose(fh);
  mask=NULL;
  if (num_channels==4)
    mask=my_convert_alpha(&row_pointers[0],width,height);
  
  my_bminf.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
  my_bminf.bmiHeader.biWidth=width;
  my_bminf.bmiHeader.biHeight=-height;
  my_bminf.bmiHeader.biPlanes=1;
  my_bminf.bmiHeader.biBitCount=bit_depth*3;
  my_bminf.bmiHeader.biCompression=BI_RGB;
  my_bminf.bmiHeader.biSizeImage=row_len*height;
  my_bminf.bmiHeader.biXPelsPerMeter=0;
  my_bminf.bmiHeader.biYPelsPerMeter=0;
  my_bminf.bmiHeader.biClrUsed=0;
  my_bminf.bmiHeader.biClrImportant=0;
  hdc=GetDC(root_window);
  mysprite=NULL;
  if (!hdcbig)
    { 
      hdcbig=CreateCompatibleDC(hdc);
    }
  dibitmap=CreateDIBitmap(hdc,&my_bminf.bmiHeader,
			  CBM_INIT,row_pointers[0],
			  &my_bminf,0);
  free(row_pointers[0]);
  free(row_pointers);
  ReleaseDC(root_window,hdc);
  if (!dibitmap)
    {
      freelog(LOG_FATAL,"CreateDIBitmap failed");
      return NULL;
    }
  mysprite=fc_malloc(sizeof(struct Sprite));
  mysprite->has_mask=0;
  if (mask)
    {
      mysprite->has_mask=1;
      HBITMAP2BITMAP(mask,&mysprite->mask);
      DeleteObject(mask);
    }
  mysprite->width=width;
  mysprite->height=height;
  HBITMAP2BITMAP(dibitmap,&mysprite->bmp);
  DeleteObject(dibitmap);
  printf("load_gfxfile end\n");
  return mysprite;
#endif
}


HBITMAP BITMAP2HBITMAP(BITMAP *bmp)
{
  return CreateBitmap(bmp->bmWidth,bmp->bmHeight,bmp->bmPlanes,
		      bmp->bmBitsPixel,bmp->bmBits);
}


/**************************************************************************

***************************************************************************/
void init_fog_bmp(void)
{
  int x,y;
  POINT points[4];
  HBRUSH oldbrush;
  HBITMAP old;
  HDC hdc;
  HBITMAP fog;
  hdc=CreateCompatibleDC(NULL);
  fog=CreateCompatibleBitmap(hdc,NORMAL_TILE_WIDTH,NORMAL_TILE_HEIGHT);
  old=SelectObject(hdc,fog);
  BitBlt(hdc,0,0,NORMAL_TILE_WIDTH,NORMAL_TILE_HEIGHT,NULL,0,0,BLACKNESS);
  SelectObject(hdc,old);
  fog_sprite.width=NORMAL_TILE_WIDTH;
  fog_sprite.height=NORMAL_TILE_HEIGHT;
  HBITMAP2BITMAP(fog,&fog_sprite.bmp);
  DeleteObject(fog);
  fog_sprite.has_mask=1;
  fog=CreateBitmap(NORMAL_TILE_WIDTH,NORMAL_TILE_HEIGHT,1,1,NULL);
  old=SelectObject(hdc,fog);
  BitBlt(hdc,0,0,NORMAL_TILE_WIDTH,NORMAL_TILE_HEIGHT,NULL,0,0,WHITENESS);
  SetBkMode(hdc,TRANSPARENT);
  oldbrush=SelectObject(hdc,GetStockObject(BLACK_BRUSH));
  points[0].x=NORMAL_TILE_WIDTH/2;
  points[0].y=0;
  points[1].x=0;
  points[1].y=NORMAL_TILE_HEIGHT/2;
  points[2].x=NORMAL_TILE_WIDTH/2;
  points[2].y=NORMAL_TILE_HEIGHT;
  points[3].x=NORMAL_TILE_WIDTH;
  points[3].y=NORMAL_TILE_HEIGHT/2;
  Polygon(hdc,points,4);
  SelectObject(hdc,oldbrush);
  for(x=0;x<NORMAL_TILE_WIDTH;x++)
    for(y=0;y<NORMAL_TILE_HEIGHT;y++)
      {
	if (!GetPixel(hdc,x,y))
	  {
	    if ((x+y)&1)
	      SetPixel(hdc,x,y,RGB(255,255,255));
	  }
      }
  SelectObject(hdc,old);
  HBITMAP2BITMAP(fog,&fog_sprite.mask);
  DeleteObject(fog);
  DeleteObject(hdc);
  
}


struct Sprite *
crop_sprite(struct Sprite *source,
                           int x, int y, int width, int height)
{
  SPRITE *mysprite;
  HDC hdc;
  HBITMAP smallbitmap;
  HBITMAP smallmask;
  HBITMAP bigbitmap;
  HBITMAP bigmask;
  HBITMAP bigsave;
  HBITMAP smallsave;
  hdc=GetDC(root_window);
  mysprite=NULL;
  if (!hdcbig)
    hdcbig=CreateCompatibleDC(hdc);
  if (!hdcsmall)
    hdcsmall=CreateCompatibleDC(hdc);
  if (sprcache!=source)
    {
      if (bitmapcache) DeleteObject(bitmapcache);
      sprcache=source;
      bitmapcache=BITMAP2HBITMAP(&(source->bmp));
    }
  bigbitmap=bitmapcache;
  if (!bigbitmap)
    {
      freelog(LOG_FATAL,"BITMAP2HBITMAP failed");
      return NULL;
    }
  if (!(hdcsmall&&hdcbig))
    {
      freelog(LOG_FATAL,"CreateCompatibleDC failed");
    }
  if (!(smallbitmap=CreateCompatibleBitmap(hdc,width,height)))
    {
      freelog(LOG_FATAL,"CreateCompatibleBitmap failed");
      return NULL;
    }
  
  bigsave=SelectObject(hdcbig,bigbitmap);
  smallsave=SelectObject(hdcsmall,smallbitmap);
  BitBlt(hdcsmall,0,0,width,height,hdcbig,x,y,SRCCOPY);
  smallmask=NULL;

  if (source->has_mask)
    {
      bigmask=BITMAP2HBITMAP(&source->mask);
      SelectObject(hdcbig,bigmask);
      if ((smallmask=CreateBitmap(width,height,1,1,NULL)))
	{
	  SelectObject(hdcsmall,smallmask);
	  BitBlt(hdcsmall,0,0,width,height,hdcbig,x,y,SRCCOPY);
	}
      SelectObject(hdcbig,bigbitmap);
      DeleteObject(bigmask);
    }

  mysprite=fc_malloc(sizeof(struct Sprite));
  mysprite->width=width;
  mysprite->height=height;
  HBITMAP2BITMAP(smallbitmap,&mysprite->bmp);
  mysprite->has_mask=0;
  if (smallmask)
    {
      mysprite->has_mask=1;
      HBITMAP2BITMAP(smallmask,&mysprite->mask);
    }

  SelectObject(hdcbig,bigsave);
  SelectObject(hdcsmall,smallsave);
  ReleaseDC(root_window,hdc);
  if (smallmask) DeleteObject(smallmask);
  DeleteObject(smallbitmap);
     
   
  return mysprite;
}


void draw_sprite(struct Sprite *sprite, HDC hdc, int x, int y)
{
  draw_sprite_part(sprite,hdc,x,y,sprite->width,sprite->height,0,0);
}

void  draw_sprite_part(struct Sprite *sprite,HDC hdc,int x, int y,int w, int h,
		       int xsrc, int ysrc)
{
  static HDC hdccomp;
  HBITMAP tempbit;
  HBITMAP bitmap;
  HBITMAP maskbit;
  if (!sprite) return;
  if (!hdccomp)
    hdccomp=CreateCompatibleDC(hdc);
  bitmap=BITMAP2HBITMAP(&sprite->bmp);
  if (sprite->has_mask)
    {
      COLORREF fgold,bgold;
      fgold=SetTextColor(hdc,RGB(0,0,0));
      bgold=SetBkColor(hdc,RGB(255,255,255));
      maskbit=BITMAP2HBITMAP(&sprite->mask);
      tempbit=SelectObject(hdccomp,maskbit);
      BitBlt(hdc,x,y,w,h,hdccomp,xsrc,ysrc,SRCAND);
      SelectObject(hdccomp,bitmap);
      BitBlt(hdc,x,y,w,h,hdccomp,xsrc,ysrc,SRCINVERT);
      DeleteObject(maskbit);
      SetBkColor(hdc,bgold);
      SetTextColor(hdc,fgold);
    }
  else
    {
      tempbit=SelectObject(hdccomp,bitmap);
      BitBlt(hdc,x,y,w,h,hdccomp,0,0,SRCCOPY);
    }
  SelectObject(hdccomp,tempbit);
  /*  DeleteDC(hdccomp); */
  DeleteObject(bitmap);

}

void draw_fog_part(HDC hdc,int x, int y,int w, int h,
		   int xsrc, int ysrc)
{
  draw_sprite_part(&fog_sprite,hdc,x,y,w,h,xsrc,ysrc);
}

void  crop_sprite_real(struct Sprite *source)
{
}         
void
free_sprite(struct Sprite *s)
{
  if (s->has_mask)
    {
      free(s->mask.bmBits);
    }
  
  
  free(s->bmp.bmBits);
  
  free(s);
  if (bitmapcache)
    DeleteObject(bitmapcache);
  sprcache=NULL;
}
int
isometric_view_supported(void)
{
        /* PORTME */
        return 1;
}
 
int
overhead_view_supported(void)
{
        /* PORTME */
        return 1;
}
    
