/*	
	SummaryView.h
	Copyright 1999 Be Do Have Software. All Rights Reserved.
*/

#ifndef SUMMARYVIEW_H
#define SUMMARYVIEW_H
#include <Message.h>
#include "BdhView.h"

class SummaryView
	: public BdhView
{
public:
			SummaryView( int width );
			~SummaryView();
	void	Draw( BRect r );
	void	AttachedToWindow(void);
	void	MessageReceived( BMessage *msg );
private:
};

#endif // SUMMARYVIEW_H
