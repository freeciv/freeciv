/*
	BdhBuild.h
	Copyright (c) 1998 Be Do Have Software.  All rights reserved.
 */
#ifndef BDH_BUILD_H
#define BDH_BUILD_H 1

//------------------------------------------
// Standard shared library gibberish
#include <BeBuild.h>
#undef _EXPIMP
#if __INTEL__ && __GNUC__
#undef  _EXPORT
#define _EXPORT
#undef  _IMPORT
#define _IMPORT
#endif

#endif // BDH_BUILD_H
