/*
	Defs.cpp
	Copyright 1999 Be Do Have. All Rights Reserved.
*/

#include <support/Debug.h>
#include <Message.h>
#include "Defs.hpp"


void
Message_ReplaceString( BMessage *msg, const char *name, const char *string )
{
	if ( msg->HasString(name) ) {
		msg->ReplaceString(name, string);
	} else {
		msg->AddString(name, string);
	}
}
 
