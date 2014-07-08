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
#include <fc_config.h>
#endif

/* libxml2 */
#ifdef HAVE_XML_REGISTRY
#include <libxml/parser.h>
#endif /* HAVE_XML_REGISTRY */

#include "registry_xml.h"

#include "registry.h"

/*************************************************************************
  Initialize registry module
*************************************************************************/
void registry_module_init(void)
{
#ifdef HAVE_XML_REGISTRY
  LIBXML_TEST_VERSION;
#endif /* HAVE_XML_REGISTRY */
}

/*************************************************************************
  Closes registry module
*************************************************************************/
void registry_module_close(void)
{
#ifdef HAVE_XML_REGISTRY
  xmlCleanupParser();
#endif /* HAVE_XML_REGISTRY */
}

/**************************************************************************
  Create a section file from a file.  Returns NULL on error.
**************************************************************************/
struct section_file *secfile_load(const char *filename,
                                  bool allow_duplicates)
{
#ifdef HAVE_XML_REGISTRY
  xmlDoc *sec_doc;

  sec_doc = xmlReadFile(filename, NULL, XML_PARSE_NOERROR);
  if (sec_doc != NULL) {
    return xmlfile_load(sec_doc, filename);
  }
#endif /* HAVE_XML_REGISTRY */

  return secfile_load_section(filename, NULL, allow_duplicates);
}
