## Process this file with automake to produce Makefile.in

## Must be set before adding anything
noinst_LIBRARIES =

AM_CPPFLAGS = -I../src $(LUA_CFLAGS) $(FCDB_MYSQL_CFLAGS) $(FCDB_POSTGRES_CFLAGS)

if FCDB_MYSQL
noinst_LIBRARIES += libluasql_mysql.a
libluasql_mysql_a_SOURCES = \
		luasql.h	\
		luasql.c	\
		ls_mysql.h	\
		ls_mysql.c
endif

if FCDB_POSTGRES
noinst_LIBRARIES += libluasql_postgres.a
libluasql_postgres_a_SOURCES = \
		luasql.h	\
		luasql.c	\
		ls_postgres.h	\
		ls_postgres.c
endif

if FCDB_SQLITE3
noinst_LIBRARIES += libluasql_sqlite3.a
libluasql_sqlite3_a_SOURCES = \
		luasql.h	\
		luasql.c	\
		ls_sqlite3.h	\
		ls_sqlite3.c
endif
