/*
	BdhDialog.cpp
	Copyright 1996-1997 Be Do Have Software. All Rights Reserved.
	
 */

#include <interface/TextView.h>
#include <interface/Button.h>
#include <support/Debug.h>
#include "BdhDialog.h"
#include "BdhApp.h"

#include "BdhBitmap.h"


//-------------------------------------------------------
// Inline data for icons (32x32 COLOR_8)
#include "BdhDialog-data.cpp"

//-------------------------------------------------------
// BdhDialog

BdhDialog::BdhDialog( const BPoint upperLeft, BMessage *specMsg, BBitmap *icon )
	: BdhWindow( upperLeft, bdhApp()->appName(),
				B_NOT_RESIZABLE | B_NOT_ZOOMABLE | B_NOT_MINIMIZABLE,
				B_FLOATING_WINDOW	),
		replyingMsg(specMsg), syncMsg(0),
		target(0), icon(icon), timeoutInterval(0)
{
	// Heavy lifting is done by Init(), in reverse order of construction
}


BdhDialog::~BdhDialog()
{
	delete replyingMsg;
}


void
BdhDialog::Init( void )
{
	if ( Initted() ) { return; }
	
	// Slap text on top of contents
	const char *leadText;
	if ( replyingMsg->FindString("text", &leadText) == B_OK ) {
		BRect frame( 0, 0, max_c(250,viewList->frame().Width()), 50 );
		BTextView *tv = new BTextView( frame, "BdhDialog::text", frame,
				B_FOLLOW_LEFT | B_FOLLOW_TOP, B_WILL_DRAW );
		tv->SetText(leadText);
		tv->Insert( 0, "\n", 1 );
		tv->Insert( tv->TextLength(), "\n", 1 );
		tv->SetAlignment( B_ALIGN_CENTER );
		tv->MakeSelectable(false);
		tv->MakeEditable(false);
		tv->ResizeTo(	tv->Frame().Width(),
				tv->TextHeight( 0, tv->TextLength() ) );
		viewList->addAtTop(tv); 
	}

	// Slap icon onto the side
	if ( icon ) {
#define ric(val)	(reinterpret_cast<int>(val))
		// replace pseudo icon value with real icon
		switch (ric(icon)) {
		case ric(BDH_ICON_QUESTION):	// blue question mark
			icon = new BdhBitmap( bits_ICON_QUESTION, 32, 32, B_COLOR_8_BIT );
			break;
		case ric(BDH_ICON_EXCLAIM):	// black exclamation point
			icon = new BdhBitmap( bits_ICON_EXCLAIM, 32, 32, B_COLOR_8_BIT );
			break;
		case ric(BDH_ICON_FYI):		// green square
			icon = new BdhBitmap( bits_ICON_FYI, 32, 32, B_COLOR_8_BIT );
			break;
		case ric(BDH_ICON_WARN):		// yellow diamond
			icon = new BdhBitmap( bits_ICON_WARN, 32, 32, B_COLOR_8_BIT );
			break;
		case ric(BDH_ICON_ERROR):	// red circle
			icon = new BdhBitmap( bits_ICON_ERROR, 32, 32, B_COLOR_8_BIT );
			break;
		case ric(BDH_ICON_DEBUG):	// orange diamond
			icon = new BdhBitmap( bits_ICON_DEBUG, 32, 32, B_COLOR_8_BIT );
			break;
		case ric(BDH_ICON_UNIMPL):	// orange diamond with black X
			icon = new BdhBitmap( bits_ICON_UNIMPL, 32, 32, B_COLOR_8_BIT );
			break;
		case ric(BDH_ICON_APP):		// app's own icon
			icon = bdhApp()->largeIcon();
			break; 
		}
		BdhBitmapView *bmv = new BdhBitmapView( 32, 32, BPoint(0,0), icon );
		// position
		viewList->addAtLeft(bmv);
	}
	
	// If any buttons specified, add them
	int8 buttonCount;
	if ( replyingMsg->FindInt8("button", &buttonCount) == B_OK && buttonCount > 0 ) {
		// Pad between buttons and contents
		viewList->pad( 0, 0, 0, 8 );

		// Walk array of buttons ...
		BButton *rightmost = NULL;
		BButton *button    = NULL;
		const char *label;
		BMessage *msg;
		float prefWidth, prefHeight;
		BPoint newUL = viewList->frame().RightBottom();

		for ( int8 b=0 ; b<buttonCount ; b++ ) {
			// ... creating each requested button ...
			if ( replyingMsg->FindString("buttons", b, &label) != B_OK ) {
				label = ( b ? "Cancel" : "OK" );
			}
			msg = new BMessage( B_QUIT_REQUESTED );
			msg->AddInt8("button",b);
			button = new BButton( BRect(0,0,90,20),
					"BdhDialog::button", label, msg );
			button->SetTarget(this);
			if ( ! rightmost ) {
				rightmost = button;
			}
			// ... then positioning it
			button->GetPreferredSize( &prefWidth, &prefHeight );
			button->ResizeToPreferred();
			newUL -= BPoint(prefWidth,0);
			viewList->addAt( button, newUL );
			newUL -= BPoint(5,0);	// separate the buttons
		}
		
		// Final padding and focussing
		viewList->pad( 4, 4, 6, 4 );
		rightmost->MakeDefault(true);
	}
	
	BdhWindow::Init();
}


void
BdhDialog::Go( BHandler *handler )
{
	target = handler;
	Init();
	Show();
}


bool
BdhDialog::QuitRequested( void )
{
	BMessage *msg = CurrentMessage();

	int8 b;
	if ( msg->FindInt8("button", &b) != B_OK ) {
		b = -1;
	}

	int8 buttonCount;
        if ( replyingMsg->FindInt8("button", &buttonCount) == B_OK ) {
		replyingMsg->ReplaceInt8("button", b);
	} else {
		replyingMsg->AddInt8("button", b);
	}
	if ( target ) {
		target->Looper()->PostMessage( replyingMsg, target );
	}

	return BdhWindow::QuitRequested();
}


