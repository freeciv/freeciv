/*
	BdhDialog.h
	Copyright (c) 1997 Be Do Have Software.  All rights reserved.
 */
#ifndef BDH_DIALOG_H
#define BDH_DIALOG_H 1

#include "BdhView.h"
#include "BdhWindow.h"

//------------------------------------------
// Standard shared library gibberish
#include "BdhBuild.h"
#undef _EXPIMP
#if _BUILDING_BdhDialog + 0 > 0
#define _EXPIMP	_EXPORT
#else
#define _EXPIMP	_IMPORT
#endif

#if __POWERPC__ 
#pragma export on
#endif
//------------------------------------------

// Helper definition, akin to cast_as
#define cast_to(expr, type)	(reinterpret_cast<type *>(expr))

// Icons defined
#define BDH_ICON_NONE		((BBitmap*) 0x00)
#define BDH_ICON_QUESTION	((BBitmap*) 0x08)
#define BDH_ICON_EXCLAIM	((BBitmap*) 0x10)
#define BDH_ICON_FYI		((BBitmap*) 0x18)
#define BDH_ICON_WARN		((BBitmap*) 0x20)
#define BDH_ICON_ERROR		((BBitmap*) 0x28)
#define BDH_ICON_DEBUG		((BBitmap*) 0x30)
#define BDH_ICON_UNIMPL		((BBitmap*) 0x38)
#define BDH_ICON_APP		((BBitmap*) 0x40)


//------------------------------------------
// Constructed as B_FLOATING_WINDOW type;  to adjust after construction,
// simply SetLook(), SetFeel(), or SetType() before Show()ing.
class _EXPIMP BdhDialog : public BdhWindow {
public:
	BdhDialog( const BPoint upperLeft, BMessage *specMsg,
			BBitmap *icon = BDH_ICON_NONE );
	virtual		~BdhDialog();
	virtual void	Init(void);		// all views via new!!
	void			Go( BHandler *handler );
	virtual bool	QuitRequested( void );
public:
	BMessage *	replyingMsg;	// msg to copy for sending
private:
	BMessage *	syncMsg;
	BHandler *	target;
	BBitmap *	icon;
	unsigned long	timeoutInterval;	// milliseconds
};

#if __POWERPC__ 
#pragma export off
#endif
#endif // BDH_DIALOG_H
