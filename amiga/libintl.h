/* dummy file */

struct lconv {char grouping[1];char *thousands_sep;};
#define FC_ALL		0
#define LC_ALL		0
#define LC_NUMERIC	0
#define textdomain(a)
#define localeconv()	0
void bindtextdomain(char * pack, char * dir);
char *gettext(const char *msgid);
char *setlocale(int a, char *b);

/* This is actually really dangerous, but if we keep track of changes,
it will work. As the Amiga does not have the gettext system and it
is too much work to port global gettext support it must be enough. */

