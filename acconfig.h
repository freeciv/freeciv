#ifndef FC_CONFIG_H
#define FC_CONFIG_H
@TOP@

/*
 *   Defaults for configuration settings
 *   by Falk Hüffner <falk.hueffner@student.uni-tuebingen.de>
 *   and others
 *
 *   Need to mention HAVE_lib here when using FC_CHECK_X_LIB
 *   instead of AC_CHECK_LIB:  --dwp
 *
 *   Note PACKAGE and VERSION are now done automatically by
 *   autoheader  --dwp
 *
 *   ALWAYS_ROOT and following correspond to entries used by
 *   hand-written config.h files, and are not set by configure.
 */

#undef MAJOR_VERSION
#undef MINOR_VERSION
#undef PATCH_VERSION
#undef IS_BETA_VERSION
#undef VERSION_STRING
#undef FREECIV_DATADIR
#undef HAVE_LIBICE
#undef HAVE_LIBSM
#undef HAVE_LIBX11
#undef HAVE_LIBXAW
#undef HAVE_LIBXAW3D
#undef HAVE_LIBXEXT
#undef HAVE_LIBXMU
#undef HAVE_LIBXPM
#undef HAVE_LIBXT
#undef ENABLE_NLS
#undef HAVE_CATGETS
#undef HAVE_GETTEXT
#undef HAVE_LC_MESSAGES
#undef HAVE_STPCPY
#undef LOCALEDIR
#undef ALWAYS_ROOT
#undef STRICT_WINDOWS
#undef GENERATING_MAC
#undef HAVE_OPENTRANSPORT

@BOTTOM@

#endif /* FC_CONFIG_H */
