/*	
	RadarView.h
	Copyright 1999 Be Do Have Software. All Rights Reserved.
*/

#ifndef RADARVIEW_H
#define RADARVIEW_H

#include <Message.h>
#include "BdhView.h"
#include "graphics.hpp"

class RadarView
	: public BdhView
{
public:
			RadarView( Sprite *bm );
			~RadarView();
	void	Draw( BRect r );
	void	AttachedToWindow(void);
	void	MessageReceived( BMessage *msg );
	int	Width(void) { return Frame().IntegerWidth(); }
private:
};

#endif // RADARVIEW_H
