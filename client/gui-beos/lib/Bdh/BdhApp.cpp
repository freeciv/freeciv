/*
	BdhApp.cpp
	Copyright 1996-1997 Be Do Have Software. All Rights Reserved.
*/

#include <unistd.h>
#include <string.h>
#include <Entry.h>
#include <Path.h>
#include <AppFileInfo.h>
#include <Resources.h>

#define _BUILDING_Bdh 1
#include "BdhApp.h"


BdhApp::BdhApp( const char* signature )
	: BApplication( signature ), appsig("none"), window_count(0), pref(0)
{
	be_app->GetAppInfo(&appInfo);

	BEntry entry( &(appInfo.ref) );
	appname = new char[B_FILE_NAME_LENGTH];
	entry.GetName(appname);
	
	BEntry dir;
	entry.GetParent( &dir );
	BPath path;
	dir.GetPath( &path );
	dirpath = new char[strlen(path.Path())+1];
	strcpy( dirpath, path.Path() );

	const char *p = strrchr( signature, '/' );
	pref = new TPreferences( (p ? &p[1] : signature) );
}

BdhApp::~BdhApp()
{
	delete pref;
	delete [] dirpath;
	delete [] appname;
}

void
BdhApp::ReadyToRun( void )
{
	BApplication::ReadyToRun();
	chdir( dirpath );
}


const char *
appDirpath( void )
{
	return bdhApp()->appDirpath();
}

BdhApp *
bdhApp( void )
{
	return dynamic_cast<BdhApp*>(be_app);
}


void
BdhApp::HelpRequested( void )
{
	// construct help_url
	char help_url[256];
	strcpy( help_url, "file://" );
	strcat( help_url, appDirpath() );
	strcat( help_url, "/help.html" );
	GotoUrl( help_url );
}

	
void
BdhApp::GotoUrl( const char* url )
{
	if ( ! url )
		return;

	// construct message
	BMessage msg( B_ARGV_RECEIVED );
	msg.AddString( "argv", "NetPositive" );
	msg.AddString( "argv", url );
	msg.AddInt32(  "argc", 2 );
	
	// launch
#define NET_POSITIVE_SIGNATURE	"application/x-vnd.Be-NPOS"
	BMessenger messenger( NET_POSITIVE_SIGNATURE, -1, NULL );
	if ( messenger.IsValid() ) {
		messenger.SendMessage(&msg);
	} else {
		be_roster->Launch( NET_POSITIVE_SIGNATURE, &msg );
	}
}


BBitmap*
BdhApp::miniIcon( void )
{

	BFile file;
	if ( file.SetTo( appRef(), B_READ_ONLY ) != B_NO_ERROR ) {
		return 0;
	}

	// attempt to locate using BAppFileInfo
	BBitmap *icon = new BBitmap(BRect(0,0,15,15), B_CMAP8);
	BAppFileInfo afi(&file);
	if ( afi.GetIcon( icon, B_MINI_ICON ) == B_NO_ERROR ) {
		return icon;
	}

	// no go, so try resource
	BResources res;
	if ( res.SetTo(&file) != B_NO_ERROR ) {
		// cannot open file as resource container
		return 0;
	}
	if ( ! res.HasResource( 0x4d49434e /*MICN*/, 101 ) ) {
		// no mini icon in resources
		return 0;
	}

	// transfer data from resource to bitmap
	size_t sz;
	void *data = res.FindResource( 0x4d49434e /*MICN*/, 101, &sz );
	if ( sz != 16*16 ) {
		// unexpected size
		return 0;
	}
	icon->SetBits( data, sz, 0, B_CMAP8 );
	return icon;
}


BBitmap*
BdhApp::largeIcon( void )
{

	BFile file;
	if ( file.SetTo( appRef(), B_READ_ONLY ) != B_NO_ERROR ) {
		return 0;
	}

	// attempt to locate using BAppFileInfo
	BAppFileInfo afi(&file);
	BBitmap *icon = new BBitmap(BRect(0,0,31,31), B_CMAP8);
	if ( afi.GetIcon( icon, B_LARGE_ICON ) == B_NO_ERROR ) {
		return icon;
	}

	// no go, so try resource
	BResources res;
	if ( res.SetTo(&file) != B_NO_ERROR ) {
		// cannot open file as resource container
		delete icon;
		return 0;
	}
	if ( ! res.HasResource( 0x49434f4e /*ICON*/, 101 ) ) {
		// no large icon in resources
		delete icon;
		return 0;
	}

	// transfer data from resource to bitmap
	size_t sz;
	void *data = res.FindResource( 0x49434f4e /*ICON*/, 101, &sz );
	if ( sz != 32*32 ) {
		// unexpected size
		return 0;
	}
	icon->SetBits( data, sz, 0, B_CMAP8 );
	return icon;
}


uint
BdhApp::AddWindow(    const BWindow *win )
{
	window_count++;
	return window_count;
}

uint
BdhApp::RemoveWindow( const BWindow *win )
{
	if ( window_count > 0 ) window_count--;
	if ( window_count == 0 ) {
		be_app->PostMessage( B_QUIT_REQUESTED );
	}
	return window_count;
}

