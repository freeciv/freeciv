/*	
	UnitsBelowView.h
	Copyright 1999 Be Do Have Software. All Rights Reserved.
*/

#ifndef UNITSBELOWVIEW_H
#define UNITSBELOWVIEW_H
#include <Message.h>
#include "BdhView.h"

class UnitsBelowView
	: public BdhView
{
public:
			UnitsBelowView( int width );
			~UnitsBelowView();
	void	Draw( BRect r );
	void	AttachedToWindow(void);
	void	MessageReceived( BMessage *msg );
private:
};

#endif // UNITSBELOWVIEW_H
