//
// TPreferences
// Using BMessages to save user settings.
//
// Eric Shepherd, Be Inc.
// with additions by Joseph Beckenbach, BDH Software,
//	to fit it into the core BDH library set
//

#ifndef __TPREFS_H__
#define __TPREFS_H__
#include <app/Message.h>
#include <storage/Path.h>
#include <storage/File.h>

//------------------------------------------
// Standard shared library gibberish
#include "BdhBuild.h"
#undef _EXPIMP
#if _BUILDING_Bdh + 0 > 0
#define _EXPIMP _EXPORT
#else
#define _EXPIMP _IMPORT
#endif

#if __POWERPC__ 
#pragma export on
#endif
//------------------------------------------

class TPreferences : public BMessage {
	public:
					TPreferences(const char *filename);
					~TPreferences();
	status_t		InitCheck(void);
	
	status_t		SetBool(const char *name, bool b);
	status_t		SetInt8(const char *name, int8 i);
	status_t		SetInt16(const char *name, int16 i);
	status_t		SetInt32(const char *name, int32 i);
	status_t		SetInt64(const char *name, int64 i);
	status_t		SetFloat(const char *name, float f);
	status_t		SetDouble(const char *name, double d);
	status_t		SetString(const char *name, const char *string);
	status_t		SetPoint(const char *name, BPoint p);
	status_t		SetRect(const char *name, BRect r);
	status_t		SetMessage(const char *name, const BMessage *message);
	status_t		SetFlat(const char *name, const BFlattenable *obj);
	
	private:
	
	BPath			path;
	status_t		status;
};

inline status_t TPreferences::InitCheck(void) {
	return status;
}

#if __POWERPC__ 
#pragma export off
#endif 

#endif  // __TPREFS_H__
