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

/**************************************************************************
  the idea with this file is to create something similar to the ms-windows
  .ini files functions.
  it also demonstrates how ugly code using the genlist class looks.
  however the interface is nice. ie:
  section_file_lookup_string(file, "player%d.unit%d.name", plrno, unitno); 
***************************************************************************/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>

#include <registry.h>
#include <log.h>
#include <shared.h>

/**************************************************************************
...
**************************************************************************/
void section_file_init(struct section_file *file)
{
  genlist_init(&file->section_list);
}


/**************************************************************************
...
**************************************************************************/
void section_file_free(struct section_file *file)
{
  struct genlist_iterator myiter;

  genlist_iterator_init(&myiter, &file->section_list, 0);

  for(; ITERATOR_PTR(myiter); ) {
    struct genlist_iterator myiter2;
    struct section *psection=(struct section *)ITERATOR_PTR(myiter);

    genlist_iterator_init(&myiter2, &psection->entry_list, 0);
    for(; ITERATOR_PTR(myiter2);) {
      struct section_entry *pentry=
	(struct section_entry *)ITERATOR_PTR(myiter2);
      ITERATOR_NEXT(myiter2);
      free(pentry->name);
      free(pentry->svalue);
      genlist_unlink(&psection->entry_list, pentry);
      free(pentry);
    }

    ITERATOR_NEXT(myiter);
    free(psection->name);
    genlist_unlink(&file->section_list, psection);
    free(psection);
  }

}



/**************************************************************************
...
**************************************************************************/
int section_file_load(struct section_file *my_section_file, char *filename)
{
  FILE *fs;
  char buffer[512];
  int lineno, ival=0;
  struct section *current_section=0;

  if(!(fs=fopen(filename, "r")))
    return 0;


  genlist_init(&my_section_file->section_list);


  lineno=0;

  while(fgets(buffer, 512, fs)) {
    char *cptr;

    lineno++;

    for(cptr=buffer; *cptr && isspace(*cptr); cptr++);

    if(!*cptr || *cptr==';')
      continue;

    else if(*cptr=='[') {
      struct section *psection;

      for(++cptr; *cptr && *cptr!=']'; cptr++);
      
      if(!*cptr) {
	log(LOG_FATAL, "missing ] in %s - line %d", filename, lineno);
	exit(1);
      }

      *cptr='\0';

      psection=(struct section *)malloc(sizeof(struct section));
      psection->name=mystrdup(buffer+1);
      genlist_init(&psection->entry_list);
      genlist_insert(&my_section_file->section_list, psection, -1);

      current_section=psection;

    }
    else {
      char *pname, *pvalue;
      struct section_entry *pentry;

      pvalue=0;

      for(; *cptr && isspace(*cptr); cptr++);

      if(!*cptr)
	continue;

      pname=cptr;

      for(; *cptr && *cptr!='='; cptr++);
      
      if(!*cptr) {
	log(LOG_FATAL, "syntax error in %s - line %d", filename, lineno);
	exit(1);
      }

      
      *cptr++='\0';

      for(; *cptr && isspace(*cptr); cptr++);

      if(isdigit(*cptr) || *cptr=='-')
	ival=atoi(cptr);
      else if(*cptr=='\"') {
	pvalue=++cptr;
	for(; *cptr && *cptr!='\"'; cptr++);
	
	if(!*cptr) {
	  log(LOG_FATAL, "expected end of string in %s - line %d", filename, lineno);
	  exit(1);
	}

	*cptr='\0';

      }
      else {
	log(LOG_FATAL, "syntax error in %s - line %d", filename, lineno);
	exit(1);
      }


      if(!current_section) {
	log(LOG_FATAL, "entry defined before first section in %s - line %d", 
	    filename, lineno);
	exit(1);
      }

      
      pentry=(struct section_entry *)malloc(sizeof(struct section_entry));
      pentry->name=mystrdup(pname);
      
      if(pvalue)
	pentry->svalue=minstrdup(pvalue);
      else {
	pentry->ivalue=ival;
	pentry->svalue=0;
      }
      genlist_insert(&current_section->entry_list, pentry, -1); 

    }
  }

  fclose(fs);
  
  return 1;
}

/**************************************************************************
...
**************************************************************************/
int section_file_save(struct section_file *my_section_file, char *filename)
{
  FILE *fs;

  struct genlist_iterator myiter;

  if(!(fs=fopen(filename, "w")))
    return 0;


  genlist_iterator_init(&myiter, &my_section_file->section_list, 0);
  

  for(; ITERATOR_PTR(myiter); ITERATOR_NEXT(myiter)) {
    struct genlist_iterator myiter2;
    fprintf(fs, "\n[%s]\n",((struct section *)ITERATOR_PTR(myiter))->name);

    genlist_iterator_init(&myiter2, &((struct section *)
			       ITERATOR_PTR(myiter))->entry_list, 0);

    for(; ITERATOR_PTR(myiter2); ITERATOR_NEXT(myiter2)) {
      if(((struct section_entry *)ITERATOR_PTR(myiter2))->svalue)
	fprintf(fs, "%s=\"%s\"\n", 
		((struct section_entry *)ITERATOR_PTR(myiter2))->name,
		((struct section_entry *)ITERATOR_PTR(myiter2))->svalue);
      else
	fprintf(fs, "%s=%d\n", 
		((struct section_entry *)ITERATOR_PTR(myiter2))->name,
		((struct section_entry *)ITERATOR_PTR(myiter2))->ivalue);
    }


  }

  if(ferror(fs) || fclose(fs) == EOF)
    return 0;

  return 1;
}

/**************************************************************************
...
**************************************************************************/
char *secfile_lookup_str(struct section_file *my_section_file, char *path, ...)
{
  struct section_entry *entry;
  char buf[512];
  va_list ap;

  va_start(ap, path);
  vsprintf(buf, path, ap);
  va_end(ap);


  if(!(entry=section_file_lookup_internal(my_section_file, buf))) {
    log(LOG_FATAL, "sectionfile doesn't contain a '%s' entry", buf);
    exit(1);
  }

  if(!entry->svalue) {
    log(LOG_FATAL, "sectionfile entry '%s' doesn't contain a string", buf);
    exit(1);
  }
  
  return entry->svalue;
}

      

/**************************************************************************
...
**************************************************************************/
void secfile_insert_int(struct section_file *my_section_file,
			int val, char *path, ...)
{
  struct section_entry *pentry;
  char buf[512];
  va_list ap;

  va_start(ap, path);
  vsprintf(buf, path, ap);
  va_end(ap);

  pentry=section_file_insert_internal(my_section_file, buf);

  pentry->ivalue=val;
  pentry->svalue=0;
  
}



/**************************************************************************
...
**************************************************************************/
void secfile_insert_str(struct section_file *my_section_file,
			char *sval, char *path, ...)
{
  struct section_entry *pentry;
  char buf[512];
  va_list ap;

  va_start(ap, path);
  vsprintf(buf, path, ap);
  va_end(ap);

  pentry=section_file_insert_internal(my_section_file, buf);
  pentry->svalue=mystrdup(sval);
}



/**************************************************************************
...
**************************************************************************/
int secfile_lookup_int(struct section_file *my_section_file, 
		       char *path, ...)
{
  struct section_entry *entry;
  char buf[512];
  va_list ap;

  va_start(ap, path);
  vsprintf(buf, path, ap);
  va_end(ap);

  if(!(entry=section_file_lookup_internal(my_section_file, buf))) {
    log(LOG_FATAL, "sectionfile doesn't contain a '%s' entry", buf);
    exit(1);
  }

  if(entry->svalue) {
    log(LOG_FATAL, "sectionfile entry '%s' doesn't contain an integer", buf);
    exit(1);
  }
  
  return entry->ivalue;
}



/**************************************************************************
...
**************************************************************************/
int section_file_lookup(struct section_file *my_section_file, 
			char *path, ...)
{
  char buf[512];
  va_list ap;

  va_start(ap, path);
  vsprintf(buf, path, ap);
  va_end(ap);

  return !!section_file_lookup_internal(my_section_file, buf);
}





/**************************************************************************
...
**************************************************************************/
struct section_entry *section_file_lookup_internal(struct section_file 
						   *my_section_file,  
						   char *fullpath) 
						   
{
  char *pdelim;
  char sec_name[512];
  char ent_name[512];
  struct genlist_iterator myiter;


  if(!(pdelim=strchr(fullpath, '.'))) /* i dont like strtok */
    return 0;
  strncpy(sec_name, fullpath, pdelim-fullpath);
  sec_name[pdelim-fullpath]='\0';
  strcpy(ent_name, pdelim+1);

  genlist_iterator_init(&myiter, &my_section_file->section_list, 0);

  for(; ITERATOR_PTR(myiter); ITERATOR_NEXT(myiter))
    if(!strcmp(((struct section *)ITERATOR_PTR(myiter))->name, sec_name)) {
      struct genlist_iterator myiter2;
      genlist_iterator_init(&myiter2, &((struct section *)
				 ITERATOR_PTR(myiter))->entry_list, 0);
      for(; ITERATOR_PTR(myiter2); ITERATOR_NEXT(myiter2)) {
   	if(!strcmp(((struct section_entry *)ITERATOR_PTR(myiter2))->name, 
		   ent_name))
	  return (struct section_entry *)ITERATOR_PTR(myiter2);
      }
    }

  return 0;
}


/**************************************************************************
...
**************************************************************************/
struct section_entry *section_file_insert_internal(struct section_file 
						   *my_section_file, 
						   char *fullpath)
{
  char *pdelim;
  char sec_name[512];
  char ent_name[512];
  struct genlist_iterator myiter;
  struct section *psection;
  struct section_entry *pentry;


  if(!(pdelim=strchr(fullpath, '.'))) /* i dont like strtok */
    return 0;
  strncpy(sec_name, fullpath, pdelim-fullpath);
  sec_name[pdelim-fullpath]='\0';
  strcpy(ent_name, pdelim+1);


  genlist_iterator_init(&myiter, &my_section_file->section_list, 0);

  for(; ITERATOR_PTR(myiter); ITERATOR_NEXT(myiter))
    if(!strcmp(((struct section *)ITERATOR_PTR(myiter))->name, sec_name)) {

      struct genlist_iterator myiter2;
      genlist_iterator_init(&myiter2, &((struct section *)
				 ITERATOR_PTR(myiter))->entry_list, 0);
      for(; ITERATOR_PTR(myiter2); ITERATOR_NEXT(myiter2)) {
   	if(!strcmp(((struct section_entry *)ITERATOR_PTR(myiter2))->name, 
		   ent_name))
	  return (struct section_entry *)ITERATOR_PTR(myiter2);
      }

      pentry=(struct section_entry *)malloc(sizeof(struct section_entry));
      pentry->name=mystrdup(ent_name);
      genlist_insert(&((struct section *)
			    ITERATOR_PTR(myiter))->entry_list, pentry, -1);

      return pentry;
    }


  psection=(struct section *)malloc(sizeof(struct section));
  psection->name=mystrdup(sec_name);
  genlist_init(&psection->entry_list);
  genlist_insert(&my_section_file->section_list, psection, -1);
  
  pentry=(struct section_entry *)malloc(sizeof(struct section_entry));
  pentry->name=mystrdup(ent_name);
  genlist_insert(&psection->entry_list, pentry, -1);

  return pentry;
}







