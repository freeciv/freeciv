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
#define HASH_BUCKET_RATIO 5
#define HASH_DEBUG 1		/* 0,1,2 */

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
static char *minstrdup(char *str);

/**************************************************************************
...
**************************************************************************/
void section_file_init(struct section_file *file)
{
  genlist_init(&file->section_list);
  file->num_entries = 0;
  file->hashd = NULL;
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

  dealloc_strbuffer();
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

  section_file_init(my_section_file);

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
	flog(LOG_FATAL, "missing ] in %s - line %d", filename, lineno);
	exit(1);
      }

      *cptr='\0';

      psection=(struct section *)strbuffermalloc(sizeof(struct section));
      psection->name=strbufferdup(buffer+1);
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
	flog(LOG_FATAL, "syntax error in %s - line %d", filename, lineno);
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
	  flog(LOG_FATAL, "expected end of string in %s - line %d", filename, lineno);
	  exit(1);
	}

	*cptr='\0';

      }
      else {
	flog(LOG_FATAL, "syntax error in %s - line %d", filename, lineno);
	exit(1);
      }


      if(!current_section) {
	flog(LOG_FATAL, "entry defined before first section in %s - line %d", 
	    filename, lineno);
	exit(1);
      }

      
      pentry=(struct section_entry *)strbuffermalloc(sizeof(struct section_entry));
      pentry->name=strbufferdup(pname);
      
      if(pvalue)  {
	pentry->svalue=minstrdup(pvalue);
      } else {
	pentry->ivalue=ival;
	pentry->svalue=0;
      }
      genlist_insert(&current_section->entry_list, pentry, -1);

      my_section_file->num_entries++;
    }
  }

  fclose(fs);

  if(DO_HASH) {
    secfilehash_build(my_section_file);
  }
    
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
    flog(LOG_FATAL, "sectionfile doesn't contain a '%s' entry", buf);
    exit(1);
  }

  if(!entry->svalue) {
    flog(LOG_FATAL, "sectionfile entry '%s' doesn't contain a string", buf);
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
  pentry->svalue=strbufferdup(sval);
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
    flog(LOG_FATAL, "sectionfile doesn't contain a '%s' entry", buf);
    exit(1);
  }

  if(entry->svalue) {
    flog(LOG_FATAL, "sectionfile entry '%s' doesn't contain an integer", buf);
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
    flog(LOG_FATAL, "sectionfile contains a '%s', but string not integer", buf);
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
    flog(LOG_FATAL, "sectionfile contains a '%s', but integer not string", buf);
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
  struct genlist_iterator myiter;
  struct hash_entry *hentry;

  if (secfilehash_hashash(my_section_file)) {
    hentry = secfilehash_lookup(my_section_file, fullpath, NULL);
    if (hentry->data) {
      return hentry->data;
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

      pentry=(struct section_entry *)strbuffermalloc(sizeof(struct section_entry));
      pentry->name=strbufferdup(ent_name);
      genlist_insert(&((struct section *)
			    ITERATOR_PTR(myiter))->entry_list, pentry, -1);

      my_section_file->num_entries++;
      return pentry;
    }


  psection=(struct section *)strbuffermalloc(sizeof(struct section));
  psection->name=strbufferdup(sec_name);
  genlist_init(&psection->entry_list);
  genlist_insert(&my_section_file->section_list, psection, -1);
  
  pentry=(struct section_entry *)strbuffermalloc(sizeof(struct section_entry));
  pentry->name=strbufferdup(ent_name);
  genlist_insert(&psection->entry_list, pentry, -1);

  return pentry;
}


/**************************************************************************
  Anyone know a good general string->hash function?
**************************************************************************/
/* some primes: */
static unsigned int coeff[] = { 61, 59, 53, 47, 43, 41, 37, 31, 29, 23, 17,
				13, 11, 7, 3, 1 };
#define NCOEFF (sizeof(coeff)/sizeof(coeff[0]))
static int hashfunc(char *name, int num_buckets)
{
  unsigned int result=0;
  int i;

  if(name[0]=='p') name+=6;
  for(i=0; *name; i++, name++) {
    if (i==NCOEFF) {
      i = 0;
    }
    result *= (unsigned int)5;
    result += coeff[i] * (unsigned int)(*name);
  }
  return (result%num_buckets);
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
    flog(LOG_FATAL, "hash operation before setup" );
    exit(1);
  }
  if (file->num_entries != file->hashd->num_entries_hashbuild) {
    flog(LOG_FATAL, "section_file has more entries than when hash built" );
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
      flog(LOG_DEBUG, "Hash collision for \"%s\", %d", key, hash_val);
    }
    i++;
    if (i==file->hashd->num_buckets) {
      i=0;
    }
  } while (i!=hash_val);	/* catch loop all the way round  */
  flog(LOG_FATAL, "Full hash table??");
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
    flog(LOG_FATAL, "Tried to insert same value twice: %s", key );
    exit(1);
  }
  hentry->data = data;
  hentry->key_val = strbufferdup(key);
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

  hashd = file->hashd = malloc(sizeof(struct hash_data));

  hashd->num_buckets = HASH_BUCKET_RATIO * file->num_entries;
  hashd->num_entries_hashbuild = file->num_entries;
  hashd->num_collisions = 0;
  
  hashd->table = malloc(hashd->num_buckets*sizeof(struct hash_entry));
  if (hashd==NULL || hashd->table==NULL) {
    flog(LOG_FATAL, "malloc error for hash table" );
    exit(1);
  }
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
    flog(LOG_DEBUG, "Hash collisions during build: %d (%d entries, %d buckets)",
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


static char *strbuffer=NULL;
static int strbufferoffset=65536;
/**************************************************************************
 Allocate a 64k buffer for strings
**************************************************************************/
void alloc_strbuffer(void)
{
  char *newbuf;

  newbuf = malloc(64*1024);

  /* add a pointer back to the old buffer */
  *(char **)(newbuf)=strbuffer;
  strbufferoffset = sizeof(char *);

  strbuffer=newbuf;
}

/**************************************************************************
 Copy a string into the string buffer, if there is room.
**************************************************************************/
char *strbufferdup(const char *str)
{
  int len = strlen(str)+1;
  char *p;

  if(len > (65536 - strbufferoffset))  {
    /* buffer must be full */
    alloc_strbuffer();
  }

  p=strbuffer+strbufferoffset;
  memcpy(p,str,len);
  strbufferoffset+=len;

  return p;
}

/**************************************************************************
 Allocate size bytes from the string buffer, like malloc() but much
 faster.  Note that size must be < 64k!  strbuffer is for little
 allocations that get freed all at once.
**************************************************************************/
void *strbuffermalloc(int len)
{
  char *p;

  if(len > (65536 - strbufferoffset))  {
    alloc_strbuffer();
  }
  p=strbuffer+strbufferoffset;
  strbufferoffset+=len;
  return p;
}

/**************************************************************************
 De-allocate all the string buffers
**************************************************************************/
void dealloc_strbuffer(void)
{
  char *next;

  do {
    next = *(char **)strbuffer;
    free(strbuffer);
    strbuffer=next;
  } while(next);
  strbufferoffset=65536;
}

/***************************************************************
 Copies a string and does \n -> newline translation
***************************************************************/
static char *minstrdup(char *str)
{
  char *dest=strbuffermalloc(strlen(str)+1);
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

