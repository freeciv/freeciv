/*
** LuaSQL, ODBC driver
** Authors: Pedro Rabinovitch, Roberto Ierusalimschy, Diego Nehab,
** Tomas Guisasola
** See Copyright Notice in license.html
*/

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if defined(_WIN32)
#include <windows.h>
#include <sqlext.h>
#elif defined(INFORMIX)
#include "infxcli.h"
#else
#include "sql.h"
#include "sqltypes.h"
#include "sqlext.h"
#endif

#include "lua.h"
#include "lauxlib.h"

#include "luasql.h"

#define LUASQL_ENVIRONMENT_ODBC "ODBC environment"
#define LUASQL_CONNECTION_ODBC "ODBC connection"
#define LUASQL_STATEMENT_ODBC "ODBC statement"
#define LUASQL_CURSOR_ODBC "ODBC cursor"

/* holds data for parameter binding */
typedef struct {
	SQLPOINTER buf;
	SQLLEN len;
	SQLLEN type;
} param_data;

/* general form of the driver objects */
typedef struct {
	short closed;
	int   lock;
} obj_data;

typedef struct {
	short         closed;
	int           lock;               /* lock count for open connections */
	SQLHENV       henv;               /* environment handle */
} env_data;

typedef struct {
	short         closed;
	int           lock;               /* lock count for open statements */
	env_data      *env;               /* the connection's environment */
	SQLHDBC       hdbc;               /* database connection handle */
} conn_data;

typedef struct {
	short         closed;
	int           lock;               /* lock count for open cursors */
	unsigned char hidden;             /* these statement was created indirectly */
	conn_data     *conn;              /* the statement's connection */
	SQLHSTMT      hstmt;              /* statement handle */
	SQLSMALLINT   numparams;          /* number of input parameters */
	int           paramtypes;         /* reference to param type table */
	param_data    *params;            /* array of parameter data */
} stmt_data;

typedef struct {
	short         closed;
	stmt_data     *stmt;              /* the cursor's statement */
	int           numcols;            /* number of columns */
	int           coltypes, colnames; /* reference to column information tables */
} cur_data;


/* we are lazy */
#define hENV SQL_HANDLE_ENV
#define hSTMT SQL_HANDLE_STMT
#define hDBC SQL_HANDLE_DBC

static int error(SQLRETURN a)
{
	return (a != SQL_SUCCESS) && (a != SQL_SUCCESS_WITH_INFO) && (a != SQL_NO_DATA);
}


/*
** Registers a given C object in the registry to avoid GC
*/
static void luasql_registerobj(lua_State *L, int index, void *obj)
{
	lua_pushvalue(L, index);
	lua_pushlightuserdata(L, obj);
	lua_pushvalue(L, -2);
	lua_settable(L, LUA_REGISTRYINDEX);
	lua_pop(L, 1);
}

/*
** Unregisters a given C object from the registry
*/
static void luasql_unregisterobj(lua_State *L, void *obj)
{
	lua_pushlightuserdata(L, obj);
	lua_pushnil(L);
	lua_settable(L, LUA_REGISTRYINDEX);
}

static int lock_obj(lua_State *L, int indx, void *ptr)
{
	obj_data *obj = (obj_data *)ptr;

	luasql_registerobj(L, indx, obj);
	return ++obj->lock;
}

static int unlock_obj(lua_State *L, void *ptr)
{
	obj_data *obj = (obj_data *)ptr;

	if(--obj->lock == 0) {
		luasql_unregisterobj(L, obj);
	}

	return obj->lock;
}

/*
** Check for valid environment.
*/
static env_data *getenvironment (lua_State *L, int i)
{
	env_data *env = (env_data *)luaL_checkudata (L, i, LUASQL_ENVIRONMENT_ODBC);
	luaL_argcheck (L, env != NULL, i, LUASQL_PREFIX"environment expected");
	luaL_argcheck (L, !env->closed, i, LUASQL_PREFIX"environment is closed");
	return env;
}


/*
** Check for valid connection.
*/
static conn_data *getconnection (lua_State *L, int i)
{
	conn_data *conn = (conn_data *)luaL_checkudata (L, i, LUASQL_CONNECTION_ODBC);
	luaL_argcheck (L, conn != NULL, i, LUASQL_PREFIX"connection expected");
	luaL_argcheck (L, !conn->closed, i, LUASQL_PREFIX"connection is closed");
	return conn;
}


/*
** Check for valid connection.
*/
static stmt_data *getstatement (lua_State *L, int i)
{
	stmt_data *stmt = (stmt_data *)luaL_checkudata (L, i, LUASQL_STATEMENT_ODBC);
	luaL_argcheck (L, stmt != NULL, i, LUASQL_PREFIX"statement expected");
	luaL_argcheck (L, !stmt->closed, i, LUASQL_PREFIX"statement is closed");
	return stmt;
}


/*
** Check for valid cursor.
*/
static cur_data *getcursor (lua_State *L, int i)
{
	cur_data *cursor = (cur_data *)luaL_checkudata (L, i, LUASQL_CURSOR_ODBC);
	luaL_argcheck (L, cursor != NULL, i, LUASQL_PREFIX"cursor expected");
	luaL_argcheck (L, !cursor->closed, i, LUASQL_PREFIX"cursor is closed");
	return cursor;
}


/*
** Pushes true and returns 1
*/
static int pass(lua_State *L) {
    lua_pushboolean (L, 1);
    return 1;
}


/*
** Fails with error message from ODBC
** Inputs:
**   type: type of handle used in operation
**   handle: handle used in operation
*/
static int fail(lua_State *L,  const SQLSMALLINT type, const SQLHANDLE handle) {
    SQLCHAR State[6];
    SQLINTEGER NativeError;
    SQLSMALLINT MsgSize, i;
    SQLRETURN ret;
    SQLCHAR Msg[SQL_MAX_MESSAGE_LENGTH];
    luaL_Buffer b;
    lua_pushnil(L);

    luaL_buffinit(L, &b);
    i = 1;
    while (1) {
        ret = SQLGetDiagRec(type, handle, i, State, &NativeError, Msg,
                sizeof(Msg), &MsgSize);
        if (ret == SQL_NO_DATA) break;
        luaL_addlstring(&b, (char*)Msg, MsgSize);
        luaL_addchar(&b, '\n');
        i++;
    }
    luaL_pushresult(&b);
    return 2;
}

static param_data *malloc_stmt_params(SQLSMALLINT c)
{
	param_data *p = (param_data *)malloc(sizeof(param_data)*c);
	memset(p, 0, sizeof(param_data)*c);

	return p;
}

static param_data *free_stmt_params(param_data *data, SQLSMALLINT c)
{
	if(data != NULL) {
		param_data *p = data;

		for(; c>0; ++p, --c) {
			free(p->buf);
		}
		free(data);
	}

	return NULL;
}

/*
** Shuts a statement
** Returns non-zero on error
*/
static int stmt_shut(lua_State *L, stmt_data *stmt)
{
	SQLRETURN ret;

	unlock_obj(L, stmt->conn);
	stmt->closed = 1;

	luaL_unref (L, LUA_REGISTRYINDEX, stmt->paramtypes);
	stmt->paramtypes = LUA_NOREF;
	stmt->params = free_stmt_params(stmt->params, stmt->numparams);

	ret = SQLFreeHandle(hSTMT, stmt->hstmt);
	if (error(ret)) {
		return 1;
	}

	return 0;
}

/*
** Closes a cursor directly
** Returns non-zero on error
*/
static int cur_shut(lua_State *L, cur_data *cur)
{
	/* Nullify structure fields. */
	cur->closed = 1;
	if (error(SQLCloseCursor(cur->stmt->hstmt))) {
		return fail(L, hSTMT, cur->stmt->hstmt);
	}

	/* release col tables */
	luaL_unref (L, LUA_REGISTRYINDEX, cur->colnames);
	luaL_unref (L, LUA_REGISTRYINDEX, cur->coltypes);

	/* release statement and, if hidden, shut it */
	if(unlock_obj(L, cur->stmt) == 0) {
		if(cur->stmt->hidden) {
			return stmt_shut(L, cur->stmt);
		}
	}

	return 0;
}


/*
** Returns the name of an equivalent lua type for a SQL type.
*/
static const char *sqltypetolua (const SQLSMALLINT type) {
    switch (type) {
        case SQL_UNKNOWN_TYPE: case SQL_CHAR: case SQL_VARCHAR:
        case SQL_TYPE_DATE: case SQL_TYPE_TIME: case SQL_TYPE_TIMESTAMP:
        case SQL_DATE: case SQL_INTERVAL: case SQL_TIMESTAMP:
        case SQL_LONGVARCHAR:
        case SQL_WCHAR: case SQL_WVARCHAR: case SQL_WLONGVARCHAR:
#if (ODBCVER >= 0x0350)
		case SQL_GUID:
#endif
            return "string";
        case SQL_BIGINT: case SQL_TINYINT:
        case SQL_INTEGER: case SQL_SMALLINT:
#if LUA_VERSION_NUM>=503
			return "integer";
#endif
		case SQL_NUMERIC: case SQL_DECIMAL:
        case SQL_FLOAT: case SQL_REAL: case SQL_DOUBLE:
            return "number";
        case SQL_BINARY: case SQL_VARBINARY: case SQL_LONGVARBINARY:
            return "binary";	/* !!!!!! nao seria string? */
        case SQL_BIT:
            return "boolean";
        default:
            assert(0);
            return NULL;
    }
}


/*
** Retrieves data from the i_th column in the current row
** Input
**   types: index in stack of column types table
**   hstmt: statement handle
**   i: column number
** Returns:
**   0 if successful, non-zero otherwise;
*/
static int push_column(lua_State *L, int coltypes, const SQLHSTMT hstmt,
        SQLUSMALLINT i) {
    const char *tname;
    char type;
    /* get column type from types table */
	lua_rawgeti (L, LUA_REGISTRYINDEX, coltypes);
	lua_rawgeti (L, -1, i);	/* typename of the column */
    tname = lua_tostring(L, -1);
    if (!tname)
		return luasql_faildirect(L, "invalid type in table.");
    type = tname[1];
    lua_pop(L, 2);	/* pops type name and coltypes table */

    /* deal with data according to type */
    switch (type) {
        /* nUmber */
        case 'u': {
			SQLDOUBLE num;
			SQLLEN got;
			SQLRETURN rc = SQLGetData(hstmt, i, SQL_C_DOUBLE, &num, 0, &got);
			if (error(rc))
				return fail(L, hSTMT, hstmt);
			if (got == SQL_NULL_DATA)
				lua_pushnil(L);
			else
				lua_pushnumber(L, num);
			return 0;
		}
#if LUA_VERSION_NUM>=503
		/* iNteger */
		case 'n': {
			SQLINTEGER num;
			SQLLEN got;
			SQLRETURN rc = SQLGetData(hstmt, i, SQL_C_SLONG, &num, 0, &got);
			if (error(rc))
				return fail(L, hSTMT, hstmt);
			if (got == SQL_NULL_DATA)
				lua_pushnil(L);
			else
				lua_pushinteger(L, num);
			return 0;
		}
#endif
		/* bOol */
        case 'o': {
			SQLCHAR b;
			SQLLEN got;
			SQLRETURN rc = SQLGetData(hstmt, i, SQL_C_BIT, &b, 0, &got);
			if (error(rc))
				return fail(L, hSTMT, hstmt);
			if (got == SQL_NULL_DATA)
				lua_pushnil(L);
			else
				lua_pushboolean(L, b);
			return 0;
		}
        /* sTring */
        case 't':
        /* bInary */
        case 'i': {
			SQLSMALLINT stype = (type == 't') ? SQL_C_CHAR : SQL_C_BINARY;
			SQLLEN got;
			char *buffer;
			luaL_Buffer b;
			SQLRETURN rc;
			luaL_buffinit(L, &b);
			buffer = luaL_prepbuffer(&b);
			rc = SQLGetData(hstmt, i, stype, buffer, LUAL_BUFFERSIZE, &got);
			if (got == SQL_NULL_DATA) {
				lua_pushnil(L);
				return 0;
			}
			/* concat intermediary chunks */
			while (rc == SQL_SUCCESS_WITH_INFO) {
				if (got >= LUAL_BUFFERSIZE || got == SQL_NO_TOTAL) {
					got = LUAL_BUFFERSIZE;
					/* get rid of null termination in string block */
					if (stype == SQL_C_CHAR) got--;
				}
				luaL_addsize(&b, got);
				buffer = luaL_prepbuffer(&b);
				rc = SQLGetData(hstmt, i, stype, buffer,
					LUAL_BUFFERSIZE, &got);
			}
			/* concat last chunk */
			if (rc == SQL_SUCCESS) {
				if (got >= LUAL_BUFFERSIZE || got == SQL_NO_TOTAL) {
					got = LUAL_BUFFERSIZE;
					/* get rid of null termination in string block */
					if (stype == SQL_C_CHAR) got--;
				}
				luaL_addsize(&b, got);
			}
			if (rc == SQL_ERROR) return fail(L, hSTMT, hstmt);
			/* return everything we got */
			luaL_pushresult(&b);
			return 0;
		}
    }
    return 0;
}

/*
** Get another row of the given cursor.
*/
static int cur_fetch (lua_State *L)
{
	cur_data *cur = getcursor (L, 1);
	SQLHSTMT hstmt = cur->stmt->hstmt;
	int ret;
	SQLRETURN rc = SQLFetch(hstmt);
	if (rc == SQL_NO_DATA) {
		/* automatically close cursor when end of resultset is reached */
		if((ret = cur_shut(L, cur)) != 0) {
			return ret;
		}

		lua_pushnil(L);
		return 1;
	} else {
		if (error(rc)) {
			return fail(L, hSTMT, hstmt);
		}
	}

	if (lua_istable (L, 2)) {
		SQLUSMALLINT i;
		const char *opts = luaL_optstring (L, 3, "n");
		int num = strchr (opts, 'n') != NULL;
		int alpha = strchr (opts, 'a') != NULL;
		for (i = 1; i <= cur->numcols; i++) {
			ret = push_column (L, cur->coltypes, hstmt, i);
			if (ret) {
				return ret;
			}
			if (alpha) {
				lua_rawgeti (L, LUA_REGISTRYINDEX, cur->colnames);
				lua_rawgeti (L, -1, i); /* gets column name */
				lua_pushvalue (L, -3); /* duplicates column value */
				lua_rawset (L, 2); /* table[name] = value */
				lua_pop (L, 1);	/* pops colnames table */
			}
			if (num) {
				lua_rawseti (L, 2, i);
			} else {
				lua_pop (L, 1); /* pops value */
			}
		}
		lua_pushvalue (L, 2);
		return 1;	/* return table */
	} else {
		SQLUSMALLINT i;
		luaL_checkstack (L, cur->numcols, LUASQL_PREFIX"too many columns");
		for (i = 1; i <= cur->numcols; i++) {
			ret = push_column (L, cur->coltypes, hstmt, i);
			if (ret) {
				return ret;
			}
		}
		return cur->numcols;
	}
}

/*
** Cursor object collector function
*/
static int cur_gc (lua_State *L) {
	cur_data *cur = (cur_data *) luaL_checkudata (L, 1, LUASQL_CURSOR_ODBC);
	if (cur != NULL && !(cur->closed))
		cur_shut(L, cur);
	return 0;
}

/*
** Closes a cursor.
*/
static int cur_close (lua_State *L)
{
	int res;
	cur_data *cur = (cur_data *) luaL_checkudata (L, 1, LUASQL_CURSOR_ODBC);
	luaL_argcheck (L, cur != NULL, 1, LUASQL_PREFIX"cursor expected");

	if (cur->closed) {
		lua_pushboolean (L, 0);
		lua_pushstring (L, "Cursor is already closed");
		return 2;
	}

	if((res = cur_shut(L, cur)) != 0) {
		return res;
	}

	return pass(L);
}

/*
** Returns the table with column names.
*/
static int cur_colnames (lua_State *L) {
	cur_data *cur = (cur_data *) getcursor (L, 1);
	lua_rawgeti (L, LUA_REGISTRYINDEX, cur->colnames);
	return 1;
}


/*
** Returns the table with column types.
*/
static int cur_coltypes (lua_State *L) {
	cur_data *cur = (cur_data *) getcursor (L, 1);
	lua_rawgeti (L, LUA_REGISTRYINDEX, cur->coltypes);
	return 1;
}


/*
** Creates two tables with the names and the types of the columns.
*/
static int create_colinfo (lua_State *L, cur_data *cur)
{
	SQLCHAR buffer[256];
	SQLSMALLINT namelen, datatype, i;
	SQLRETURN ret;
	int types, names;

	lua_newtable(L);
	types = lua_gettop (L);
	lua_newtable(L);
	names = lua_gettop (L);
	for (i = 1; i <= cur->numcols; i++) {
		ret = SQLDescribeCol(cur->stmt->hstmt, i, buffer, sizeof(buffer),
		                     &namelen, &datatype, NULL, NULL, NULL);
		if (ret == SQL_ERROR) {
			lua_pop(L, 2);
			return -1;
		}

		lua_pushstring (L, (char *)buffer);
		lua_rawseti (L, names, i);
		lua_pushstring(L, sqltypetolua(datatype));
		lua_rawseti (L, types, i);
	}
	cur->colnames = luaL_ref (L, LUA_REGISTRYINDEX);
	cur->coltypes = luaL_ref (L, LUA_REGISTRYINDEX);

	return 0;
}


/*
** Creates a cursor table and leave it on the top of the stack.
*/
static int create_cursor (lua_State *L, int stmt_i, stmt_data *stmt,
                          const SQLSMALLINT numcols)
{
	cur_data *cur;

	lock_obj(L, stmt_i, stmt);

	cur = (cur_data *) LUASQL_NEWUD(L, sizeof(cur_data));
	luasql_setmeta (L, LUASQL_CURSOR_ODBC);

	/* fill in structure */
	cur->closed = 0;
	cur->stmt = stmt;
	cur->numcols = numcols;
	cur->colnames = LUA_NOREF;
	cur->coltypes = LUA_NOREF;

	/* make and store column information table */
	if(create_colinfo (L, cur) < 0) {
		lua_pop(L, 1);
		return fail(L, hSTMT, cur->stmt->hstmt);
	}

	return 1;
}


/*
** Returns the table with statement params.
*/
static int stmt_paramtypes (lua_State *L)
{
	stmt_data *stmt = getstatement(L, 1);
	lua_rawgeti (L, LUA_REGISTRYINDEX, stmt->paramtypes);
	return 1;
}

static int stmt_close(lua_State *L)
{
	stmt_data *stmt = (stmt_data *) luaL_checkudata (L, 1, LUASQL_STATEMENT_ODBC);
	luaL_argcheck (L, stmt != NULL, 1, LUASQL_PREFIX"statement expected");
	luaL_argcheck (L, stmt->lock == 0, 1,
	               LUASQL_PREFIX"there are still open cursors");

	if (stmt->closed) {
		lua_pushboolean (L, 0);
		return 1;
	}

	if(stmt_shut(L, stmt)) {
		return fail(L, hSTMT, stmt->hstmt);
	}

	lua_pushboolean(L, 1);
	return 1;
}


/*
** Closes a connection.
*/
static int conn_close (lua_State *L)
{
	SQLRETURN ret;
	conn_data *conn = (conn_data *)luaL_checkudata(L,1,LUASQL_CONNECTION_ODBC);
	luaL_argcheck (L, conn != NULL, 1, LUASQL_PREFIX"connection expected");
	if (conn->closed) {
		lua_pushboolean (L, 0);
		lua_pushstring (L, "Connection is already closed");
		return 2;
	}
	if (conn->lock > 0) {
		lua_pushboolean (L, 0);
		lua_pushstring (L, "There are open cursors");
		return 2;
	}

	/* Decrement connection counter on environment object */
	unlock_obj(L, conn->env);

	/* Nullify structure fields. */
	conn->closed = 1;
	ret = SQLDisconnect(conn->hdbc);
	if (error(ret)) {
		return fail(L, hDBC, conn->hdbc);
	}

	ret = SQLFreeHandle(hDBC, conn->hdbc);
	if (error(ret)) {
		return fail(L, hDBC, conn->hdbc);
	}

	return pass(L);
}

/*
** Executes the given statement
** Takes:
**  * istmt : location of the statement object on the stack
*/
static int raw_execute(lua_State *L, int istmt)
{
	SQLSMALLINT numcols;

	stmt_data *stmt = getstatement(L, istmt);

	/* execute the statement */
	if (error(SQLExecute(stmt->hstmt))) {
		return fail(L, hSTMT, stmt->hstmt);
	}

	/* determine the number of result columns */
	if (error(SQLNumResultCols(stmt->hstmt, &numcols))) {
		return fail(L, hSTMT, stmt->hstmt);
	}

	if (numcols > 0) {
		/* if there is a results table (e.g., SELECT) */
		return create_cursor(L, -1, stmt, numcols);
	} else {
		/* if action has no results (e.g., UPDATE) */
		SQLLEN numrows;
		if(error(SQLRowCount(stmt->hstmt, &numrows))) {
			return fail(L, hSTMT, stmt->hstmt);
		}

		lua_pushnumber(L, numrows);
		return 1;
	}
}

static int set_param(lua_State *L, stmt_data *stmt, int i, param_data *data)
{
	static SQLLEN cbNull = SQL_NULL_DATA;

	switch(lua_type(L, -1)) {
	case LUA_TNIL: {
			lua_pop(L, 1);

			if(error(SQLBindParameter(stmt->hstmt, i, SQL_PARAM_INPUT, SQL_C_DEFAULT,
			                          SQL_DOUBLE, 0, 0, NULL, 0, &cbNull))) {
				return fail(L, hSTMT, stmt->hstmt);
			}
		}
		break;

	case LUA_TNUMBER: {
			data->buf = malloc(sizeof(double));
			*(double *)data->buf = (double)lua_tonumber(L, -1);
			data->len = sizeof(double);
			data->type = 0;

			lua_pop(L, 1);

			if(error(SQLBindParameter(stmt->hstmt, i, SQL_PARAM_INPUT, SQL_C_DOUBLE,
			                          SQL_DOUBLE, 0, 0, data->buf, data->len,
			                          &data->type))) {
				return fail(L, hSTMT, stmt->hstmt);
			}
		}
		break;

	case LUA_TSTRING: {
			const char *str = lua_tostring(L, -1);
			size_t len = strlen(str);

			data->buf = malloc(len+1);
			memcpy((char *)data->buf, str, len+1);
			data->len = len;
			data->type = SQL_NTS;

			lua_pop(L, 1);

			if(error(SQLBindParameter(stmt->hstmt, i, SQL_PARAM_INPUT, SQL_C_CHAR,
			                          SQL_CHAR, len, 0, data->buf, data->len,
			                          &data->type))) {
				return fail(L, hSTMT, stmt->hstmt);
			}
		}
		break;

	case LUA_TBOOLEAN: {
			data->buf = malloc(sizeof(SQLCHAR));
			*(SQLCHAR *)data->buf = (SQLCHAR)lua_toboolean(L, -1);
			data->len = 0;
			data->type = 0;

			lua_pop(L, 1);

			if(error(SQLBindParameter(stmt->hstmt, i, SQL_PARAM_INPUT, SQL_C_BIT,
			                          SQL_BIT, 0, 0, data->buf, data->len,
			                          &data->type))) {
				return fail(L, hSTMT, stmt->hstmt);
			}
		}
		break;

	default:
		lua_pop(L, 1);
		return luasql_faildirect(L, "unsupported parameter type");
	}

	return 0;
}

/*
** Reads a param table into a statement
*/
static int raw_readparams_table(lua_State *L, stmt_data *stmt, int iparams)
{
	SQLSMALLINT i;
	param_data *data;
	int res = 0;

	free_stmt_params(stmt->params, stmt->numparams);
	stmt->params = malloc_stmt_params(stmt->numparams);
	data = stmt->params;

	for(i=1; i<=stmt->numparams; ++i, ++data) {
		/* not using lua_geti for backwards compat with Lua 5.1/LuaJIT */
		lua_pushnumber(L, i);
		lua_gettable(L, iparams);

		res = set_param(L, stmt, i, data);
		if(res != 0) {
			return res;
		}
	}

	return 0;
}

/*
** Reads a param table into a statement
*/
static int raw_readparams_args(lua_State *L, stmt_data *stmt, int arg, int ltop)
{
	SQLSMALLINT i;
	param_data *data;
	int res = 0;

	free_stmt_params(stmt->params, stmt->numparams);
	stmt->params = malloc_stmt_params(stmt->numparams);
	data = stmt->params;

	for(i=1; i<=stmt->numparams; ++i, ++data, ++arg) {
		if(arg > ltop) {
			lua_pushnil(L);
		} else {
			lua_pushvalue(L, arg);
		}
		res = set_param(L, stmt, i, data);
		if(res != 0) {
			return res;
		}
	}

	return 0;
}

/*
** Executes the prepared statement
** Lua Input: [params]
**   params: A table of parameters to use in the statement
** Lua Returns
**   cursor object: if there are results or
**   row count: number of rows affected by statement if no results
*/
static int stmt_execute(lua_State *L)
{
	int ltop = lua_gettop(L);
	int res;

	/* any parameters to use */
	if(ltop > 1) {
		stmt_data *stmt = getstatement(L, 1);
		if(lua_type(L, 2) == LUA_TTABLE) {
			res = raw_readparams_table(L, stmt, 2);
		} else {
			res = raw_readparams_args(L, stmt, 2, ltop);
		}
		if(res != 0) {
			return res;
		}
	}

	return raw_execute(L, 1);
}

/*
** creates a table of parameter types (maybe)
** Returns: the reference key of the table (noref if unable to build the table)
*/
static int desc_params(lua_State *L, stmt_data *stmt)
{
	SQLSMALLINT i;

	lua_newtable(L);
	for(i=1; i <= stmt->numparams; ++i) {
		SQLSMALLINT type, digits, nullable;
		SQLULEN len;

		/* fun fact: most ODBC drivers don't support this function (MS Access for
		   example), so we can't get a param type table */
		if(error(SQLDescribeParam(stmt->hstmt, i, &type, &len, &digits, &nullable))) {
			lua_pop(L,1);
			return LUA_NOREF;
		}

		lua_pushstring(L, sqltypetolua(type));
		lua_rawseti(L, -2, i);
	}

	return luaL_ref(L, LUA_REGISTRYINDEX);
}

/*
** Prepares a statement
** Lua Input: sql
**   sql: The SQL statement to prepare
** Lua Returns:
**   Statement object
*/
static int conn_prepare(lua_State *L)
{
	conn_data *conn = getconnection(L, 1);
	SQLCHAR *statement = (SQLCHAR *)luaL_checkstring(L, 2);
	SQLHDBC hdbc = conn->hdbc;
	SQLHSTMT hstmt;
	SQLRETURN ret;

	stmt_data *stmt;

	ret = SQLAllocHandle(hSTMT, hdbc, &hstmt);
	if (error(ret)) {
		return fail(L, hDBC, hdbc);
	}

	ret = SQLPrepare(hstmt, statement, SQL_NTS);
	if (error(ret)) {
		ret = fail(L, hSTMT, hstmt);
		SQLFreeHandle(hSTMT, hstmt);
		return ret;
	}

	stmt = (stmt_data *)LUASQL_NEWUD(L, sizeof(stmt_data));
	memset(stmt, 0, sizeof(stmt_data));

	stmt->closed = 0;
	stmt->lock = 0;
	stmt->hidden = 0;
	stmt->conn = conn;
	stmt->hstmt = hstmt;
	if(error(SQLNumParams(hstmt, &stmt->numparams))) {
		int res;
		lua_pop(L, 1);
		res = fail(L, hSTMT, hstmt);
		SQLFreeHandle(hSTMT, hstmt);
		return res;
	}
	stmt->paramtypes = desc_params(L, stmt);
	stmt->params = NULL;

	/* activate statement object */
	luasql_setmeta(L, LUASQL_STATEMENT_ODBC);
	lock_obj(L, 1, conn);

	return 1;
}

/*
** Executes a SQL statement directly
** Lua Input: sql, [params]
**   sql: The SQL statement to execute
**   params: A table of parameters to use in the SQL
** Lua Returns
**   cursor object: if there are results or
**   row count: number of rows affected by statement if no results
*/
static int conn_execute (lua_State *L)
{
	stmt_data *stmt;
	int res, istmt;
	int ltop = lua_gettop(L);

	/* prepare statement */
	if((res = conn_prepare(L)) != 1) {
		return res;
	}
	istmt = lua_gettop(L);
	stmt = getstatement(L, istmt);

	/* because this is a direct execute, statement is hidden from user */
	stmt->hidden = 1;

	/* do we have any parameters */
	if(ltop > 2) {
		if(lua_type(L, 3) == LUA_TTABLE) {
			res = raw_readparams_table(L, stmt, 3);
		} else {
			res = raw_readparams_args(L, stmt, 3, ltop);
		}
		if(res != 0) {
			return res;
		}
	}

	/* do it */
	res = raw_execute(L, istmt);

	/* anything but a cursor, close the statement directly */
	if(!lua_isuserdata(L, -res)) {
		stmt_shut(L, stmt);
	}

	/* tidy up */
	lua_remove(L, istmt);

	return res;
}

/*
** Rolls back a transaction.
*/
static int conn_commit (lua_State *L) {
	conn_data *conn = (conn_data *) getconnection (L, 1);
	SQLRETURN ret = SQLEndTran(hDBC, conn->hdbc, SQL_COMMIT);
	if (error(ret))
		return fail(L, hSTMT, conn->hdbc);
	else
		return pass(L);
}

/*
** Rollback the current transaction.
*/
static int conn_rollback (lua_State *L) {
	conn_data *conn = (conn_data *) getconnection (L, 1);
	SQLRETURN ret = SQLEndTran(hDBC, conn->hdbc, SQL_ROLLBACK);
	if (error(ret))
		return fail(L, hSTMT, conn->hdbc);
	else
		return pass(L);
}

/*
** Sets the auto commit mode
*/
static int conn_setautocommit (lua_State *L) {
	conn_data *conn = (conn_data *) getconnection (L, 1);
	SQLRETURN ret;
	if (lua_toboolean (L, 2)) {
		ret = SQLSetConnectAttr(conn->hdbc, SQL_ATTR_AUTOCOMMIT,
			(SQLPOINTER) SQL_AUTOCOMMIT_ON, 0);
	} else {
		ret = SQLSetConnectAttr(conn->hdbc, SQL_ATTR_AUTOCOMMIT,
			(SQLPOINTER) SQL_AUTOCOMMIT_OFF, 0);
	}
	if (error(ret))
		return fail(L, hSTMT, conn->hdbc);
	else
		return pass(L);
}


/*
** Create a new Connection object and push it on top of the stack.
*/
static int create_connection (lua_State *L, int o, env_data *env, SQLHDBC hdbc)
{
	conn_data *conn = (conn_data *)LUASQL_NEWUD(L, sizeof(conn_data));

	/* set auto commit mode */
	SQLRETURN ret = SQLSetConnectAttr(hdbc, SQL_ATTR_AUTOCOMMIT,
	                                  (SQLPOINTER) SQL_AUTOCOMMIT_ON, 0);
	if (error(ret)) {
		return fail(L, hDBC, hdbc);
	}

	luasql_setmeta (L, LUASQL_CONNECTION_ODBC);

	/* fill in structure */
	conn->closed = 0;
	conn->lock = 0;
	conn->env = env;
	conn->hdbc = hdbc;

	lock_obj(L, 1, env);

	return 1;
}


/*
** Creates and returns a connection object
** Lua Input: source [, user [, pass]]
**   source: data source
**   user, pass: data source authentication information
** Lua Returns:
**   connection object if successful
**   nil and error message otherwise.
*/
static int env_connect (lua_State *L) {
	env_data *env = (env_data *) getenvironment (L, 1);
	SQLCHAR *sourcename = (SQLCHAR*)luaL_checkstring (L, 2);
	SQLCHAR *username = (SQLCHAR*)luaL_optstring (L, 3, NULL);
	SQLCHAR *password = (SQLCHAR*)luaL_optstring (L, 4, NULL);
	SQLHDBC hdbc;
	SQLRETURN ret;

	/* tries to allocate connection handle */
	ret = SQLAllocHandle (hDBC, env->henv, &hdbc);
	if (error(ret))
		return luasql_faildirect (L, "connection allocation error.");

	/* tries to connect handle */
	ret = SQLConnect (hdbc, sourcename, SQL_NTS,
		username, SQL_NTS, password, SQL_NTS);
	if (error(ret)) {
		ret = fail(L, hDBC, hdbc);
		SQLFreeHandle(hDBC, hdbc);
		return ret;
	}

	/* success, return connection object */
	return create_connection (L, 1, env, hdbc);
}

/*
** Environment object collector function
*/
static int env_gc (lua_State *L)
{
	SQLRETURN ret;
	env_data *env = (env_data *)luaL_checkudata(L, 1, LUASQL_ENVIRONMENT_ODBC);
	if (env != NULL && !(env->closed)) {
		env->closed = 1;
		ret = SQLFreeHandle (hENV, env->henv);
		if (error (ret)) {
			int ret2 = fail (L, hENV, env->henv);
			env->henv = NULL;
			return ret2;
		}
	}
	return 0;
}


/*
** Closes an environment object
*/
static int env_close (lua_State *L)
{
	SQLRETURN ret;
	env_data *env = (env_data *)luaL_checkudata(L, 1, LUASQL_ENVIRONMENT_ODBC);
	luaL_argcheck (L, env != NULL, 1, LUASQL_PREFIX"environment expected");
	if (env->closed) {
		lua_pushboolean (L, 0);
		lua_pushstring (L, "Environment is already closed");
		return 2;
	}
	if (env->lock > 0) {
		lua_pushboolean (L, 0);
		lua_pushstring (L, "There are open connections");
		return 2;
	}

	env->closed = 1;
	ret = SQLFreeHandle (hENV, env->henv);
	if (error(ret)) {
		int ret2 = fail(L, hENV, env->henv);
		env->henv = NULL;
		return ret2;
	}
	return pass(L);
}


/*
** Create metatables for each class of object.
*/
static void create_metatables (lua_State *L) {
	struct luaL_Reg environment_methods[] = {
		{"__gc", env_gc},
		{"__close", env_gc},
		{"close", env_close},
		{"connect", env_connect},
		{NULL, NULL},
	};
	struct luaL_Reg connection_methods[] = {
		{"__gc", conn_close}, /* Should this method be changed? */
		{"__close", conn_close},
		{"close", conn_close},
		{"prepare", conn_prepare},
		{"execute", conn_execute},
		{"commit", conn_commit},
		{"rollback", conn_rollback},
		{"setautocommit", conn_setautocommit},
		{NULL, NULL},
	};
	struct luaL_Reg statement_methods[] = {
		{"__gc", stmt_close}, /* Should this method be changed? */
		{"__close", stmt_close},
		{"close", stmt_close},
		{"execute", stmt_execute},
		{"getparamtypes", stmt_paramtypes},
		{NULL, NULL},
	};
	struct luaL_Reg cursor_methods[] = {
		{"__gc", cur_gc}, /* Should this method be changed? */
		{"__close", cur_gc},
		{"close", cur_close},
		{"fetch", cur_fetch},
		{"getcoltypes", cur_coltypes},
		{"getcolnames", cur_colnames},
		{NULL, NULL},
	};
	luasql_createmeta (L, LUASQL_ENVIRONMENT_ODBC, environment_methods);
	luasql_createmeta (L, LUASQL_CONNECTION_ODBC, connection_methods);
	luasql_createmeta (L, LUASQL_STATEMENT_ODBC, statement_methods);
	luasql_createmeta (L, LUASQL_CURSOR_ODBC, cursor_methods);
	lua_pop (L, 4);
}


/*
** Creates an Environment and returns it.
*/
static int create_environment (lua_State *L)
{
	env_data *env;
	SQLHENV henv;
	SQLRETURN ret = SQLAllocHandle(hENV, SQL_NULL_HANDLE, &henv);
	if (error(ret)) {
		return luasql_faildirect(L, "error creating environment.");
	}

	ret = SQLSetEnvAttr (henv, SQL_ATTR_ODBC_VERSION, (void *)SQL_OV_ODBC3, 0);
	if (error(ret)) {
		ret = luasql_faildirect (L, "error setting SQL version.");
		SQLFreeHandle (hENV, henv);
		return ret;
	}

	env = (env_data *)LUASQL_NEWUD (L, sizeof (env_data));
	luasql_setmeta (L, LUASQL_ENVIRONMENT_ODBC);
	/* fill in structure */
	env->closed = 0;
	env->lock = 0;
	env->henv = henv;
	return 1;
}


/*
** Creates the metatables for the objects and registers the
** driver open method.
*/
LUASQL_API int luaopen_luasql_odbc (lua_State *L) {
	struct luaL_Reg driver[] = {
		{"odbc", create_environment},
		{NULL, NULL},
	};
	create_metatables (L);
	lua_newtable (L);
	luaL_setfuncs (L, driver, 0);
	luasql_set_info (L);
	return 1;
}
