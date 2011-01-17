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
