/*
	BdhDialog.cpp
	Copyright 1996-1997 Be Do Have Software. All Rights Reserved.
	
 */

#include <support/Debug.h>
#include <interface/TextControl.h>
#include "BdhEntryDialog.h"

//-------------------------------------------------------
// BdhEntryDialog

BdhEntryDialog::BdhEntryDialog( const BPoint upperLeft,
		BMessage *specMsg, BBitmap *icon )
	: BdhDialog( upperLeft, specMsg, icon ), entryList(0)
{
	entryList = new BList;
}


BdhEntryDialog::~BdhEntryDialog()
{
	delete entryList;	// contents are child views, ~BWindow deletes
}

void
BdhEntryDialog::Init(void)
{
	if ( Initted() ) {
		return;
	}
	
	// alter replyingMsg to constrain buttons
	int8 buttonCount;
	if ( replyingMsg->FindInt8("button", &buttonCount) != B_OK ) {
		replyingMsg->AddInt8("button", 2);
	} else if ( buttonCount != 2 ) {
		replyingMsg->ReplaceInt8("button", 2);
	}
	
	// put in entry boxes
	int32 entryIndex = 0;
	const char *label;
	const char *data;
	bool hidden;
	BTextControl *box;
	while ( replyingMsg->FindString("label", entryIndex, &label) == B_OK ) {
		// add an entry box
		if ( replyingMsg->FindString("data", entryIndex, &data) != B_OK ) {
			data = NULL;
		}
		box = new BTextControl( BRect(0,0,360,15), "BdhEntryDialog::entry",
				label, data, NULL );
		box->SetDivider( 120 );
		box->SetAlignment( B_ALIGN_RIGHT, B_ALIGN_LEFT );
		if ( replyingMsg->FindBool("hidden", &hidden) != B_OK ) {
			hidden = false;
		} else {
			box->TextView()->HideTyping(hidden);
		}
		entryList->AddItem( box );
		viewList->addAtBottom( box );
		
		entryIndex++;
	}
	
	BdhDialog::Init();
}


bool
BdhEntryDialog::QuitRequested( void )
{
	BMessage *msg = CurrentMessage();
        int8 b = -1;
        if ( msg->FindInt8("button", &b) != B_OK ) {
                b = -1;
        }
 	if ( b == 0 ) {		// OK clicked
		const char *label;
		const char *text;
		for ( int32 ee = 0 ;
		      replyingMsg->FindString("label", ee, &label) == B_OK ;
		      ee++ ) {
			BTextControl *box = cast_to(
				entryList->ItemAt(ee), BTextControl );
			if ( box ) {
				text = box->Text();
			} else {
				text = "";
			}
			if ( replyingMsg->HasString("data", ee) ) {
				replyingMsg->ReplaceString("data", ee, text );
			} else {
				replyingMsg->AddString("data", text );
			}
		}
	}
	return BdhDialog::QuitRequested();
}


