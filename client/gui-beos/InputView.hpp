/*	
	InputView.h
	Copyright 1999 Be Do Have Software. All Rights Reserved.

	LATER REFINEMENT:  BButton and one-line BTextView
*/

#ifndef INPUTVIEW_H
#define INPUTVIEW_H
#include <Message.h>
#include "TextGridView.h"

class InputView
	: public TextGridView
{
public:
			InputView();
			~InputView();
	void	Draw( BRect r );
	void	AttachedToWindow(void);
	void	MessageReceived( BMessage *msg );
private:
};

#endif // INPUTVIEW_H
