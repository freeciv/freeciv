/*
	BdhWindow.cpp
	Copyright 1996-1997 Be Do Have Software. All Rights Reserved.
*/

#include <Application.h>

#define _BUILDING_Bdh 1
#include "BdhApp.h"
#include "BdhWindow.h"
#include "BdhBitmap.h"

// -----------------------------------------------------------
// BdhWindow

BdhWindow::BdhWindow( BPoint upperLeft, const char* title,
			uint32 flags, window_type type )
	: BWindow( BRect(upperLeft.x,upperLeft.y,upperLeft.x+50,upperLeft.y+5),
				title, type, flags ),
		initted(false)
{
	viewList = new BdhViewList;
}

void
BdhWindow::Init( void )
{
	if ( initted ) {
		return;
	}
		
	// -- walk list of views, adding them as children
	if ( viewList->CountItems() > 0 ) {
		Lock();
		for ( long c=0; c < viewList->CountItems(); c++ ) {
			BView *vp = viewList->viewAt(c);
			if ( vp ) { AddChild( vp ); }
		}
		BRect r = viewList->frame();
		ResizeTo( r.Width(), r.Height() );
		ChildAt(0)->MakeFocus(true);
		Unlock();
	}
	
	initted = true;
}

BdhWindow::~BdhWindow()
{
	delete viewList;
} 


// -----------------------------------------------------------
// BdhMenuedWindow
// first child is always menubar;
// user needs to make room for it

BdhMenuedWindow::BdhMenuedWindow( BPoint ul, const char* title,
		uint32 flags, window_type type, bool appMenu )
	: BdhWindow( ul, title, flags, type ),
			appMenu(appMenu)
{
}


BdhMenuedWindow::~BdhMenuedWindow()
{
}


void
BdhMenuedWindow::Init( void )
{
	BRect r = viewList->frame();
	menuBar = new BdhMenuBar( r.left, r.right, "menubar", appMenu );
	viewList->addAtTop( menuBar );
	BdhWindow::Init();
}


bool
BdhMenuedWindow::AddItem( BMenu* m, uint32 dex )
{
	return menuBar->AddItem(m,dex);
}

bool
BdhMenuedWindow::AddItem( BMenuItem* mi, uint32 dex )
{
	return menuBar->AddItem(mi,dex);
}

bool
BdhMenuedWindow::AddItem( BMenu* m, uint32 mdex, uint32 dex )
{
	BMenu *mm = menuBar->SubmenuAt(mdex);
	return (mm ? (mm->AddItem(m,dex)) : false );
}

bool
BdhMenuedWindow::AddItem( BMenuItem* mi, uint32 mdex, uint32 dex )
{
	BMenu *mm = menuBar->SubmenuAt(mdex);
	return (mm ? (mm->AddItem(mi,dex)) : false );
}

// -----------------------------------------------------------
// BdhMenuBar
// first menu contains icon of application
// first menu is defaulted to About | separator | Quit

BdhMenuBar::BdhMenuBar( float left, float right,
			const char* title, bool appMenu )
	: BMenuBar( BRect(left, 0, right, 20), title )
{
	BMenu *menu = new BMenu("  ");	
	if ( appMenu ) {
		BMenuItem *item;
		item = new BMenuItem( "About " ELLIPSIS,
				new BMessage(B_ABOUT_REQUESTED) );
		item->SetTarget( be_app );
		menu->AddItem( item );
		menu->AddSeparatorItem();
		item = new BMenuItem( "Quit",
				new BMessage(B_QUIT_REQUESTED), 'Q' );
		menu->AddItem( item );
	}
	AddItem( new BdhMenuIcon(NULL,menu), 0 );
}

BdhMenuBar::~BdhMenuBar()
{
}

// -----------------------------------------------------------
// BdhMenuIcon
// menu item which uses small bitmap or icon as label
static char defaultBits[] = {
	 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16,
	 16, 42, 42, 42, 16, 16, 52, 52, 52, 16, 16, 32, 16, 16, 32, 16,
	 16, 42, 16, 16, 42, 16, 52, 16, 16, 52, 16, 32, 16, 16, 32, 16,
	 16, 42, 16, 16, 42, 16, 52, 16, 16, 52, 16, 32, 16, 16, 32, 16,
	 16, 42, 16, 16, 42, 16, 52, 16, 16, 52, 16, 32, 16, 16, 32, 16,
	 16, 42, 16, 16, 42, 16, 52, 16, 16, 52, 16, 32, 16, 16, 32, 16,
	 16, 42, 42, 42, 16, 16, 52, 16, 16, 52, 16, 32, 32, 32, 32, 16,
	 16, 42, 16, 16, 42, 16, 52, 16, 16, 52, 16, 32, 16, 16, 32, 16,
	 16, 42, 16, 16, 42, 16, 52, 16, 16, 52, 16, 32, 16, 16, 32, 16,
	 16, 42, 16, 16, 42, 16, 52, 16, 16, 52, 16, 32, 16, 16, 32, 16,
	 16, 42, 16, 16, 42, 16, 52, 16, 16, 52, 16, 32, 16, 16, 32, 16,
	 16, 42, 42, 42, 16, 16, 52, 52, 52, 16, 16, 32, 16, 16, 32, 16,
	 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16,
	 16,102,102,102,102,102,102,102,102,102,102,102,102,102,102, 16,
	 16,102,102,102,102,102,102,102,102,102,102,102,102,102,102, 16,
	 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16,
};

BdhMenuIcon::BdhMenuIcon( BBitmap *bm, const char* label,
		BMessage *message, char shortcut, uint32 modifiers )
	: BMenuItem(label, message, shortcut, modifiers), bitmap(bm)
{
	if (! bitmap ) {
		bitmap = bdhApp()->miniIcon();
	}
	if ( ! bitmap ) {
		bitmap = new BdhBitmap( defaultBits, 16, 16, B_CMAP8 );
	}
}


BdhMenuIcon::BdhMenuIcon( BBitmap *bm, BMenu *menu )
	: BMenuItem(menu), bitmap(bm)
{
	if (! bitmap ) {
		bitmap = (dynamic_cast<BdhApp*>(be_app))->miniIcon();
	}
	if ( ! bitmap ) {
		bitmap = new BdhBitmap( defaultBits, 16, 16, B_CMAP8 );
	}
}

BdhMenuIcon::~BdhMenuIcon()
{
	bitmap = 0;
}

void
BdhMenuIcon::DrawContent( void )
{
	Menu()->SetDrawingMode( B_OP_OVER );
	Menu()->DrawBitmap( bitmap );
}

void
BdhMenuIcon::GetContentSize( float *width, float *height )
{
	if ( bitmap ) {
		BRect bounds = bitmap->Bounds();
		*width  = bounds.Width();
		*height = bounds.Height();
	} else {
		*width  = 16;
		*height = 16;
	}
}

