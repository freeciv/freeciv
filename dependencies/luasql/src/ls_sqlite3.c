/*
** LuaSQL, SQLite driver
** Author: Tiago Dionizio, Eduardo Quintao
** See Copyright Notice in license.html
*/

#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "sqlite3.h"

#include "lua.h"
#include "lauxlib.h"


#include "luasql.h"

#define LUASQL_ENVIRONMENT_SQLITE "SQLite3 environment"
#define LUASQL_CONNECTION_SQLITE "SQLite3 connection"
#define LUASQL_CURSOR_SQLITE "SQLite3 cursor"

typedef struct
{
  short       closed;
} env_data;


typedef struct
{
  short        closed;
  int          env;                /* reference to environment */
  short        auto_commit;        /* 0 for manual commit */
  unsigned int cur_counter;
  sqlite3      *sql_conn;
} conn_data;


typedef struct
{
  short       closed;
  int         conn;               /* reference to connection */
  int         numcols;            /* number of columns */
  int         colnames, coltypes; /* reference to column information tables */
  conn_data   *conn_data;         /* reference to connection for cursor */
  sqlite3_stmt  *sql_vm;
} cur_data;


/*
** Check for valid environment.
*/
static env_data *getenvironment(lua_State *L) {
  env_data *env = (env_data *)luaL_checkudata(L, 1, LUASQL_ENVIRONMENT_SQLITE);
  luaL_argcheck(L, env != NULL, 1, LUASQL_PREFIX"environment expected");
  luaL_argcheck(L, !env->closed, 1, LUASQL_PREFIX"environment is closed");
  return env;
}


/*
** Check for valid connection.
*/
static conn_data *getconnection(lua_State *L) {
  conn_data *conn = (conn_data *)luaL_checkudata (L, 1, LUASQL_CONNECTION_SQLITE);
  luaL_argcheck(L, conn != NULL, 1, LUASQL_PREFIX"connection expected");
  luaL_argcheck(L, !conn->closed, 1, LUASQL_PREFIX"connection is closed");
  return conn;
}


/*
** Check for valid cursor.
*/
static cur_data *getcursor(lua_State *L) {
  cur_data *cur = (cur_data *)luaL_checkudata (L, 1, LUASQL_CURSOR_SQLITE);
  luaL_argcheck(L, cur != NULL, 1, LUASQL_PREFIX"cursor expected");
  luaL_argcheck(L, !cur->closed, 1, LUASQL_PREFIX"cursor is closed");
  return cur;
}

/*
** Closes the cursor and nullify all structure fields.
*/
static void cur_nullify(lua_State *L, cur_data *cur)
{
  conn_data *conn;

  /* Nullify structure fields. */
  cur->closed = 1;
  cur->sql_vm = NULL;
  /* Decrement cursor counter on connection object */
  lua_rawgeti (L, LUA_REGISTRYINDEX, cur->conn);
  conn = lua_touserdata (L, -1);
  conn->cur_counter--;

  luaL_unref(L, LUA_REGISTRYINDEX, cur->conn);
  luaL_unref(L, LUA_REGISTRYINDEX, cur->colnames);
  luaL_unref(L, LUA_REGISTRYINDEX, cur->coltypes);
}


/*
** Finalizes the vm
** Return nil + errmsg or nil in case of success
*/
static int finalize(lua_State *L, cur_data *cur) {
  const char *errmsg;
  if (sqlite3_finalize(cur->sql_vm) != SQLITE_OK)
    {
      errmsg = sqlite3_errmsg(cur->conn_data->sql_conn);
      cur_nullify(L, cur);
      return luasql_faildirect(L, errmsg);
    }
  cur_nullify(L, cur);
  lua_pushnil(L);
  return 1;
}


static void push_column(lua_State *L, sqlite3_stmt *vm, int column) {
  switch (sqlite3_column_type(vm, column)) {
  case SQLITE_INTEGER:
#if LUA_VERSION_NUM >= 503
    lua_pushinteger(L, sqlite3_column_int64(vm, column));
#else
    // Preserves precision of integers up to 2^53.
    lua_pushnumber(L, sqlite3_column_int64(vm, column));
#endif
    break;
  case SQLITE_FLOAT:
    lua_pushnumber(L, sqlite3_column_double(vm, column));
    break;
  case SQLITE_TEXT:
    lua_pushlstring(L, (const char *)sqlite3_column_text(vm, column),
		    (size_t)sqlite3_column_bytes(vm, column));
    break;
  case SQLITE_BLOB:
    lua_pushlstring(L, sqlite3_column_blob(vm, column),
		    (size_t)sqlite3_column_bytes(vm, column));
    break;
  case SQLITE_NULL:
    lua_pushnil(L);
    break;
  default:
    luaL_error(L, LUASQL_PREFIX"Unrecognized column type");
    break;
  }
}


/*
** Get another row of the given cursor.
*/
static int cur_fetch (lua_State *L) {
  cur_data *cur = getcursor(L);
  sqlite3_stmt *vm = cur->sql_vm;
  int res;

  if (vm == NULL)
    return 0;

  res = sqlite3_step(vm);

  /* no more results? */
  if (res == SQLITE_DONE)
    return finalize(L, cur);

  if (res != SQLITE_ROW)
    return finalize(L, cur);

  if (lua_istable (L, 2))
    {
      int i;
      const char *opts = luaL_optstring(L, 3, "n");

      if (strchr(opts, 'n') != NULL)
        {
        /* Copy values to numerical indices */
          for (i = 0; i < cur->numcols;)
            {
              push_column(L, vm, i);
              lua_rawseti(L, 2, ++i);
            }
        }
      if (strchr(opts, 'a') != NULL)
        {
          /* Copy values to alphanumerical indices */
          lua_rawgeti(L, LUA_REGISTRYINDEX, cur->colnames);

          for (i = 0; i < cur->numcols; i++)
            {
              lua_rawgeti(L, -1, i+1);
              push_column(L, vm, i);
              lua_rawset (L, 2);
            }
        }
      lua_pushvalue(L, 2);
      return 1; /* return table */
    }
  else
    {
      int i;
      luaL_checkstack (L, cur->numcols, LUASQL_PREFIX"too many columns");
      for (i = 0; i < cur->numcols; ++i)
        push_column(L, vm, i);
      return cur->numcols; /* return #numcols values */
    }
}


/*
** Cursor object collector function
*/
static int cur_gc(lua_State *L)
{
  cur_data *cur = (cur_data *)luaL_checkudata(L, 1, LUASQL_CURSOR_SQLITE);
  if (cur != NULL && !(cur->closed))
    {
      sqlite3_finalize(cur->sql_vm);
      cur_nullify(L, cur);
    }
  return 0;
}


/*
** Close the cursor on top of the stack.
** Return 1
*/
static int cur_close(lua_State *L)
{
  cur_data *cur = (cur_data *)luaL_checkudata(L, 1, LUASQL_CURSOR_SQLITE);
  luaL_argcheck(L, cur != NULL, 1, LUASQL_PREFIX"cursor expected");
  if (cur->closed) {
    lua_pushboolean (L, 0);
    lua_pushstring(L, "cursor is already closed");
    return 2;
  }

  cur->closed = 1;
  sqlite3_finalize(cur->sql_vm);
  cur_nullify(L, cur);
  lua_pushboolean(L, 1);
  return 1;
}


/*
** Return the list of field names.
*/
static int cur_getcolnames(lua_State *L)
{
  cur_data *cur = getcursor(L);
  lua_rawgeti(L, LUA_REGISTRYINDEX, cur->colnames);
  return 1;
}


/*
** Return the list of field types.
*/
static int cur_getcoltypes(lua_State *L)
{
  cur_data *cur = getcursor(L);
  lua_rawgeti(L, LUA_REGISTRYINDEX, cur->coltypes);
  return 1;
}


/*
** Create a new Cursor object and push it on top of the stack.
*/
/* static int create_cursor(lua_State *L, int conn, sqlite3_stmt *sql_vm,
   int numcols, const char **row, const char **col_info)*/
static int create_cursor(lua_State *L, int o, conn_data *conn,
			 sqlite3_stmt *sql_vm, int numcols)
{
  int i;
  cur_data *cur = (cur_data*)LUASQL_NEWUD(L, sizeof(cur_data));
  luasql_setmeta (L, LUASQL_CURSOR_SQLITE);

  /* increment cursor count for the connection creating this cursor */
  conn->cur_counter++;

  /* fill in structure */
  cur->closed = 0;
  cur->conn = LUA_NOREF;
  cur->numcols = numcols;
  cur->colnames = LUA_NOREF;
  cur->coltypes = LUA_NOREF;
  cur->sql_vm = sql_vm;
  cur->conn_data = conn;

  lua_pushvalue(L, o);
  cur->conn = luaL_ref(L, LUA_REGISTRYINDEX);

  /* create table with column names */
  lua_newtable(L);
  for (i = 0; i < numcols;)
    {
      lua_pushstring(L, sqlite3_column_name(sql_vm, i));
      lua_rawseti(L, -2, ++i);
    }
  cur->colnames = luaL_ref(L, LUA_REGISTRYINDEX);

  /* create table with column types */
  lua_newtable(L);
  for (i = 0; i < numcols;)
    {
      lua_pushstring(L, sqlite3_column_decltype(sql_vm, i));
      lua_rawseti(L, -2, ++i);
    }
  cur->coltypes = luaL_ref(L, LUA_REGISTRYINDEX);

  return 1;
}


/*
** Connection object collector function
*/
static int conn_gc(lua_State *L)
{
  conn_data *conn = (conn_data *)luaL_checkudata(L, 1, LUASQL_CONNECTION_SQLITE);
  if (conn != NULL && !(conn->closed))
    {
      /* Nullify structure fields. */
      conn->closed = 1;
      luaL_unref(L, LUA_REGISTRYINDEX, conn->env);
      sqlite3_close(conn->sql_conn);
    }
  return 0;
}


/*
** Close a Connection object.
*/
static int conn_close(lua_State *L)
{
  conn_data *conn = (conn_data *)luaL_checkudata(L, 1, LUASQL_CONNECTION_SQLITE);
  luaL_argcheck (L, conn != NULL, 1, LUASQL_PREFIX"connection expected");
  if (conn->closed)
  {
    lua_pushboolean(L, 0);
    lua_pushstring(L, "Connection is already closed");
    return 2;
  }

  if (conn->cur_counter > 0)
  {
    lua_pushboolean(L, 0);
    lua_pushstring(L, "There are open cursors");
    return 2;
  }

  conn->closed = 1;

  lua_pushboolean(L, 1);
  return 1;
}

static int conn_escape(lua_State *L)
{
  const char *from = luaL_checklstring (L, 2, 0);
  char *escaped = sqlite3_mprintf("%q", from);
  if (escaped == NULL)
    {
      lua_pushnil(L);
    }
  else
    {
      lua_pushstring(L, escaped);
      sqlite3_free(escaped);
    }
  return 1;
}


/*
** Bind one parameter.
** Supported are the data types nil, string, boolean, number.
*/
static int set_param(lua_State *L, sqlite3_stmt *vm, int param_nr, int arg)
{
  int tt = lua_type(L, arg);
  int rc = 0;

  switch (tt) {
    case LUA_TNIL:
    rc = sqlite3_bind_null(vm, param_nr);
    break;

    case LUA_TSTRING: {
      size_t s_len;
      const char *s = lua_tolstring(L, arg, &s_len);
      rc = sqlite3_bind_null(vm, param_nr);
      rc = sqlite3_bind_text(vm, param_nr, s, s_len, SQLITE_TRANSIENT);
      break;
    }

    case LUA_TBOOLEAN: {
      int val = lua_tointeger(L, arg);
      rc = sqlite3_bind_int(vm, param_nr, val);
      break;
    }

    case LUA_TNUMBER: {
#if defined(lua_isinteger)
      if (lua_isinteger(L, arg)) {
        lua_Integer val = lua_tointeger(L, arg);
        rc = sqlite3_bind_int64(vm, param_nr, val);
      } else {
#endif
        double val = lua_tonumber(L, arg);
        rc = sqlite3_bind_double(vm, param_nr, val);
#if defined(lua_isinteger)
      }
#endif
      break;
    }

    default:
    luaL_error(L, LUASQL_PREFIX"unhandled data type %s in parameter binding",
      lua_typename(L, tt));
  }

  return rc;
}

static int raw_readparams_args(lua_State *L, sqlite3_stmt *vm, int arg, int ltop)
{
  int param_count, param_nr, rc = 0;

  param_count = sqlite3_bind_parameter_count(vm);
  if (ltop - arg + 1 != param_count)
    luaL_error(L, LUASQL_PREFIX"wrong number of parameters: expected=%d, given=%d",
      param_count, ltop - arg + 1);

  for (param_nr=1; param_nr <= param_count; param_nr ++, arg ++) {
    rc = set_param(L, vm, param_nr, arg);
    if (rc)
      break;
  }

  return rc;
}

/*
** Bind all parameters from the given table.
** The table indices can be integers or strings.
** Unbound parameters, or duplicate bindings are not detected.
*/
static int raw_readparams_table(lua_State *L, sqlite3_stmt *vm, int arg)
{
  int param_nr, rc = 0;

  lua_pushnil(L);

  while (lua_next(L, arg)) {		// [arg]=table, [-2]=key, [-1]=val
#if defined(lua_isinteger)
    int tt =
#endif
      lua_type(L, -2);
#if defined(lua_isinteger)
    if (tt == LUA_TNUMBER && lua_isinteger(L, -2)) {
      param_nr = lua_tointeger(L, -2);
    } else {
#endif
      const char *param_name = lua_tostring(L, -2);
      param_nr = sqlite3_bind_parameter_index(vm, param_name);
      if (param_nr == 0)
        luaL_error(L, LUASQL_PREFIX"binding to invalid parameter name %s\n",
          param_name);
#if defined(lua_isinteger)
    }
#endif
    set_param(L, vm, param_nr, -1);
    lua_pop(L, 1);
  }

  return rc;
}

/*
** Execute an SQL statement.
** Return a Cursor object if the statement is a query, otherwise
** return the number of tuples affected by the statement.
*/
static int conn_execute(lua_State *L)
{
  conn_data *conn = getconnection(L);
  const char *statement = luaL_checkstring(L, 2);
  int res;
  sqlite3_stmt *vm;
  const char *errmsg;
  int numcols;
  const char *tail;

#if SQLITE_VERSION_NUMBER > 3006013
  res = sqlite3_prepare_v2(conn->sql_conn, statement, -1, &vm, &tail);
#else
  res = sqlite3_prepare(conn->sql_conn, statement, -1, &vm, &tail);
#endif
  if (res != SQLITE_OK)
    {
      errmsg = sqlite3_errmsg(conn->sql_conn);
      return luasql_faildirect(L, errmsg);
    }

  /* Bind parameters (if any) */
  int ltop = lua_gettop(L);
  if (ltop > 2) {
    if (ltop == 3 && lua_type(L, 3) == LUA_TTABLE) {
      res = raw_readparams_table(L, vm, 3);
    } else if (ltop >= 3) {
      res = raw_readparams_args(L, vm, 3, ltop);
    } else {
      luaL_error(L, LUASQL_PREFIX"parameters are either one table or positional");
    }
    if (res)
      return res;
  }

  /* process first result to retrieve query information and type */
  res = sqlite3_step(vm);
  numcols = sqlite3_column_count(vm);

  /* real query? if empty, must have numcols!=0 */
  if ((res == SQLITE_ROW) || ((res == SQLITE_DONE) && numcols))
    {
      sqlite3_reset(vm);
      return create_cursor(L, 1, conn, vm, numcols);
    }

  if (res == SQLITE_DONE) /* and numcols==0, INSERT,UPDATE,DELETE statement */
    {
      sqlite3_finalize(vm);
      /* return number of columns changed */
      lua_pushnumber(L, sqlite3_changes(conn->sql_conn));
      return 1;
    }

  /* error */
  errmsg = sqlite3_errmsg(conn->sql_conn);
  sqlite3_finalize(vm);
  return luasql_faildirect(L, errmsg);
}


/*
** Commit the current transaction.
*/
static int conn_commit(lua_State *L)
{
  char *errmsg;
  conn_data *conn = getconnection(L);
  int res;
  const char *sql = "COMMIT";

  if (conn->auto_commit == 0) sql = "COMMIT;BEGIN";

  res = sqlite3_exec(conn->sql_conn, sql, NULL, NULL, &errmsg);

  if (res != SQLITE_OK)
    {
      lua_pushnil(L);
      lua_pushliteral(L, LUASQL_PREFIX);
      lua_pushstring(L, errmsg);
      sqlite3_free(errmsg);
      lua_concat(L, 2);
      return 2;
    }
  lua_pushboolean(L, 1);
  return 1;
}


/*
** Rollback the current transaction.
*/
static int conn_rollback(lua_State *L)
{
  char *errmsg;
  conn_data *conn = getconnection(L);
  int res;
  const char *sql = "ROLLBACK";

  if (conn->auto_commit == 0) sql = "ROLLBACK;BEGIN";

  res = sqlite3_exec(conn->sql_conn, sql, NULL, NULL, &errmsg);
  if (res != SQLITE_OK)
    {
      lua_pushnil(L);
      lua_pushliteral(L, LUASQL_PREFIX);
      lua_pushstring(L, errmsg);
      sqlite3_free(errmsg);
      lua_concat(L, 2);
      return 2;
    }
  lua_pushboolean(L, 1);
  return 1;
}

static int conn_getlastautoid(lua_State *L)
{
  conn_data *conn = getconnection(L);
  lua_pushnumber(L, sqlite3_last_insert_rowid(conn->sql_conn));
  return 1;
}


/*
** Set "auto commit" property of the connection.
** If 'true', then rollback current transaction.
** If 'false', then start a new transaction.
*/
static int conn_setautocommit(lua_State *L)
{
  conn_data *conn = getconnection(L);
  if (lua_toboolean(L, 2))
    {
      conn->auto_commit = 1;
      /* undo active transaction - ignore errors */
      (void) sqlite3_exec(conn->sql_conn, "ROLLBACK", NULL, NULL, NULL);
    }
  else
    {
      char *errmsg;
      int res;
      conn->auto_commit = 0;
      res = sqlite3_exec(conn->sql_conn, "BEGIN", NULL, NULL, &errmsg);
      if (res != SQLITE_OK)
        {
	  lua_pushliteral(L, LUASQL_PREFIX);
	  lua_pushstring(L, errmsg);
	  sqlite3_free(errmsg);
	  lua_concat(L, 2);
	  lua_error(L);
        }
    }
  lua_pushboolean(L, 1);
  return 1;
}


/*
** Create a new Connection object and push it on top of the stack.
*/
static int create_connection(lua_State *L, int env, sqlite3 *sql_conn)
{
  conn_data *conn = (conn_data*)LUASQL_NEWUD(L, sizeof(conn_data));
  luasql_setmeta(L, LUASQL_CONNECTION_SQLITE);

  /* fill in structure */
  conn->closed = 0;
  conn->env = LUA_NOREF;
  conn->auto_commit = 1;
  conn->sql_conn = sql_conn;
  conn->cur_counter = 0;
  lua_pushvalue (L, env);
  conn->env = luaL_ref (L, LUA_REGISTRYINDEX);
  return 1;
}


/*
** Connects to a data source.
*/
static int env_connect(lua_State *L)
{
  const char *sourcename;
  sqlite3 *conn;
  const char *errmsg;
  int res;
  bool readOnlyMode = false;
  int mode;

  getenvironment(L);  /* validate environment */

  if (lua_isboolean(L, 4)) {
    if (lua_toboolean(L, 4)) {
      readOnlyMode = true;
    }
  }

  sourcename = luaL_checkstring(L, 2);
#if SQLITE_VERSION_NUMBER > 3006013
  if (strstr(sourcename, ":memory:")) /* TODO: rework this and get/add param 'flag' for sqlite3_open_v2 - see TODO below */
  {
    if (readOnlyMode) {
      mode = SQLITE_OPEN_READONLY | SQLITE_OPEN_MEMORY;
    } else {
      mode = SQLITE_OPEN_READWRITE | SQLITE_OPEN_MEMORY;
    }
  }
  else
  {
    if (readOnlyMode) {
      mode = SQLITE_OPEN_READONLY;
    } else {
      mode = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE;
    }
  }
  res = sqlite3_open_v2(sourcename, &conn, mode, NULL);
#else
  res = sqlite3_open(sourcename, &conn);
#endif
  if (res != SQLITE_OK)
    {
      errmsg = sqlite3_errmsg(conn);
      luasql_faildirect(L, errmsg);
      sqlite3_close(conn);
      return 2;
    }

  if (lua_isnumber(L, 3)) {
  	sqlite3_busy_timeout(conn, lua_tonumber(L,3)); /* TODO: remove this */
  }

  return create_connection(L, 1, conn);
}


/*
** Environment object collector function.
*/
static int env_gc (lua_State *L)
{
  env_data *env = (env_data *)luaL_checkudata(L, 1, LUASQL_ENVIRONMENT_SQLITE);
  if (env != NULL && !(env->closed))
    env->closed = 1;
  return 0;
}


/*
** Close environment object.
*/
static int env_close (lua_State *L)
{
  env_data *env = (env_data *)luaL_checkudata(L, 1, LUASQL_ENVIRONMENT_SQLITE);
  luaL_argcheck(L, env != NULL, 1, LUASQL_PREFIX"environment expected");
  if (env->closed) {
    lua_pushboolean(L, 0);
    lua_pushstring(L, "env is already closed");
		return 2;
  }

  env->closed = 1;

  lua_pushboolean(L, 1);
  return 1;
}


/*
** Sets the timeout for a lock in the connection.
static int opts_settimeout  (lua_State *L)
{
	conn_data *conn = getconnection(L);
	int milisseconds = luaL_checknumber(L, 2);
	lua_pushnumber(L, sqlite3_busy_timeout(conn->sql_conn, milisseconds));
	return 1;
}
*/

/*
** Create metatables for each class of object.
*/
static void create_metatables (lua_State *L)
{
  struct luaL_Reg environment_methods[] = {
    {"__gc", env_gc},
    {"__close", env_gc},
    {"close", env_close},
    {"connect", env_connect},
    {NULL, NULL},
  };
  struct luaL_Reg connection_methods[] = {
    {"__gc", conn_gc},
    {"__close", conn_gc},
    {"close", conn_close},
    {"escape", conn_escape},
//    {"prepare", conn_prepare},
    {"execute", conn_execute},
    {"commit", conn_commit},
    {"rollback", conn_rollback},
    {"setautocommit", conn_setautocommit},
    {"getlastautoid", conn_getlastautoid},
    {NULL, NULL},
  };
  struct luaL_Reg cursor_methods[] = {
    {"__gc", cur_gc},
    {"__close", cur_gc},
    {"close", cur_close},
    {"getcolnames", cur_getcolnames},
    {"getcoltypes", cur_getcoltypes},
    {"fetch", cur_fetch},
    {NULL, NULL},
  };
  luasql_createmeta(L, LUASQL_ENVIRONMENT_SQLITE, environment_methods);
  luasql_createmeta(L, LUASQL_CONNECTION_SQLITE, connection_methods);
  luasql_createmeta(L, LUASQL_CURSOR_SQLITE, cursor_methods);
  lua_pop (L, 3);
}

/*
** Creates an Environment and returns it.
*/
static int create_environment (lua_State *L)
{
  env_data *env = (env_data *)LUASQL_NEWUD(L, sizeof(env_data));
  luasql_setmeta(L, LUASQL_ENVIRONMENT_SQLITE);

  /* fill in structure */
  env->closed = 0;
  return 1;
}


/*
** Creates the metatables for the objects and registers the
** driver open method.
*/
LUASQL_API int luaopen_luasql_sqlite3(lua_State *L)
{
  struct luaL_Reg driver[] = {
    {"sqlite3", create_environment},
    {NULL, NULL},
  };
  create_metatables (L);
  lua_newtable (L);
  luaL_setfuncs (L, driver, 0);
  luasql_set_info (L);
  lua_pushliteral (L, "_CLIENTVERSION");
  lua_pushliteral (L, SQLITE_VERSION);
  lua_settable (L, -3);
  return 1;
}
