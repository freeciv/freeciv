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

/**************************************************************************
  Description of the file format:  (description by David Pfitzner
  (dwp@mso.anu.edu.au), not an original author of the code)
  
  - Whitespace lines are ignored, as are lines where the first
  non-whitespace character is ";" (comment lines).
  Optionally '#' can also be used for comments. (new)
  
  - A line with "[name]" labels the start of a section with
  that name; one of these must be the first non-comment line in the file.
  Any spaces within the brackets are included in the name, which
  is probably not a good idea.

  - Within a section, lines have one of the following forms:
      subname="stringvalue"
      subname=-digits
      subname=digits
    for a value with given name and string, negative integer, and
    positive integer values, respectively.  These entries are
    referenced in the following functions as "sectionname.subname".
    The section name should not contain any dots ('.'); the subname
    can, but they have no particular significance.
    There can be optional whitespace before and/or after the equals
    sign (partly new: previously only after).
    
  Backslash is an escape character in strings (double-quoted strings
  only, not names); recognised escapes are \n, \\, and \".
  (Any other \<char> is just treated as <char>.)

  The above is the basic savefile format as originally implemented.
  The following modifications/extensions are by dwp:

  - Comments: originally any extra characters on the above lines
  were ignored.  Now you should explicitly use the comment character ';'

  - Vector format:
  The line:
      foo= 10, 11, "x"
  is equivalent to the following original-format lines:
      foo=10
      foo,1=11
      foo,2="x"
  As in the example, in principle you can mix integers and strings,
  but the calling program will probably require elements to be the
  same type.   Note that the first element of a vector is not "foo,0",
  in order that the name of the first element is the same whether or
  not there are subsequent elements.  However as a convenience, if
  you try to lookup "foo,0" then you get back "foo".  (So you should
  never have "foo,0" as a real name in the datafile.)

  - Tabular format:
  This was introduced to allow tabular-style data which humans can
  more easily parse and edit (for rulesets).  As a bonus, it also
  makes savefiles smaller.
  The following lines:
      foo={ "bar","baz","bax"
      "wow",    10,  -5
      "cool",  "str"
      "hmm",   314,  99, 33, 11
      }
  are equivalent to the following original-format lines:
      foo0.bar="wow"
      foo0.baz=10
      foo0.bax=-5
      foo1.bar="cool"
      foo1.baz="str"
      foo2.bar="hmm"
      foo2.baz=314
      foo2.bax=99
      foo2.bax,1=33
      foo2.bax,2=11
  The first line specifies the base name and the column names, and the
  subsequent lines have data.  Again while it is possible to mix string
  and integer values in a column, and have either more or less values
  in a row than there are column headings, the code which uses this
  information (via the registry) may set more stringent conditions.
  If a row has more entries than column headings, the last column
  is treated as a vector (as above).

  The equivalence above between the new and old formats is fairly
  direct: internally, data is converted to the old format.
  In principle it could be a good idea to represent the data
  as a table (2-d array) internally, but the current method
  seems sufficient and relatively simple...
  
  There is a limited ability to save data in the new format:
  So long as the section_file is constructed in an expected way,
  tabular data (with no missing or extra values) can be saved
  in tabular form.  (See section_file_save().)
  
  With the new formats, it may be useful to have a line-continuation
  mechanism. (?)  But that has not been implemented yet.
  
***************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>

#include "log.h"
#include "mem.h"
#include "shared.h"

#include "registry.h"

/**************************************************************************
  Hashing registry lookups: (by dwp)
  - Have a hash table direct to entries, bypassing sections division.
  - For convenience, store the key (the full section+entry name)
    in the hash table (some memory overhead).
  - The number of entries is fixed when the hash table is built.
  - Use closed hashing with simple collision resolution.
  - Should have implemented abstract hash table and used it here,
    rather than tie hashing closely to section_file...
**************************************************************************/
#define DO_HASH 1
#define HASH_DEBUG 1		/* 0,1,2 */

#define SAVE_TABLES 1		/* set to 0 for old-style savefiles */

struct hash_entry {
  struct section_entry *data;
  char *key_val;		/* including section prefix */
  int hash_val;			/* not really necessary, but may reduce
				   key comparisons? */
};

struct hash_data {
  struct hash_entry *table;
  int num_buckets;
  int num_entries_hashbuild;
  int num_collisions;
};

static int hashfunc(char *name, int num_buckets);
static int secfilehash_hashash(struct section_file *file);
static void secfilehash_check(struct section_file *file);
static struct hash_entry *secfilehash_lookup(struct section_file *file,
					     char *key, int *hash_return);
static void secfilehash_insert(struct section_file *file,
			       char *key, struct section_entry *data);
static void secfilehash_build(struct section_file *file);
static void secfilehash_free(struct section_file *file);
static char *minstrdup(struct sbuffer *sb, char *str);
static char *moutstr(char *str);


struct flat_entry_list {
  struct section_entry **plist;	/* list of pointers to individual mallocs */
  int n;			/* number used */
  int n_alloc;			/* number allocated */
};


static void parse_commalist(struct flat_entry_list *entries, char *buffer,
			    char *filename, int lineno, struct sbuffer *sb);


/**************************************************************************
...
**************************************************************************/
void section_file_init(struct section_file *file)
{
  genlist_init(&file->section_list);
  file->num_entries = 0;
  file->hashd = NULL;
  file->sb = sbuf_new();
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
      genlist_unlink(&psection->entry_list, pentry);
    }

    ITERATOR_NEXT(myiter);
    genlist_unlink(&file->section_list, psection);
  }
  if(secfilehash_hashash(file)) {
    secfilehash_free(file);
  }

  sbuf_free(file->sb);
  file->sb = NULL;
}

/**************************************************************************
  Print log messages for any entries in the file which have
  not been looked up -- ie, unused or unrecognised entries.
  To mark an entry as used without actually doing anything with it,
  you could do something like:
     section_file_lookup(&file, "foo.bar");  / * unused * /
**************************************************************************/
void section_file_check_unused(struct section_file *file, char *filename)
{
  struct genlist_iterator myiter, myiter2;
  struct section *psection;
  struct section_entry *pentry;
  int any = 0;

  genlist_iterator_init(&myiter, &file->section_list, 0);

  for(; ITERATOR_PTR(myiter); ITERATOR_NEXT(myiter)) {
    psection = (struct section *)ITERATOR_PTR(myiter);
    genlist_iterator_init(&myiter2, &psection->entry_list, 0);
    for(; ITERATOR_PTR(myiter2); ITERATOR_NEXT(myiter2)) {
      pentry = (struct section_entry *)ITERATOR_PTR(myiter2);
      if (!pentry->used) {
	if (any==0 && filename!=NULL) {
	  freelog(LOG_NORMAL, "Unused entries in file %s:", filename);
	  any = 1;
	}
	freelog(LOG_NORMAL, "  unused entry: %s.%s", psection->name, pentry->name);
      }
    }
  }
}

/**************************************************************************
  Parse a comma-delimited list of strings and integers.

  "buffer" is a line of data, which should be a comma-separated
  list of strings (with double-quotes) and simple integers.
  Whitespace between entries is ok.
  We mess with the buffer by inserting nuls.
  
  The section entry structs in "entries" are individually malloced,
  so they can be used in the other registry structs and then freed
  later without problems.  (Though now strbuffermalloc is used,
  so this is less important.)  "entries" should be consistent on
  input so we can realloc list->tab as necessary.  (We use realloc
  for this instead of the strbuffer stuff, as this is reused many
  times during loading the datafile.)
  
  In the section_entry structs we fill in ivalue and svalue,
  but not name (which is set to NULL).  svalue, if used, is 
  strbuffermalloc-ed memory.
  (As usual svalue==NULL implies integer, else string.)
  
  The number of values is returned in list->n; there should be
  at least one value, and trailing commas are not permitted.
  
  "filename" and "lineno" are for error reporting.
**************************************************************************/
static void parse_commalist(struct flat_entry_list *entries, char *buffer,
			    char *filename, int lineno, struct sbuffer *sb)
{
  char *cptr=buffer, *start;
  struct section_entry *this_entry;
  int end=0;

  entries->n = 0;
  do {
    /* skip initial whitespace, to get to start of next value: */
    for(; *cptr && isspace(*cptr); cptr++);
    if(!*cptr) {
      /* could just "break" here to allow trailing commas */
      freelog(LOG_FATAL, "missing expected value in %s - line %d",
	   filename, lineno);
      exit(1);
    }

    /* prepare for next entry: */
    entries->n++;
    this_entry = sbuf_malloc(sb, sizeof(struct section_entry));
    if (entries->n > entries->n_alloc) {
      entries->n_alloc = 2*entries->n+1;
      entries->plist = fc_realloc(entries->plist,
				  entries->n_alloc*sizeof(struct section_entry*));
    }
    entries->plist[entries->n-1] = this_entry;
    this_entry->name = NULL;
    this_entry->used = 0;

    /* deal with integer or string */
    if(isdigit(*cptr) || *cptr=='-') {
      
      /* integer: skip more digits: */
      start=cptr;
      cptr++;
      for(; *cptr && isdigit(*cptr); cptr++);
      
      /* skip optional trailing whitespace and check for end or comma: */
      for(; *cptr && isspace(*cptr); cptr++);
      if(*cptr=='\0' || *cptr==';' || *cptr=='#') {
	end=1;
      } else if (*cptr!=',') {
	freelog(LOG_FATAL, "junk after integer in %s - line %d",
	     filename, lineno);
	exit(1);
      } else {
	cptr++;			/* skip comma */
      }

      this_entry->ivalue=atoi(start);
      this_entry->svalue=NULL;
      
    } else if(*cptr == '\"') {

      /* string: find next non-escaped double-quotes and terminate
       * the sub-string */
      start=++cptr;
      for(; *cptr && (*cptr!='\"' || *(cptr-1)=='\\'); cptr++);
      if (!*cptr) {
	freelog(LOG_FATAL, "missing end of string in %s - line %d",
	     filename, lineno);
	exit(1);
      }
      *cptr++='\0';
      
      /* skip optional trailing whitespace and check for end or comma: */
      for(; *cptr && isspace(*cptr); cptr++);
      if(*cptr=='\0' || *cptr==';' || *cptr=='#') {
	end=1;
      } else if (*cptr!=',') {
	freelog(LOG_FATAL, "junk after string in %s - line %d",
	     filename, lineno);
	exit(1);
      } else {
	cptr++;			/* skip comma */
      }

      this_entry->svalue=minstrdup(sb, start);
      this_entry->ivalue=0;
      
    } else {
      freelog(LOG_FATAL, "syntax error at value %d in %s - line %d",
	   entries->n, filename, lineno);
      exit(1);
    }
    
  } while(!end);
}

/**************************************************************************
...
**************************************************************************/
int section_file_load(struct section_file *my_section_file, char *filename)
{
  FILE *fs;
  char buffer[512], temp_name[512];
  int lineno;
  struct section *current_section=NULL;
  int table_state=0;		/* 1 when within tabular format */
  int table_lineno=0;		/* row number in tabular, 0=top data row */
  char *table_basename=NULL;	/* reuse, so normal malloc */
  struct flat_entry_list columns = {NULL, 0, 0};
  struct flat_entry_list entries = {NULL, 0, 0};
  struct sbuffer *sb;
  int i;

  if(!(fs=fopen(filename, "r"))) {
    freelog(LOG_NORMAL, "Could not open file \"%s\"", filename);
    return 0;
  }

  section_file_init(my_section_file);
  sb = my_section_file->sb;
  lineno=0;

  while(fgets(buffer, sizeof(buffer), fs)) {
    char *cptr;

    if (strlen(buffer)==sizeof(buffer)-1) {
      freelog(LOG_FATAL, "line too long in %s - line %d", filename, lineno);
      exit(1);
    }
    lineno++;

    for(cptr=buffer; *cptr && isspace(*cptr); cptr++);

    if(*cptr=='\0' || *cptr==';' || *cptr=='#') {
      continue;			/* blank line or comment line */

    } else if(*cptr=='[') {
      struct section *psection;
      char *sname;

      if (table_state) {
	freelog(LOG_FATAL, "new section during table in %s - line %d",
	     filename, lineno);
	exit(1);
      }
      sname = cptr+1;
      for(++cptr; *cptr && *cptr!=']'; cptr++);
      if(!*cptr) {
	freelog(LOG_FATAL, "missing ] in %s - line %d", filename, lineno);
	exit(1);
      }
      *cptr='\0';

      psection = sbuf_malloc(sb, sizeof(struct section));
      psection->name = sbuf_strdup(sb, sname);
      genlist_init(&psection->entry_list);
      genlist_insert(&my_section_file->section_list, psection, -1);

      current_section=psection;

    } else if(!current_section) {
      
      freelog(LOG_FATAL, "entry defined before first section in %s - line %d", 
	   filename, lineno);
      exit(1);
      
    } else if(*cptr=='}') {

      if (!table_state) {
	freelog(LOG_FATAL, "misplaced \"}\" in %s - line %d", filename, lineno);
	exit(1);
      }
      free(table_basename);
      table_basename=NULL;
      table_state=0;

    } else if(table_state) {

      parse_commalist(&entries, cptr, filename, lineno, sb);
      for(i=0; i<entries.n; i++) {
	if (i<columns.n) {
	  sprintf(temp_name,"%s%d.%s", table_basename, table_lineno,
		  columns.plist[i]->svalue);
	} else {
	  sprintf(temp_name,"%s%d.%s,%d", table_basename, table_lineno,
		  columns.plist[columns.n-1]->svalue, i-columns.n+1);
	}
	entries.plist[i]->name = sbuf_strdup(sb, temp_name);
	genlist_insert(&current_section->entry_list, entries.plist[i], -1); 
	my_section_file->num_entries++;
      }
      table_lineno++;
      
    } else {
      char *pname=cptr;

      /* Name is ended by a space or equals sign: */
      for(; *cptr && (*cptr!='=' && !isspace(*cptr)); cptr++);
      if(!*cptr) {
	freelog(LOG_FATAL, "syntax error in %s - line %d", filename, lineno);
	exit(1);
      }
      if (*cptr=='=') {
	*cptr++='\0';
      } else {
	*cptr++='\0';
	/* find the equals sign: */
	for(; *cptr && *cptr!='='; cptr++);
	if(!*cptr) {
	  freelog(LOG_FATAL, "syntax error in %s - line %d", filename, lineno);
	  exit(1);
	}
	cptr++;
      }
      /* skip any space after the equals: */
      for(; *cptr && isspace(*cptr); cptr++);
      
      if(isdigit(*cptr) || *cptr=='-' || *cptr=='\"') {
	
	parse_commalist(&entries, cptr, filename, lineno, sb);
	for(i=0; i<entries.n; i++) {
	  if (i==0) {
	    entries.plist[0]->name = sbuf_strdup(sb, pname);
	  } else {
	    sprintf(temp_name,"%s,%d", pname, i);
	    entries.plist[i]->name = sbuf_strdup(sb, temp_name);
	  }
	  genlist_insert(&current_section->entry_list, entries.plist[i], -1); 
	  my_section_file->num_entries++;
	}
	
      } else if(*cptr=='{') {
	
	table_basename = mystrdup(pname);
	parse_commalist(&columns, cptr+1, filename, lineno, sb);
	for(i=0; i<columns.n; i++) {
	  if( !columns.plist[i]->svalue ) {
	    freelog(LOG_FATAL, "table format entry non-string in %s - line %d",
		 filename, lineno);
	    exit(1);
	  } 
	}
	table_state=1;
	table_lineno=0;
      }	
    }
  }
  if (table_state) {
    freelog(LOG_FATAL, "finished file %s before end of table\n", filename);
    exit(1);
  }

  fclose(fs);
  free(columns.plist);
  free(entries.plist);
  
  if(DO_HASH) {
    secfilehash_build(my_section_file);
  }
    
  return 1;
}

/**************************************************************************
 Save the previously filled in section_file to disk.
 
 There is now limited ability to save in the new tabular format
 (to give smaller savefiles).
 The start of a table is detected by an entry with name of the form:
    (alphabetical_component)(zero)(period)(alphanumeric_component)
 Eg: u0.id, or c0.id, in the freeciv savefile.
 The alphabetical component is taken as the "name" of the table,
 and the component after the period as the first column name.
 This should be followed by the other column values for u0,
 and then subsequent u1, u2, etc, in strict order with no omissions,
 and with all of the columns for all uN in the same order as for u0.
**************************************************************************/
int section_file_save(struct section_file *my_section_file, char *filename)
{
  FILE *fs;

  struct genlist_iterator sec_iter, ent_iter, save_iter, col_iter;
  struct section *psection;
  struct section_entry *pentry, *col_pentry;

  if(!(fs=fopen(filename, "w")))
    return 0;

  for(genlist_iterator_init(&sec_iter, &my_section_file->section_list, 0);
      (psection = ITERATOR_PTR(sec_iter));
      ITERATOR_NEXT(sec_iter)) {
    fprintf(fs, "\n[%s]\n", psection->name);

    for(genlist_iterator_init(&ent_iter, &psection->entry_list, 0);
	(pentry = ITERATOR_PTR(ent_iter));
	ITERATOR_NEXT(ent_iter)) {

      /* Tables: break out of this loop if this is a non-table
       * entry (pentry and ent_iter unchanged) or after table (pentry
       * and ent_iter suitably updated, pentry possibly NULL).
       * After each table, loop again in case the next entry
       * is another table.
       */
      for(;;) {
	char *c, *first, base[64];
	int offset, irow, icol, ncol;
	
	/* Example: for first table name of "xyz0.blah":
	 *  first points to the original string pentry->name
	 *  base contains "xyz";
	 *  offset=5 (so first+offset gives "blah")
	 *  note strlen(base)=offset-2
	 */

	if(!SAVE_TABLES) break;
	
	c = first = pentry->name;
	if(!*c || !isalpha(*c)) break;
	for( ; *c && isalpha(*c); c++);
	if(strncmp(c,"0.",2) != 0) break;
	c+=2;
	if(!*c || !isalnum(*c)) break;

	offset = c - first;
	first[offset-2] = '\0';
	strcpy(base, first);
	first[offset-2] = '0';
	fprintf(fs, "%s={", base);

	/* Save an iterator at this first entry, which we can later use
	 * to repeatedly iterate over column names:
	 */
	save_iter = ent_iter;

	/* write the column names, and calculate ncol: */
	ncol = 0;
	col_iter = save_iter;
	for( ; (col_pentry = ITERATOR_PTR(col_iter)); ITERATOR_NEXT(col_iter)) {
	  if(strncmp(col_pentry->name, first, offset) != 0)
	    break;
	  fprintf(fs, "%c\"%s\"", (ncol==0?' ':','), col_pentry->name+offset);
	  ncol++;
	}
	fprintf(fs, "\n");

	/* Iterate over rows and columns, incrementing ent_iter as we go,
	 * and writing values to the table.  Have a separate iterator
	 * to the column names to check they all match.
	 */
	irow = icol = 0;
	col_iter = save_iter;
	for(;;) {
	  char expect[128];	/* pentry->name we're expecting */

	  pentry = ITERATOR_PTR(ent_iter);
	  col_pentry = ITERATOR_PTR(col_iter);

	  sprintf(expect, "%s%d.%s", base, irow, col_pentry->name+offset);

	  /* break out of tabular if doesn't match: */
	  if((!pentry) || (strcmp(pentry->name, expect) != 0)) {
	    if(icol != 0) {
	      freelog(LOG_NORMAL, "unexpected exit from tabular at %s",
		   pentry->name);
	      fprintf(fs, "\n");
	    }
	    fprintf(fs, "}\n");
	    break;
	  }

	  if(icol>0)
	    fprintf(fs, ",");
	  if(pentry->svalue) 
	    fprintf(fs, "\"%s\"", moutstr(pentry->svalue));
	  else
	    fprintf(fs, "%d", pentry->ivalue);
	  
	  ITERATOR_NEXT(ent_iter);
	  ITERATOR_NEXT(col_iter);
	  
	  icol++;
	  if(icol==ncol) {
	    fprintf(fs, "\n");
	    irow++;
	    icol = 0;
	    col_iter = save_iter;
	  }
	}
	if(!pentry) break;
      }
      if(!pentry) break;

      if(pentry->svalue)
	fprintf(fs, "%s=\"%s\"\n", pentry->name, moutstr(pentry->svalue));
      else
	fprintf(fs, "%s=%d\n", pentry->name, pentry->ivalue);
    }
  }
  moutstr(NULL);		/* free internal buffer */

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
    freelog(LOG_FATAL, "sectionfile doesn't contain a '%s' entry", buf);
    exit(1);
  }

  if(!entry->svalue) {
    freelog(LOG_FATAL, "sectionfile entry '%s' doesn't contain a string", buf);
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

  pentry = section_file_insert_internal(my_section_file, buf);
  pentry->svalue = sbuf_strdup(my_section_file->sb, sval);
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
    freelog(LOG_FATAL, "sectionfile doesn't contain a '%s' entry", buf);
    exit(1);
  }

  if(entry->svalue) {
    freelog(LOG_FATAL, "sectionfile entry '%s' doesn't contain an integer", buf);
    exit(1);
  }
  
  return entry->ivalue;
}


/**************************************************************************
  As secfile_lookup_int(), but return a specified default value if the
  entry does not exist.  If the entry exists as a string, then die.
**************************************************************************/
int secfile_lookup_int_default(struct section_file *my_section_file,
			       int def, char *path, ...)
{
  struct section_entry *entry;
  char buf[512];
  va_list ap;

  va_start(ap, path);
  vsprintf(buf, path, ap);
  va_end(ap);

  if(!(entry=section_file_lookup_internal(my_section_file, buf))) {
    return def;
  }
  if(entry->svalue) {
    freelog(LOG_FATAL, "sectionfile contains a '%s', but string not integer", buf);
    exit(1);
  }
  return entry->ivalue;
}

/**************************************************************************
  As secfile_lookup_str(), but return a specified default (char*) if the
  entry does not exist.  If the entry exists as an int, then die.
**************************************************************************/
char *secfile_lookup_str_default(struct section_file *my_section_file, 
				 char *def, char *path, ...)
{
  struct section_entry *entry;
  char buf[512];
  va_list ap;

  va_start(ap, path);
  vsprintf(buf, path, ap);
  va_end(ap);

  if(!(entry=section_file_lookup_internal(my_section_file, buf))) {
    return def;
  }

  if(!entry->svalue) {
    freelog(LOG_FATAL, "sectionfile contains a '%s', but integer not string", buf);
    exit(1);
  }
  
  return entry->svalue;
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
  char mod_fullpath[1024];
  int len;
  struct genlist_iterator myiter;
  struct hash_entry *hentry;
  struct section_entry *result;

  /* treat "sec.foo,0" as "sec.foo": */
  len = strlen(fullpath);
  if(len>2 && fullpath[len-2]==',' && fullpath[len-1]=='0') {
    strcpy(mod_fullpath, fullpath);
    fullpath = mod_fullpath;	/* reassign local pointer 'fullpath' */
    fullpath[len-2] = '\0';
  }
  
  if (secfilehash_hashash(my_section_file)) {
    hentry = secfilehash_lookup(my_section_file, fullpath, NULL);
    if (hentry->data) {
      result = hentry->data;
      result->used++;
      return result;
    } else {
      return 0;
    }
  }

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
		   ent_name)) {
	  result = (struct section_entry *)ITERATOR_PTR(myiter2);
	  result->used++;
	  return result;
	}
      }
    }

  return 0;
}


/**************************************************************************
 The caller should ensure that "fullpath" should not refer to an entry
 which already exists in "my_section_file".
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
  struct sbuffer *sb = my_section_file->sb;

  if(!(pdelim=strchr(fullpath, '.'))) /* i dont like strtok */
    return 0;
  strncpy(sec_name, fullpath, pdelim-fullpath);
  sec_name[pdelim-fullpath]='\0';
  strcpy(ent_name, pdelim+1);
  my_section_file->num_entries++;

  /* Do a reverse search of sections, since we're most likely
   * to be adding to the lastmost section.
   */
  for(genlist_iterator_init(&myiter, &my_section_file->section_list, -1);
      (psection = (struct section *)ITERATOR_PTR(myiter));
      ITERATOR_PREV(myiter)) {
    if(strcmp(psection->name, sec_name)==0) {
      /* This DOES NOT check whether the entry already exists in
       * the section, to avoid O(N^2) behaviour.
       */
      pentry = sbuf_malloc(sb, sizeof(struct section_entry));
      pentry->name = sbuf_strdup(sb, ent_name);
      genlist_insert(&psection->entry_list, pentry, -1);
      return pentry;
    }
  }

  psection = sbuf_malloc(sb, sizeof(struct section));
  psection->name = sbuf_strdup(sb, sec_name);
  genlist_init(&psection->entry_list);
  genlist_insert(&my_section_file->section_list, psection, -1);
  
  pentry = sbuf_malloc(sb, sizeof(struct section_entry));
  pentry->name = sbuf_strdup(sb, ent_name);
  genlist_insert(&psection->entry_list, pentry, -1);

  return pentry;
}


/**************************************************************************
  Anyone know a good general string->hash function?
**************************************************************************/
/* plenty of primes, not too small, in random order: */
static unsigned int coeff[] = { 113, 59, 23, 73, 31, 79, 97, 103, 67, 109,
				71, 89, 53, 37, 101, 41, 29, 43, 13, 61,
				107, 47, 83, 17 };
#define NCOEFF (sizeof(coeff)/sizeof(coeff[0]))
static int hashfunc(char *name, int num_buckets)
{
  unsigned int result=0;
  int i;

  /* if(name[0]=='p') name+=6; */  /* no longer needed --dwp */
  for(i=0; *name; i++, name++) {
    if (i==NCOEFF) {
      i = 0;
    }
    result *= (unsigned int)5;
    result += coeff[i] * (unsigned int)(*name);
  }
  return (result % (unsigned int)num_buckets);
}

/**************************************************************************
 Return 0 if the section_file has not been setup for hashing.
**************************************************************************/
static int secfilehash_hashash(struct section_file *file)
{
  return (file->hashd!=NULL && file->hashd->table!=NULL
	  && file->hashd->num_buckets!=0);
}

/**************************************************************************
  Basic checks for existence/integrity of hash data and fail if bad.
**************************************************************************/
static void secfilehash_check(struct section_file *file)
{
  if (!secfilehash_hashash(file)) {
    freelog(LOG_FATAL, "hash operation before setup" );
    exit(1);
  }
  if (file->num_entries != file->hashd->num_entries_hashbuild) {
    freelog(LOG_FATAL, "section_file has more entries than when hash built" );
    exit(1);
  }
}

/**************************************************************************
  Return pointer to entry in hash table where key resides, or
  where it should go if its to be a new key.
  Insert the hash value in parameter (*hash_return) unless hash_return==NULL
**************************************************************************/
static struct hash_entry *secfilehash_lookup(struct section_file *file,
					     char *key, int *hash_return)
{
  int hash_val, i;
  struct hash_entry *hentry;

  secfilehash_check(file);
  i = hash_val = hashfunc(key, file->hashd->num_buckets);
  if (hash_return) {
    *hash_return = hash_val;
  }
  do {
    hentry = &(file->hashd->table[i]);
    if (hentry->key_val == NULL) {	/* empty position in table */
      return hentry;
    }
    if (hentry->hash_val==hash_val &&
	strcmp(hentry->key_val, key)==0) { /* match */
      return hentry;
    }
    file->hashd->num_collisions++;
    if (HASH_DEBUG>=2) {
      freelog(LOG_DEBUG, "Hash collision for \"%s\", %d\n   with \"%s\", %d",
	   key, hash_val, hentry->key_val, hentry->hash_val);
    }
    i++;
    if (i==file->hashd->num_buckets) {
      i=0;
    }
  } while (i!=hash_val);	/* catch loop all the way round  */
  freelog(LOG_FATAL, "Full hash table??");
  exit(1);
}


/**************************************************************************
 Insert a entry into the hash table.
 Note the hash table key_val is malloced here.
**************************************************************************/
static void secfilehash_insert(struct section_file *file,
			       char *key, struct section_entry *data)
{
  struct hash_entry *hentry;
  int hash_val;

  hentry = secfilehash_lookup(file, key, &hash_val);
  if (hentry->key_val != NULL) {
    freelog(LOG_FATAL, "Tried to insert same value twice: %s", key );
    exit(1);
  }
  hentry->data = data;
  hentry->key_val = sbuf_strdup(file->sb, key);
  hentry->hash_val = hash_val;
}

/**************************************************************************
 Build a hash table for the file.  Note that the section_file should
 not be modified (except to free it) subsequently.
**************************************************************************/
static void secfilehash_build(struct section_file *file)
{
  struct hash_data *hashd;
  struct genlist_iterator myiter, myiter2;
  struct section *this_section;
  struct section_entry *this_entry;
  char buf[256];
  int i;
  unsigned int uent, pow2;

  hashd = file->hashd = fc_malloc(sizeof(struct hash_data));

  /* Power of two which is larger than num_entries, then
     extra factor of 2 for breathing space: */
  uent = file->num_entries;
  pow2 = 2;
  while (uent) {
    uent >>= 1;
    pow2 <<= 1;
  };
  if (pow2<128) pow2 = 128;
  
  hashd->num_buckets = pow2;
  hashd->num_entries_hashbuild = file->num_entries;
  hashd->num_collisions = 0;
  
  hashd->table = fc_malloc(hashd->num_buckets*sizeof(struct hash_entry));

  for(i=0; i<hashd->num_buckets; i++) {
    hashd->table[i].data = NULL;
    hashd->table[i].key_val = NULL;
    hashd->table[i].hash_val = -1;
  }

  genlist_iterator_init(&myiter, &file->section_list, 0);

  for(; ITERATOR_PTR(myiter); ITERATOR_NEXT(myiter)) {
    this_section = (struct section *)ITERATOR_PTR(myiter);
    genlist_iterator_init(&myiter2, &((struct section *)
			       ITERATOR_PTR(myiter))->entry_list, 0);

    for(; ITERATOR_PTR(myiter2); ITERATOR_NEXT(myiter2)) {
      this_entry = (struct section_entry *)ITERATOR_PTR(myiter2);
      sprintf(buf, "%s.%s", this_section->name, this_entry->name);
      secfilehash_insert(file, buf, this_entry);
    }
  }
  if (HASH_DEBUG>=1) {
    freelog(LOG_DEBUG, "Hash collisions during build: %d (%d entries, %d buckets)",
	hashd->num_collisions, file->num_entries, hashd->num_buckets );
  }
}


/**************************************************************************
 Free the memory allocated for the hash table.
**************************************************************************/
static void secfilehash_free(struct section_file *file)
{
  secfilehash_check(file);
  free(file->hashd->table);
  free(file->hashd);
}

/**************************************************************************
 Returns the number of elements in a "vector".
 That is, returns the number of consecutive entries in the sequence:
 "path,0" "path,1", "path,2", ...
 If none, returns 0.
**************************************************************************/
int secfile_lookup_vec_dimen(struct section_file *my_section_file, 
			     char *path, ...)
{
  char buf[512];
  va_list ap;
  int j=0;

  va_start(ap, path);
  vsprintf(buf, path, ap);
  va_end(ap);

  while(section_file_lookup(my_section_file, "%s,%d", buf, j)) {
    j++;
  }
  return j;
}

/**************************************************************************
 Return a pointer for a list of integers for a "vector".
 The return value is malloced here, and should be freed by the user.
 The size of the returned list is returned in (*dimen).
 If the vector does not exist, returns NULL ands sets (*dimen) to 0.
**************************************************************************/
int *secfile_lookup_int_vec(struct section_file *my_section_file,
			    int *dimen, char *path, ...)
{
  char buf[512];
  va_list ap;
  int j, *res;

  va_start(ap, path);
  vsprintf(buf, path, ap);
  va_end(ap);

  *dimen = secfile_lookup_vec_dimen(my_section_file, buf);
  if (*dimen == 0) {
    return NULL;
  }
  res = fc_malloc((*dimen)*sizeof(int));
  for(j=0; j<(*dimen); j++) {
    res[j] = secfile_lookup_int(my_section_file, "%s,%d", buf, j);
  }
  return res;
}

/**************************************************************************
 Return a pointer for a list of "strings" for a "vector".
 The return value is malloced here, and should be freed by the user,
 but the strings themselves are contained in the sectionfile
 (as when a single string is looked up) and should not be altered
 or freed by the user.
 The size of the returned list is returned in (*dimen).
 If the vector does not exist, returns NULL ands sets (*dimen) to 0.
**************************************************************************/
char **secfile_lookup_str_vec(struct section_file *my_section_file,
			      int *dimen, char *path, ...)
{
  char buf[512];
  va_list ap;
  int j;
  char **res;

  va_start(ap, path);
  vsprintf(buf, path, ap);
  va_end(ap);

  *dimen = secfile_lookup_vec_dimen(my_section_file, buf);
  if (*dimen == 0) {
    return NULL;
  }
  res = fc_malloc((*dimen)*sizeof(char*));
  for(j=0; j<(*dimen); j++) {
    res[j] = secfile_lookup_str(my_section_file, "%s,%d", buf, j);
  }
  return res;
}

/***************************************************************
 Copies a string and does '\n' -> newline translation.
 Other '\c' sequences (any character 'c') are just passed
 through with the '\' removed (eg, includes '\\', '\"')
***************************************************************/
static char *minstrdup(struct sbuffer *sb, char *str)
{
  char *dest = sbuf_malloc(sb, strlen(str)+1);
  char *d2=dest;
  if(dest) {
    while (*str) {
      if (*str=='\\') {
	str++;
	if (*str=='\\') {
	  *dest++='\\';
	  str++;
	} else if (*str=='n') {
	  *dest++='\n';
	  str++;
	}
      } else {
	*dest++=*str++;
      }

    }

    *dest='\0';
  }
  return d2;
}

/***************************************************************
 Returns a pointer to an internal buffer (can only get one
 string at a time) with str escaped the opposite of minstrdup.
 Specifically, any newline, backslash, or double quote is
 escaped with a backslash.
 The internal buffer is grown as necessary, and not normally
 freed (since this will be called frequently.)  A call with
 str=NULL frees the buffer and does nothing else (returns NULL).
***************************************************************/
static char *moutstr(char *str)
{
  static char *buf = NULL;
  static int nalloc = 0;

  int len;			/* required length, including terminator */
  char *c, *dest;

  if (str==NULL) {
    freelog(LOG_DEBUG, "moutstr alloc was %d", nalloc);
    free(buf);
    buf = NULL;
    nalloc = 0;
    return NULL;
  }
  
  len = strlen(str)+1;
  for(c=str; *c; c++) {
    if (*c == '\n' || *c == '\\' || *c == '\"') {
      len++;
    }
  }
  if (len > nalloc) {
    nalloc = 2 * len + 1;
    buf = fc_realloc(buf, nalloc);
  }
  
  dest = buf;
  while(*str) {
    if (*str == '\n' || *str == '\\' || *str == '\"') {
      *dest++ = '\\';
      if (*str == '\n') {
	*dest++ = 'n';
	str++;
      } else {
	*dest++ = *str++;
      }
    } else {
      *dest++ = *str++;
    }
  }
  *dest = '\0';
  return buf;
}
