/*	
	OutputView.h
	Copyright 1999 Be Do Have Software. All Rights Reserved.

	LATER REFINEMENT:  BScrollView parenting BTextView
*/

#ifndef OUTPUTVIEW_H
#define OUTPUTVIEW_H
#include <Message.h>
#include "TextGridView.h"

class OutputView
	: public TextGridView
{
public:
			OutputView();
			~OutputView();
	void	Draw( BRect r );
	void	AttachedToWindow(void);
	void	MessageReceived( BMessage *msg );
private:
};

#endif // OUTPUTVIEW_H
