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

#ifdef HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif
#ifdef HAVE_SYS_SOCKET_H
#include <sys/socket.h>
#endif
#include <unistd.h>
#include <errno.h>

/* utility */
#include "capability.h"
#include "fcintl.h"
#include "log.h"
#include "netintf.h"
#include "registry.h"

/* modinst */
#include "download.h"


#define PROTOCOL_STRING "HTTP/1.1 "
#define UNKNOWN_CONTENT_LENGTH -1


static char *lookup_header_entry(char *headers, char *entry)
{
  int i, j;
  int len = strlen(entry);

  for (i = 0; headers[i] != '\0' ; i += j) {
    for (j = 0; headers[i + j] != '\0' && headers[i + j] != '\n'; j++) {
      /* Nothing, we just searched for end of line */
    }
    if (headers[i + j] == '\n') {
      if (!strncmp(&headers[i + j + 1], entry, len)) {
        return &headers[i + j + 1 + len];
      }

      /* Get over newline character */
      j++;
    }
  }

  return NULL;
}

static bool download_file(const char *URL, const char *local_filename)
{
  const char *path;
  char srvname[MAX_LEN_ADDR];
  int port;
  static union fc_sockaddr addr;
  int sock;
  char buf[50000];
  char hdr_buf[2048];
  int hdr_idx = 0;
  int msglen;
  int result;
  FILE *fp;
  int header_end_chars = 0;
  int clen = UNKNOWN_CONTENT_LENGTH;
  int cread = 0;

  path = fc_lookup_httpd(srvname, &port, URL);
  if (path == NULL) {
    return FALSE;
  }

  if (!net_lookup_service(srvname, port, &addr, FALSE)) {
    return FALSE;
  }

  sock = socket(addr.saddr.sa_family, SOCK_STREAM, 0);
  if (sock == -1) {
    return FALSE;
  }

  if (fc_connect(sock, &addr.saddr, sockaddr_size(&addr)) == -1) {
    fc_closesocket(sock);
    return FALSE;
  }

  msglen = fc_snprintf(buf, sizeof(buf),
                       "GET %s HTTP/1.1\r\n"
                       "HOST: %s:%d\r\n"
                       "User-Agent: Freeciv-modpack/%s\r\n"
                       "Content-Length: 0\r\n"
                       "\r\n",
                       path, srvname, port, VERSION_STRING);
  if (fc_writesocket(sock, buf, msglen) != msglen) {
    fc_closesocket(sock);
    return FALSE;
  }

  fp = fopen(local_filename, "w+b");
  if (fp == NULL) {
    fc_closesocket(sock);
    return FALSE;
  }

  result = 1;
  while (result && (clen == UNKNOWN_CONTENT_LENGTH || clen > cread)) {
    result = fc_readsocket(sock, buf, sizeof(buf));

    if (result < 0) {
      if (errno != EAGAIN && errno != EINTR && errno != EWOULDBLOCK) {
        fc_closesocket(sock);
        return FALSE;
      }
      fc_usleep(1000000);
    } else if (result > 0) {
      int left;
      int total_written = 0;
      int i;

      for (i = 0; i < result && header_end_chars < 4; i++) {
        if (hdr_idx < sizeof(hdr_buf)) {
          hdr_buf[hdr_idx++] = buf[i];
        }
        switch (header_end_chars) {
         case 0:
         case 2:
           if (buf[i] == '\r') {
             header_end_chars++;
           } else {
             header_end_chars =  0;
           }
           break;
         case 1:
         case 3:
           if (buf[i] == '\n') {
             header_end_chars++;
             if (header_end_chars >= 3) {
               if (hdr_idx < sizeof(hdr_buf)) {
                 int httpret;
                 char *clenstr;
                 int protolen = strlen(PROTOCOL_STRING);

                 hdr_buf[hdr_idx] = '\0';

                 if (strncmp(hdr_buf, PROTOCOL_STRING, protolen)) {
                   /* Not valid HTTP header */
                   fclose(fp);
                   fc_closesocket(sock);
                   return FALSE;
                 }
                 httpret = atoi(hdr_buf + protolen);

                 if (httpret != 200) {
                   /* Error document */
                   fclose(fp);
                   fc_closesocket(sock);
                   return FALSE;
                 }

                 clenstr = lookup_header_entry(hdr_buf, "Content-Length: ");
                 if (clenstr != NULL) {
                   clen = atoi(clenstr);
                 }
               } else {
                 /* Too long header */
                 fclose(fp);
                 fc_closesocket(sock);
                 return FALSE;
               }
             }
           } else {
             header_end_chars =  0;
           }
           break;
        }
      }

      left = result - i;
      total_written = i;

      if (left > 0) {
        cread += left;
      }

      while (left > 0) {
        int written;

        written = fwrite(buf + total_written, 1, left, fp);
        if (written <= 0) {
          fclose(fp);
          fc_closesocket(sock);
          return FALSE;
        }
        left -= written;
        total_written += written;
      }
    }
  }

  fclose(fp);

  fc_closesocket(sock);
  return TRUE;
}

/**************************************************************************
  Return path to control directory
**************************************************************************/
static char *control_dir(void)
{
  const char *home;
  static char controld[500] = { '\0' };

  if (controld[0] != '\0') {
    return controld;
  }

  home = user_home_dir();
  if (home == NULL) {
    return NULL;
  }

  fc_snprintf(controld, sizeof(controld),
              "%s/.freeciv/" DATASUBDIR "/control",
              home);

  return controld;
}

/**************************************************************************
  Download modpack from a given URL
**************************************************************************/
const char *download_modpack(const char *URL,
                             dl_msg_callback mcb,
                             dl_pb_callback pbcb)
{
  char *controld;
  char local_name[2048];
  const char *home;
  int start_idx;
  int filenbr, total_files;
  struct section_file *control;
  const char *control_capstr;
  const char *baseURL;
  char fileURL[2048];
  const char *src_name;
  bool partial_failure = FALSE;

  if (URL == NULL || URL[0] == '\0') {
    return _("No URL given");
  }

  if (strlen(URL) < strlen(MODPACK_SUFFIX)
      || strcmp(URL + strlen(URL) - strlen(MODPACK_SUFFIX), MODPACK_SUFFIX)) {
    return _("This does not look like modpack URL");
  }

  for (start_idx = strlen(URL) - strlen(MODPACK_SUFFIX);
       start_idx > 0 && URL[start_idx - 1] != '/';
       start_idx--) {
    /* Nothing */
  }

  log_normal(_("Installing modpack %s from %s"), URL + start_idx, URL);

  home = user_home_dir();
  if (home == NULL) {
    return _("Cannot determine user home directory");
  }

  controld = control_dir();

  if (controld == NULL) {
    return _("Cannot determine control directory");
  }

  if (!make_dir(controld)) {
    return _("Cannot create required directories");
  }

  fc_snprintf(local_name, sizeof(local_name), "%s/%s", controld, URL + start_idx);

  if (mcb != NULL) {
    mcb(_("Downloading modpack control file."));
  }
  if (!download_file(URL, local_name)) {
    return _("Failed to download modpack control file from given URL");
  }

  control = secfile_load(local_name, FALSE);

  if (control == NULL) {
    return _("Cannot parse modpack control file");
  }

  control_capstr = secfile_lookup_str(control, "info.options");
  if (control_capstr == NULL) {
    secfile_destroy(control);
    return _("Modpack control file has no capability string");
  }

  if (!has_capabilities(MODPACK_CAPSTR, control_capstr)) {
    log_error("Incompatible control file:");
    log_error("  control file options: %s", control_capstr);
    log_error("  supported options:    %s", MODPACK_CAPSTR);

    secfile_destroy(control);

    return _("Modpack control file is incompatible");  
  }

  baseURL = secfile_lookup_str(control, "info.baseURL");

  total_files = 0;
  do {
    src_name = secfile_lookup_str_default(control, NULL,
                                          "files.list%d.src", total_files);

    if (src_name != NULL) {
      total_files++;
    }
  } while (src_name != NULL);

  if (pbcb != NULL) {
    /* Control file already downloaded */
    double fraction = (double) 1 / (total_files + 1);

    pbcb(fraction);
  }

  filenbr = 0;
  for (filenbr = 0; filenbr < total_files; filenbr++) {
    const char *dest_name;
    int i;
    bool illegal_filename = FALSE;

    src_name = secfile_lookup_str_default(control, NULL,
                                          "files.list%d.src", filenbr);

    dest_name = secfile_lookup_str_default(control, NULL,
                                           "files.list%d.dest", filenbr);

    if (dest_name == NULL || dest_name[0] == '\0') {
      /* Missing dest name is ok, we just default to src_name */
      dest_name = src_name;
    }

    for (i = 0; dest_name[i] != '\0'; i++) {
      if (dest_name[i] == '.' && dest_name[i+1] == '.') {
        if (mcb != NULL) {
          char buf[2048];

          fc_snprintf(buf, sizeof(buf), _("Illegal path for %s"),
                      dest_name);
          mcb(buf);
        }
        partial_failure = TRUE;
        illegal_filename = TRUE;
      }
    }

    if (!illegal_filename) {
      fc_snprintf(local_name, sizeof(local_name),
                  "%s/.freeciv/" DATASUBDIR "/%s",
                  home, dest_name);
      for (i = strlen(local_name) - 1 ; local_name[i] != '/' ; i--) {
        /* Nothing */
      }
      local_name[i] = '\0';
      if (!make_dir(local_name)) {
        secfile_destroy(control);
        return _("Cannot create required directories");
      }
      local_name[i] = '/';

      if (mcb != NULL) {
        char buf[2048];

        fc_snprintf(buf, sizeof(buf), _("Downloading %s"), src_name);
        mcb(buf);
      }

      fc_snprintf(fileURL, sizeof(fileURL), "%s/%s", baseURL, src_name);
      if (!download_file(fileURL, local_name)) {
        if (mcb != NULL) {
          char buf[2048];

          fc_snprintf(buf, sizeof(buf), _("Failed to download %s"),
                      src_name);
          mcb(buf);
        }
        partial_failure = TRUE;
      }
    }

    if (pbcb != NULL) {
      /* Count download of control file also */
      double fraction = (double)(filenbr + 2) / (total_files + 1);

      pbcb(fraction);
    }
  }

  secfile_destroy(control);

  if (partial_failure) {
    return _("Some parts of the modpack failed to install.");
  }

  return NULL;
}

/**************************************************************************
  Download modpack list
**************************************************************************/
const char *download_modpack_list(const char *URL, modpack_list_setup_cb cb,
                                  dl_msg_callback mcb)
{
  const char *controld = control_dir();
  char local_name[2048];
  struct section_file *list_file;
  const char *list_capstr;
  int modpack_count;
  const char *msg;
  const char *mp_name;

  if (controld == NULL) {
    return _("Cannot determine control directory");
  }

  if (!make_dir(controld)) {
    return _("Cannot create required directories");
  }

  fc_snprintf(local_name, sizeof(local_name), "%s/modpack.list", controld);

  if (!download_file(URL, local_name)) {
    return _("Failed to download modpack list");
  }

  list_file = secfile_load(local_name, FALSE);

  if (list_file == NULL) {
    return _("Cannot parse modpack list");
  }

  list_capstr = secfile_lookup_str(list_file, "info.options");
  if (list_capstr == NULL) {
    secfile_destroy(list_file);
    return _("Modpack list has no capability string");
  }

  if (!has_capabilities(MODLIST_CAPSTR, list_capstr)) {
    log_error("Incompatible modpack list file:");
    log_error("  list file options: %s", list_capstr);
    log_error("  supported options: %s", MODLIST_CAPSTR);

    secfile_destroy(list_file);

    return _("Modpack list is incompatible");  
  }

  msg = secfile_lookup_str_default(list_file, NULL, "info.message");

  if (msg != NULL) {
    mcb(msg);
  }

  modpack_count = 0;
  do {
    const char *mpURL;
    const char *mpver;
    const char *mp_type_str;

    mp_name = secfile_lookup_str_default(list_file, NULL,
                                         "modpacks.list%d.name", modpack_count);
    mpver = secfile_lookup_str_default(list_file, NULL,
                                       "modpacks.list%d.version",
                                       modpack_count);
    mp_type_str = secfile_lookup_str_default(list_file, NULL,
                                             "modpacks.list%d.type",
                                             modpack_count);
    mpURL = secfile_lookup_str_default(list_file, NULL,
                                       "modpacks.list%d.URL", modpack_count);

    if (mp_name != NULL && mpURL != NULL) {
      enum modpack_type type = modpack_type_by_name(mp_type_str, fc_strcasecmp);
      if (!modpack_type_is_valid(type)) {
        log_error("Illegal modpack type \"%s\"", mp_type_str ? mp_type_str : "NULL");
      }
      if (mpver == NULL) {
        mpver = "-";
      }
      cb(mp_name, mpURL, mpver, type);
      modpack_count++;
    }
  } while (mp_name != NULL);

  return NULL;
}
