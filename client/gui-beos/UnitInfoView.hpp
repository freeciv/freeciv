/*	
	UnitInfoView.h
	Copyright 1999 Be Do Have Software. All Rights Reserved.
*/

#ifndef UNITINFOVIEW_H
#define UNITINFOVIEW_H
#include <Message.h>
#include "BdhView.h"

class UnitInfoView
	: public BdhView
{
public:
			UnitInfoView( int width );
			~UnitInfoView();
	void	Draw( BRect r );
	void	AttachedToWindow(void);
	void	MessageReceived( BMessage *msg );
private:
};

#endif // UNITINFOVIEW_H
