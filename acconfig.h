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
 */

#undef PACKAGE
#undef VERSION
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

@BOTTOM@

#endif /* FC_CONFIG_H */
