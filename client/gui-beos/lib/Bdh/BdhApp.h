//
//	BdhApp.h
//	Copyright 1996-1998 Be Do Have Software. All Rights Reserved.
//
#ifndef BDH_APP_H
#define BDH_APP_H 1

#include <Application.h>
#include <app/Roster.h>
#include <interface/Bitmap.h>
#include "TPreferences.h"

class BWindow;

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


class _EXPIMP BdhApp : public BApplication
{
public:
		BdhApp(const char*);
	virtual	~BdhApp();
	virtual void ReadyToRun(void);

// new stuff we provide
	const char*		appDirpath(void) { return dirpath; }
	const char*		appName(   void) { return appname; }
	const char*		appSignature(  void) { return appsig; }
	const entry_ref*	appRef(    void) { return &(appInfo.ref); }
	TPreferences*		appPrefs(  void) { return pref; }
	
	void			GotoUrl( const char* url );
	virtual void		HelpRequested( void );
	BBitmap*		miniIcon( void);
	BBitmap*		largeIcon(void);

	uint	AddWindow(    const BWindow *win );
	uint	RemoveWindow( const BWindow *win );

private:
	app_info	appInfo;
	char		*dirpath;
	char		*appname;
	char		*appsig;
	uint		window_count;
public:
	TPreferences	*pref;
};

_EXPIMP extern const char*	appDirpath(void);
_EXPIMP extern BdhApp*		bdhApp(void);



#if __POWERPC__ 
#pragma export off
#endif
#endif // BDH_APP_H
