/*
	BdhEntryDialog.h
	Copyright (c) 1997 Be Do Have Software.  All rights reserved.
 */
#ifndef BDH_ENTRY_DIALOG_H
#define BDH_ENTRY_DIALOG_H 1

#include "BdhDialog.h"

#if __POWERPC__ 
#pragma export on
#endif 

// -- Data entry boxes
class _EXPIMP BdhEntryDialog : public BdhDialog {
public:
	BdhEntryDialog( const BPoint upperLeft, BMessage *specMsg,
			BBitmap *icon = BDH_ICON_NONE );
	virtual			~BdhEntryDialog();
	virtual void	Init(void);			// all views via new!!
	virtual bool	QuitRequested( void );
private:
	BList *		entryList;
};



#if __POWERPC__ 
#pragma export off
#endif
#endif // BDH_ENTRY_DIALOG_H
