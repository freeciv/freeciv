/*	
	OutputView.h
	Copyright 1999 Be Do Have Software. All Rights Reserved.

	LATER REFINEMENT:  BScrollView parenting BTextView
*/

#ifndef OUTPUTVIEW_H
#define OUTPUTVIEW_H
#include <Message.h>
#include <ScrollView.h>

class BFont;
class BTextView;

class OutputView
	: public BScrollView
{
public:
			OutputView( const BFont *font, float width );
			~OutputView();
	void	MessageReceived( BMessage *msg );
	void	AttachedToWindow( void );
	void	Append( const char *str );
	void	ExportToLog( void );
	void	Clear( void );
private:
	float	lineSize;
	BTextView	*text;
};

#endif // OUTPUTVIEW_H
