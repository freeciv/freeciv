/********************************************************************** 
 Freeciv - Copyright (C) 1996 - A Kjeldberg, L Gregersen, P Unold
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
***********************************************************************/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <curl/curl.h>

/* utility */
#include "ioz.h"
#include "rand.h"
#include "registry.h"

#include "netfile.h"

/* Consecutive transfers can use same handle for better performance */
static CURL *handle = NULL;

/********************************************************************** 
  Fetch section file from net
***********************************************************************/
struct section_file *netfile_get_section_file(const char *URL,
                                              nf_errmsg cb, void *data)
{
  struct section_file *out = NULL;
  fz_FILE *file;
  FILE *fp;
  CURLcode curlret;

  if (handle == NULL) {
    handle = curl_easy_init();
  }

  curl_easy_setopt(handle, CURLOPT_URL, URL);

#ifdef WIN32_NATIVE
  {
    char filename[MAX_PATH];

    GetTempPath(sizeof(filename), filename);
    cat_snprintf(filename, sizeof(filename), "fctmp%d", fc_rand(1000));

    fp = fc_fopen(filename, "w+b");
  }
#else /* WIN32_NATIVE */
  fp = tmpfile();
#endif /* WIN32_NATIVE */

  if (fp == NULL) {
    if (cb != NULL) {
      cb(_("Could not open temp file."), data);
    }
    return NULL;
  }

  curl_easy_setopt(handle, CURLOPT_WRITEDATA, fp);

  curlret = curl_easy_perform(handle);

  if (curlret == CURLE_OK) {
    rewind(fp);

    file = fz_from_stream(fp);

    out = secfile_from_stream(file, TRUE);
  } else {

    fclose(fp);

    if (cb != NULL) {
      char buf[2048];

      fc_snprintf(buf, sizeof(buf),
                  _("Failed to fetch %s"), URL);
      cb(buf, data);
    }
  }

  return out;
}
