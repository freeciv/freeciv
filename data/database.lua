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

-- This file is the Freeciv server's interface to the database backend
-- when authentication is enabled. See doc/README.fcdb.

-- **************************************************************************
-- basic mysql functions
-- **************************************************************************

-- luasql mysql
local sql = luasql.mysql()
local dbh = nil

-- connect to the database (or stop with an error)
local function connect()
  local err -- error message

  if dbh then
    dbh:close()
  end

  -- Load the database parameters.
  local database = fcdb.option(fcdb.param.DATABASE)
  local user     = fcdb.option(fcdb.param.USER)
  local password = fcdb.option(fcdb.param.PASSWORD)
  local host     = fcdb.option(fcdb.param.HOST)

  dbh, err = sql:connect(database, user, password, host)
  if not dbh then
    log.error('[mysql:connect]: %s', err)
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

  log.verbose("MySql query: %s", query)

  res, err = dbh:execute(query)
  if not res then
    log.error("[mysql:execute]: %s\nquery: %s", err, query)
    return fcdb.status.ERROR, err
  else
    return fcdb.status.TRUE, res
  end
end


-- **************************************************************************
-- The freeciv database tables can be created with the following
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
--   name varchar(32) default NULL,
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
  local numrows -- number of rows in the sql result
  local row     -- one row of the sql result
  local query   -- sql query

  local fields = 'password'

  local table_user = fcdb.option(fcdb.param.TABLE_USER)
  local table_log = fcdb.option(fcdb.param.TABLE_LOG)

  if not dbh then
    log.error("Missing database connection ...")
    return fcdb.status.ERROR
  end

  local username = dbh:escape(auth.get_username(conn))
  local ipaddr = dbh:escape(auth.get_ipaddr(conn))

  -- get the password for this user
  query = string.format([[SELECT %s FROM %s WHERE `name` = '%s']],
                        fields, table_user, username)
  status, res = execute(query);
  if status ~= fcdb.status.TRUE then
    return fcdb.status.ERROR
  end

  numrows = res:numrows()
  if numrows == 0 then
    res:close()
    return fcdb.status.FALSE
  end

  if numrows > 1 then
    log.error('[user_load]: multiple entries (%d) for user: %s',
              numrows, username)
    res:close()
    return fcdb.status.FALSE
  end

  row = res:fetch({}, 'a')
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
    log.error("Missing database connection ...")
    return fcdb.status.ERROR
  end

  local username = dbh:escape(auth.get_username(conn))
  local password = dbh:escape(auth.get_password(conn))
  local ipaddr = auth.get_ipaddr(conn)

  -- insert the user
  query = string.format([[INSERT INTO %s VALUES (NULL, '%s', '%s',
                          NULL, UNIX_TIMESTAMP(), UNIX_TIMESTAMP(),
                          '%s', '%s', 0)]], table_user, username,
                        password, ipaddr, ipaddr)
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
    log.error("Missing database connection ...")
    return fcdb.status.ERROR
  end

  local table_user = fcdb.option(fcdb.param.TABLE_USER)
  local table_log = fcdb.option(fcdb.param.TABLE_LOG)

  local username = dbh:escape(auth.get_username(conn))
  local ipaddr = auth.get_ipaddr(conn)
  local success_str = success and 'S' or 'F'

  -- update user data
  query = string.format([[UPDATE %s SET `accesstime` = UNIX_TIMESTAMP(),
                          `address` = '%s', `logincount` = `logincount` + 1
                          WHERE `name` = '%s']], table_user, ipaddr, username)
  status, res = execute(query)
  if status ~= fcdb.status.TRUE then
    return fcdb.status.ERROR
  end

  -- insert the log row for this user
  query = string.format([[INSERT INTO %s (name, logintime, address, succeed)
                          VALUES ('%s', UNIX_TIMESTAMP(), '%s', '%s')]],
                        table_log, username, ipaddr, success_str)
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

  log.normal('MySQL database version is %s.', luasql._MYSQLVERSION)
  log.verbose('Testing database connection...')

  status = connect()
  if status == fcdb.status.TRUE then
    log.verbose('Database connection successful.')
  end

  return status
end

-- free the database connection
function database_free()
  log.verbose('Closing database connection ...')

  if dbh then
    dbh:close()
  end

  return fcdb.status.TRUE;
end
