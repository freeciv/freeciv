/*	
	InputView.h
	Copyright 1999 Be Do Have Software. All Rights Reserved.

	LATER REFINEMENT:  BButton and one-line BTextView
*/

#ifndef INPUTVIEW_H
#define INPUTVIEW_H
#include <Message.h>
#include <Font.h>
#include <TextView.h>

class InputView
	: public BTextView
{
public:
			InputView( const BFont *font );
			~InputView();
	void	AttachedToWindow(void);
	void	KeyDown( const char *bytes, int32 numBytes );
	void	Clear(void) { SetText(NULL); }
private:
	static float	HeightOf( const BFont *font );
};

#endif // INPUTVIEW_H
