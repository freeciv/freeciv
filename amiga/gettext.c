/* This file is based on gettext sources, but specialiced for freeciv.
It may not work with other programs, as the functions do not do the
stuff of gettext package. Also client and server cannot use one data file,
but need to load it individually.

And I did not want to port gettext completely, which would have been
much more work!

Modification history;
08.08.2000 : Dirk Stöcker <stoecker@epost.de>
  Initial version
27.08.2000 : Dirk Stöcker <stoecker@epost.de>
  fixes to match changed calling mechanism
26.09.2000 : Oliver Gantert <lucyg@t-online.de>
  changed includes to work with vbcc WarpOS
16.12.2000 : Dirk Stöcker <stoecker@epost.de>
  removed changes, as it works without also
26.12.2001 : Sebastian Bauer <sebauer@t-online.de>
  uses now the prefered languages to determine the gmostr
26.12.2001 : Sebastian Bauer <sebauer@t-online.de>
  lets SAS open the locale library for us now
19.02.2002 : Sebastian Bauer <sebauer@t-online.de>
  added ngettext, but not fully working, only a dummy
12.04.2002 : Sebastian Bauer <sebauer@t-online.de>
  convert KOI8-R charset to the chareset used on Amiga
*/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <proto/exec.h>
#include <proto/locale.h>

/* The magic number of the GNU message catalog format.  */
#define _MAGIC 0x950412de
#define _MAGIC_SWAPPED 0xde120495

#define SWAP(i) (((i) << 24) | (((i) & 0xff00) << 8) | (((i) >> 8) & 0xff00) | ((i) >> 24))
#define GET(data) (domain.must_swap ? SWAP (data) : (data))

typedef unsigned long nls_uint32;

/* Header for binary .mo file format.  */
struct mo_file_header
{
  nls_uint32 magic;  /* The magic number.  */
  nls_uint32 revision;  /* The revision number of the file format.  */
  nls_uint32 nstrings;  /* The number of strings pairs.  */
  nls_uint32 orig_tab_offset;  /* Offset of table with start offsets of original strings.  */
  nls_uint32 trans_tab_offset;  /* Offset of table with start offsets of translation strings.  */
  nls_uint32 hash_tab_size;  /* Size of hashing table.  */
  nls_uint32 hash_tab_offset;  /* Offset of first hashing entry.  */
};

struct string_desc
{
  nls_uint32 length;  /* Length of addressed string.  */
  nls_uint32 offset;  /* Offset of string in file.  */
};

struct loaded_domain
{
  const char *data;
  int must_swap;
  nls_uint32 nstrings;
  struct string_desc *orig_tab;
  struct string_desc *trans_tab;
  nls_uint32 hash_size;
  nls_uint32 *hash_tab;
};

static struct loaded_domain domain = {0, 0, 0, 0, 0, 0, 0};

char *gettext(const char *msgid);

static long openmo(char *dir, char *loc)
{
  char filename[512];
  FILE *fd;
  long size;
  struct mo_file_header *data;

  if(!dir || !loc)
    return 0;

  sprintf(filename, "%s/%s.mo", dir, loc);
#ifdef DEBUG
  printf("locale file name: %s\n", filename);
#endif

  if((fd = fopen(filename, "rb")))
  {
    if(!fseek(fd, 0, SEEK_END))
    {
      if((size = ftell(fd)) != EOF)
      {
        if(!fseek(fd, 0, SEEK_SET))
        {
          if((data = (struct mo_file_header *) malloc(size)))
          {
            if(fread(data, size, 1, fd) == 1)
            {
              if((data->magic == _MAGIC || data->magic == _MAGIC_SWAPPED) && !data->revision) /* no need to swap! */
              {
                domain.data = (const char *) data;
                domain.must_swap = data->magic != _MAGIC;
                domain.nstrings = GET(data->nstrings);
                domain.orig_tab = (struct string_desc *) ((char *) data + GET(data->orig_tab_offset));
                domain.trans_tab = (struct string_desc *) ((char *) data + GET(data->trans_tab_offset));
                domain.hash_size = GET(data->hash_tab_size);
                domain.hash_tab = (nls_uint32 *) ((char *) data + GET(data->hash_tab_offset));

                if (strstr(gettext(""),"charset=KOI8-R") /* This somehow doesn't work so we check for the ru string also */ || !strcmp("ru",loc))
                {
		  int i;

		  /* convert the charset to AMI1251, this is the table */
		  static const unsigned char table[] = {
		    0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x09,0x0A,0x0B,0x0C,0x0D,0x0E,0x0F,
		    0x10,0x11,0x12,0x13,0x14,0x15,0x16,0x17,0x18,0x19,0x1A,0x1B,0x1C,0x1D,0x1E,0x1F,
		    0x20,0x21,0x22,0x23,0x24,0x25,0x26,0x27,0x28,0x29,0x2A,0x2B,0x2C,0x2D,0x2E,0x2F,
		    0x30,0x31,0x32,0x33,0x34,0x35,0x36,0x37,0x38,0x39,0x3A,0x3B,0x3C,0x3D,0x3E,0x3F,
		    0x40,0x41,0x42,0x43,0x44,0x45,0x46,0x47,0x48,0x49,0x4A,0x4B,0x4C,0x4D,0x4E,0x4F,
		    0x50,0x51,0x52,0x53,0x54,0x55,0x56,0x57,0x58,0x59,0x5A,0x5B,0x5C,0x5D,0x5E,0x5F,
		    0x60,0x61,0x62,0x63,0x64,0x65,0x66,0x67,0x68,0x69,0x6A,0x6B,0x6C,0x6D,0x6E,0x6F,
		    0x70,0x71,0x72,0x73,0x74,0x75,0x76,0x77,0x78,0x79,0x7A,0x7B,0x7C,0x7D,0x7E,0x7F,
		    0xBF,0xB6,0xBE,0xAA,0xA1,0xB5,0x8D,0x8E,0x93,0x90,0x98,0x96,0x99,0x94,0x9A,0x80,
		    0x81,0x82,0x87,0x88,0x89,0x8A,0x8F,0x9E,0x9F,0xB4,0xAD,0xAC,0xAC,0xAC,0xAC,0xAC,
		    0xAC,0xAC,0xAC,0xB8,0xAC,0xAC,0xAC,0xBA,0xAC,0xAC,0xAC,0xAC,0xAC,0xAC,0xAB,0xBB,
		    0x9B,0x9C,0x9E,0xA8,0xA5,0x83,0x84,0x85,0x86,0x97,0x95,0x91,0x92,0x8B,0x8C,0xAF,
		    0xFE,0xE0,0xE1,0xF6,0xE4,0xE5,0xF4,0xE3,0xF5,0xE8,0xE9,0xEA,0xEB,0xEC,0xED,0xEE,
		    0xEF,0xFF,0xF0,0xF1,0xF2,0xF3,0xE6,0xE2,0xFC,0xFB,0xE7,0xF8,0xFD,0xF9,0xF7,0xFA,
		    0xDE,0xC0,0xC1,0xD6,0xC4,0xC5,0xD4,0xC3,0xD5,0xC8,0xC9,0xCA,0xCB,0xCC,0xCD,0xCE,
		    0xCF,0xDF,0xD0,0xD1,0xD2,0xD3,0xC6,0xC2,0xDC,0xDB,0xC7,0xD8,0xDD,0xD9,0xD7,0xDA
                  };

		  for (i=0;i<domain.nstrings;i++)
		  {
		    int j;
		    int off = GET(domain.trans_tab[i].offset);
		    char *trans_string = domain.data + off;

		    /* Now convert every char in size the string */
		    for (j=0;j<GET(domain.trans_tab[i].length) && j+off<size;j++)
		    {
		      trans_string[j] = table[trans_string[j]];
		    }
		  }
                }
              }
              else
                free(data);
            }
            else
              free(data);
          }
        }
      }
    }
    fclose(fd);
  }
  return (long) domain.data;
}

/* languages supported by freeciv */
struct LocaleConv {
  char *langstr;
  char *gmostr;
} LocaleConvTab[] = {
{"deutsch", "de"},
/*{"", "en_GB"}, does not exist on Amiga */
{"español", "es"},
{"français", "fr"},
{"hrvatski", "hu"},
{"italiano", "it"},
{"nihongo", "ja"},
{"nederlands", "nl"},
{"norsk", "no"},
{"polski", "pl"},
{"português" , "pt"},
{"português-brasil", "pt_BR"},
{"russian", "ru"},
{"svenska", "sv"},
{"suomi","fi"},
{0, 0},
};

/* Returns NULL if is not in the list */
static char *find_gmostr(char *lang)
{
	int i;
	if (!lang) return NULL;
	for (i=0;LocaleConvTab[i].langstr;i++)
	{
		if (!strcmp(LocaleConvTab[i].langstr,lang)) return LocaleConvTab[i].gmostr;
	}
	return NULL;
}

void bindtextdomain(char * pack, char * dir)
{
	struct Locale *l;
  int i;

  if(openmo(dir, getenv("LANG")))
    return;

  if(openmo(dir, getenv("LANGUAGE")))
    return;

	if((l = OpenLocale(0)))
	{
		for (i=0;i<10;i++)
		{
			if (!openmo(dir,find_gmostr(l->loc_PrefLanguages[i])))
			{
				if (openmo(dir,l->loc_PrefLanguages[i])) break;
			} else break;
		}

		CloseLocale(l);
	}
}

#define HASHWORDBITS 32
static unsigned long hash_string(const char *str_param)
{
  unsigned long int hval, g;
  const char *str = str_param;

  /* Compute the hash value for the given string.  */
  hval = 0;
  while (*str != '\0')
  {
    hval <<= 4;
    hval += (unsigned long) *str++;
    g = hval & ((unsigned long) 0xf << (HASHWORDBITS - 4));
    if (g != 0)
    {
      hval ^= g >> (HASHWORDBITS - 8);
      hval ^= g;
    }
  }
  return hval;
}

static char *find_msg(const char * msgid)
{
  long top, act, bottom;

  if(!domain.data || !msgid)
    return NULL;

  /* Locate the MSGID and its translation.  */
  if(domain.hash_size > 2 && domain.hash_tab != NULL)
  {
    /* Use the hashing table.  */
    nls_uint32 len = strlen (msgid);
    nls_uint32 hash_val = hash_string (msgid);
    nls_uint32 idx = hash_val % domain.hash_size;
    nls_uint32 incr = 1 + (hash_val % (domain.hash_size - 2));
    nls_uint32 nstr;

    for(;;)
    {
      if(!(nstr = GET(domain.hash_tab[idx]))) /* Hash table entry is empty.  */
	return NULL;

      if(GET(domain.orig_tab[nstr - 1].length) == len && strcmp(msgid, domain.data +
      GET(domain.orig_tab[nstr - 1].offset)) == 0)
        return (char *) domain.data + GET(domain.trans_tab[nstr - 1].offset);

      if(idx >= domain.hash_size - incr)
	idx -= domain.hash_size - incr;
      else
        idx += incr;
    }
  }

  /* Now we try the default method:  binary search in the sorted
     array of messages.  */
  bottom = act = 0;
  top = domain.nstrings;
  while(bottom < top)
  {
    int cmp_val;

    act = (bottom + top) / 2;
    cmp_val = strcmp(msgid, domain.data + GET(domain.orig_tab[act].offset));
    if(cmp_val < 0)
      top = act;
    else if(cmp_val > 0)
      bottom = act + 1;
    else
      break;
  }

  /* If an translation is found return this.  */
  return bottom >= top ? NULL : (char *) domain.data + GET(domain.trans_tab[act].offset);
}


char *gettext(const char *msgid)
{
  char *res;

  if(!(res = find_msg(msgid)))
  {
    res = (char *) msgid;
#ifdef DEBUG
    Printf("Did not find: '%s'\n", res);
#endif
  }

  return res;
}

char *ngettext(const char *msgid1, const char *msgid2, unsigned long int n)
{
	return (n==1?msgid1:msgid2);
}

char *setlocale(int a, char *b)
{
  return "C";
}

void textdomain(char *package)
{
}
