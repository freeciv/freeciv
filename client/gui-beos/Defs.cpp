/*
	Defs.cpp
	Copyright 1999 Be Do Have. All Rights Reserved.
*/

#include <support/Debug.h>
#include <Message.h>
#include <Font.h>
#include "Defs.hpp"


//--------------------------------------------------------------
// BMessage manipulations 

void
Message_ReplaceString( BMessage *msg, const char *name, const char *string )
{
	if ( msg->HasString(name) ) {
		msg->ReplaceString(name, string);
	} else {
		msg->AddString(name, string);
	}
}
 

//--------------------------------------------------------------
// BFont info

float
HeightOf( const BFont *font )
{
    font_height fh;
    font->GetHeight(&fh);
    return (fh.ascent + fh.descent + fh.leading);
} 

