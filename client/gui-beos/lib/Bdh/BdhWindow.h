/*
	BdhWindow.h
	Copyright 1996-1997 Be Do Have Software. All Rights Reserved.
*/
#ifndef BDH_WINDOW_H
#define BDH_WINDOW_H 1

#include <SupportDefs.h>
#include <Window.h>
#include <Menu.h>
#include <MenuBar.h>
#include <MenuItem.h>

#include "BdhViewList.h"

// Helpful little character define
#define ELLIPSIS	"\xE2\x80\xA6"


//------------------------------------------
// Standard shared library gibberish
#include "BdhBuild.h"
#undef _EXPIMP
#if _BUILDING_Bdh + 0 > 0
#define _EXPIMP	_EXPORT
#else
#define _EXPIMP	_IMPORT
#endif

#if __POWERPC__ 
#pragma export on
#endif
//------------------------------------------


// -- Unmenued window, built from list of views
class _EXPIMP BdhWindow : public BWindow {
public:
	BdhWindow( BPoint upperLeft, const char* title,
		uint32 flags, window_type type = B_TITLED_WINDOW );
	virtual ~BdhWindow();
	virtual void Init(void);	// all views via new!!
	bool Initted(void) { return initted; }
	void FocusIs( uint8 whichChild )
		{ ChildAt(whichChild)->MakeFocus(TRUE); }
public:
	BdhViewList	*viewList;
private:
	bool	initted;
};

// -- Menued window, built from list of views
// Supporting classes for BdhMenuedWindow
class BdhMenuBar : public BMenuBar {
public:
	BdhMenuBar(float, float, const char*, bool appMenu = true );
	~BdhMenuBar( );
};

class _EXPIMP BdhMenuIcon : public BMenuItem {
public:
	BdhMenuIcon( BBitmap *bm, const char* label,
		BMessage *message, char shortcut = 0,
		uint32 modifiers = 0 );
	BdhMenuIcon( BBitmap *bm, BMenu *menu );
	virtual ~BdhMenuIcon();
protected:
	virtual void DrawContent(void);
	virtual void GetContentSize( float *width, float *height );
private:
	BBitmap* bitmap;
};

// First child is always automatically-created menubar
class _EXPIMP BdhMenuedWindow : public BdhWindow {
public:
	BdhMenuedWindow( BPoint upperLeft, const char* title, uint32 flags,
		window_type type = B_TITLED_WINDOW, bool appMenu = true );
	virtual ~BdhMenuedWindow(); 
	virtual void Init(void);	// all views via new!!
	bool AddItem( BMenu*, uint32 );
	bool AddItem( BMenu*, uint32, uint32 );
	bool AddItem( BMenuItem*, uint32 );
	bool AddItem( BMenuItem*, uint32, uint32 );
	BMenu* GetMenu( uint32 i )
		{ return menuBar->SubmenuAt(i); }
	BMenu* GetMenu( uint32 i, uint32 j )
		{ return menuBar->SubmenuAt(i)->SubmenuAt(j); }
	BMenuItem* GetItem( uint32 i )
		{ return menuBar->ItemAt(i); }
	BMenuItem* GetItem( uint32 i, uint32 j )
		{ return menuBar->SubmenuAt(i)->ItemAt(j); }
private:
	bool		appMenu;
	BdhMenuBar *menuBar;
};

#if __POWERPC__ 
#pragma export off
#endif
#endif // BDH_WINDOW_H
