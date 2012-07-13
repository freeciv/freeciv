-- Freeciv - Copyright (C) 2011 - The Freeciv Project
--   This program is free software; you can redistribute it and/or modify
--   it under the terms of the GNU General Public License as published by
--   the Free Software Foundation; either version 2, or (at your option)
--   any later version.
--
--   This program is distributed in the hope that it will be useful,
--   but WITHOUT ANY WARRANTY; without even the implied warranty of
--   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
--   GNU General Public License for more details.

-- This file is the Freeciv server`s interface to the database backend
-- when authentication is enabled. See doc/README.fcdb.

-- **************************************************************************
-- basic mysql functions
-- **************************************************************************

local dbh = nil

-- connect to a MySQL database (or stop with an error)
local function mysql_connect()
  local err -- error message

  if dbh then
    dbh:close()
  end

  local sql = luasql.mysql()

  log.verbose('MySQL database version is %s.', luasql._MYSQLVERSION)

  -- Load the database parameters.
  local database = fcdb.option(fcdb.param.DATABASE)
  local user     = fcdb.option(fcdb.param.USER)
  local password = fcdb.option(fcdb.param.PASSWORD)
  local host     = fcdb.option(fcdb.param.HOST)
  local port     = fcdb.option(fcdb.param.PORT)

  dbh, err = sql:connect(database, user, password, host, port)
  if not dbh then
    log.error('[mysql:connect]: %s', err)
    return fcdb.status.ERROR
  else
    return fcdb.status.TRUE
  end
end

-- open a SQLite database (or stop with an error)
local function sqlite_connect()
  local err -- error message

  if dbh then
    dbh:close()
  end

  local sql = luasql.sqlite3()

  -- Load the database parameters.
  local database = fcdb.option(fcdb.param.DATABASE)
  -- USER/PASSWORD/HOST/PORT ignored for SQLite

  dbh, err = sql:connect(database)
  if not dbh then
    log.error('[sqlite:connect]: %s', err)
    return fcdb.status.ERROR
  else
    return fcdb.status.TRUE
  end
end

-- execute a sql query
local function execute(query)
  local res -- result handle
  local err -- error message

  if not dbh then
    return fcdb.status.ERROR, "[execute] Invalid database handle."
  end

  -- log.verbose("Database query: %s", query)

  res, err = dbh:execute(query)
  if not res then
    log.error("[luasql:execute]: %s", err)
    return fcdb.status.ERROR, err
  else
    return fcdb.status.TRUE, res
  end
end

-- DIRTY: return a string to put in a database query which gets the
-- current time (in seconds since the epoch, UTC).
-- (This should be replaced with Lua os.time() once the script has access
-- to this, see <http://gna.org/bugs/?19729>.)
function sql_time()
  local backend = fcdb.option(fcdb.param.BACKEND)
  if backend == 'mysql' then
    return 'UNIX_TIMESTAMP()'
  elseif backend == 'sqlite' then
    return 'strftime(\'%s\',\'now\')'
  else
    log.error('Don\'t know how to do timestamps for database backend \'%s\'', backend)
    return 'ERROR'
  end
end

-- Set up tables for an SQLite database.
-- (Since there`s no concept of user rights, we can do this directly from Lua,
-- without needing a separate script like MySQL. The server operator can do
-- "/fcdb lua sqlite_createdb()" from the server prompt.)
function sqlite_createdb()
  local query
  local res

  if fcdb.option(fcdb.param.BACKEND) ~= 'sqlite' then
    log.error("'backend' in configuration file must be 'sqlite'")
    return fcdb.status.ERROR
  end

  local table_user = fcdb.option(fcdb.param.TABLE_USER)
  local table_log = fcdb.option(fcdb.param.TABLE_LOG)

  if not dbh then
    log.error("Missing database connection...")
    return fcdb.status.ERROR
  end

  query = string.format([[
CREATE TABLE %s (
  id INTEGER NOT NULL PRIMARY KEY AUTOINCREMENT,
  name VARCHAR(48) default NULL UNIQUE,
  password VARCHAR(32) default NULL,
  email VARCHAR default NULL,
  createtime INTEGER default NULL,
  accesstime INTEGER default NULL,
  address VARCHAR default NULL,
  createaddress VARCHAR default NULL,
  logincount INTEGER default '0'
);
]], table_user)
  status, res = execute(query)
  if status == fcdb.status.TRUE then
    log.normal("Successfully created user table '%s'", table_user)
  else
    log.error("Error creating user table '%s'", table_user)
    return fcdb.status.ERROR
  end

  query = string.format([[
CREATE TABLE %s (
  id INTEGER NOT NULL PRIMARY KEY AUTOINCREMENT,
  name VARCHAR(48) default NULL,
  logintime INTEGER default NULL,
  address VARCHAR default NULL,
  succeed TEXT default 'S'
);]], table_log)
  status, res = execute(query)
  if status == fcdb.status.TRUE then
    log.normal("Successfully created log table '%s'", table_log)
  else
    log.error("Error creating log table '%s'", table_log)
    return fcdb.status.ERROR
  end

  return fcdb.status.TRUE

end

-- **************************************************************************
-- For MySQL, the following shapes of tables are expected
-- (scripts/setup_auth_server.sh automates this):
--
-- CREATE TABLE fcdb_auth (
--   id int(11) NOT NULL auto_increment,
--   name varchar(48) default NULL,
--   password varchar(32) default NULL,
--   email varchar(128) default NULL,
--   createtime int(11) default NULL,
--   accesstime int(11) default NULL,
--   address varchar(255) default NULL,
--   createaddress varchar(255) default NULL,
--   logincount int(11) default '0',
--   PRIMARY KEY  (id),
--   UNIQUE KEY name (name)
-- ) TYPE=MyISAM;
--
-- CREATE TABLE fcdb_log (
--   id int(11) NOT NULL auto_increment,
--   name varchar(48) default NULL,
--   logintime int(11) default NULL,
--   address varchar(255) default NULL,
--   succeed enum('S','F') default 'S',
--   PRIMARY KEY  (id)
-- ) TYPE=MyISAM;
--
-- N.B. if the tables are not of this format, then the select, insert,
--      and update syntax in the following functions must be changed.
-- **************************************************************************

-- **************************************************************************
-- freeciv user auth functions
-- **************************************************************************

-- load user data
function user_load(conn)
  local status  -- return value (status of the request)
  local res     -- result handle
  local row     -- one row of the sql result
  local query   -- sql query

  local fields = 'password'

  local table_user = fcdb.option(fcdb.param.TABLE_USER)
  local table_log = fcdb.option(fcdb.param.TABLE_LOG)

  if not dbh then
    log.error("Missing database connection...")
    return fcdb.status.ERROR
  end

  local username = dbh:escape(auth.get_username(conn))
  local ipaddr = dbh:escape(auth.get_ipaddr(conn))

  -- get the password for this user
  query = string.format([[SELECT %s FROM %s WHERE name = '%s']],
                        fields, table_user, username)
  status, res = execute(query)
  if status ~= fcdb.status.TRUE then
    return fcdb.status.ERROR
  end

  row = res:fetch({}, 'a')
  if not row then
    -- No match
    res:close()
    return fcdb.status.FALSE
  end

  -- There should be only one result
  if res:fetch() then
    log.error('[user_load]: multiple entries (%d) for user: %s',
              numrows, username)
    res:close()
    return fcdb.status.FALSE
  end

  auth.set_password(conn, row.password)

  res:close()

  return fcdb.status.TRUE
end

-- save a user to the database
function user_save(conn)
  local status  -- return value (status of the request)
  local res     -- result handle
  local query   -- sql query

  local table_user = fcdb.option(fcdb.param.TABLE_USER)

  if not dbh then
    log.error("Missing database connection...")
    return fcdb.status.ERROR
  end

  local username = dbh:escape(auth.get_username(conn))
  local password = dbh:escape(auth.get_password(conn))
  local ipaddr = auth.get_ipaddr(conn)

  -- insert the user
  --local now = os.time()
  query = string.format([[INSERT INTO %s VALUES (NULL, '%s', '%s',
                          NULL, %s, %s, '%s', '%s', 0)]],
                        table_user, username, password,
                        sql_time(), sql_time(),
                        ipaddr, ipaddr)
  status, res = execute(query)
  if status ~= fcdb.status.TRUE then
    return fcdb.status.ERROR
  end

  -- log this session
  return user_log(conn, true)
end

-- log the session
function user_log(conn, success)
  local status  -- return value (status of the request)
  local res     -- result handle
  local query   -- sql query

  if not dbh then
    log.error("Missing database connection...")
    return fcdb.status.ERROR
  end

  local table_user = fcdb.option(fcdb.param.TABLE_USER)
  local table_log = fcdb.option(fcdb.param.TABLE_LOG)

  local username = dbh:escape(auth.get_username(conn))
  local ipaddr = auth.get_ipaddr(conn)
  local success_str = success and 'S' or 'F'

  -- update user data
  --local now = os.time()
  query = string.format([[UPDATE %s SET accesstime = %s, address = '%s',
                          logincount = logincount + 1
                          WHERE name = '%s']], table_user, sql_time(),
                          ipaddr, username)
  status, res = execute(query)
  if status ~= fcdb.status.TRUE then
    return fcdb.status.ERROR
  end

  -- insert the log row for this user
  query = string.format([[INSERT INTO %s (name, logintime, address, succeed)
                          VALUES ('%s', %s, '%s', '%s')]],
                        table_log, username, sql_time(), ipaddr, success_str)
  status, res = execute(query)
  if status ~= fcdb.status.TRUE then
    return fcdb.status.ERROR
  end

  return fcdb.status.TRUE
end

-- **************************************************************************
-- freeciv database entry functions
-- **************************************************************************

-- test and initialise the database connection
function database_init()
  local status -- return value (status of the request)

  local backend = fcdb.option(fcdb.param.BACKEND)
  if backend == 'mysql' then
    log.verbose('Opening MySQL database connection...')
    status = mysql_connect()
  elseif backend == 'sqlite' then
    log.verbose('Opening SQLite database connection...')
    status = sqlite_connect()
  else
    log.error('Database backend \'%s\' not supported by database.lua', backend)
    return fcdb.status.ERROR
  end

  if status == fcdb.status.TRUE then
    log.verbose('Database connection successful.')
  end

  return status
end

-- free the database connection
function database_free()
  log.verbose('Closing database connection...')

  if dbh then
    dbh:close()
  end

  return fcdb.status.TRUE;
end
