/*	
	MapCanvas.h
	Copyright 1999 Be Do Have Software. All Rights Reserved.
*/

#ifndef MAPCANVAS_H
#define MAPCANVAS_H
#include <Message.h>
#include <ScrollView.h>
#include "SquareView.h"
#include "tilespec.h"
#include "graphics.hpp"

class MapCanvas
	: public BScrollView
{
public:
			MapCanvas( int tiles_wide, int tiles_high,
                  int tile_width, int tile_height );
			~MapCanvas();
	void	Draw( BRect r );
	void	AttachedToWindow(void);
	void	MessageReceived( BMessage *msg );
private:
	int		tile_width;
	int		tile_height;
	int		tiles_wide;
	int		tiles_high;
};

class MapView
	: public SquareView 
{
public:
			MapView( int tiles_wide, int tiles_high,
                  int tile_width, int tile_height );
			~MapView();
	void	Draw( BRect r );
	void	AttachedToWindow(void);
	void	MessageReceived( BMessage *msg );
private:
	int		tile_width;
	int		tile_height;
	int		tiles_wide;
	int		tiles_high;
};

#endif // MAPCANVAS_H
