-- strategic-v1 Freeciv unsafe Lua bridge. Loaded explicitly by agent_eval.
local agent_url = os.getenv("AGENT_EVAL_URL")
local turn_url = os.getenv("AGENT_EVAL_TURN_URL")
local internal_token = os.getenv("AGENT_EVAL_INTERNAL_TOKEN")
local bridge_status_path = os.getenv("AGENT_EVAL_BRIDGE_STATUS_PATH")
local replay_path = os.getenv("AGENT_EVAL_REPLAY_PATH")
local replay_catalog_path = os.getenv("AGENT_EVAL_REPLAY_CATALOG_PATH")
local replay_warnings_path = os.getenv("AGENT_EVAL_REPLAY_WARNINGS_PATH")
local victory_path = os.getenv("AGENT_EVAL_VICTORY_PATH")
local turn_timeout_s = tonumber(os.getenv("AGENT_EVAL_TURN_TIMEOUT_S") or "")
if not turn_timeout_s or turn_timeout_s < 0 then turn_timeout_s = 300 end
-- curl defines --max-time 0 as no timeout; preserve it for infinite games.
turn_timeout_s = math.floor(turn_timeout_s)
local game_id = os.getenv("AGENT_EVAL_GAME_ID") or "unknown"
local roster = os.getenv("AGENT_EVAL_SEATS") or ""
local replay_roster = os.getenv("AGENT_EVAL_REPLAY_SEATS") or ""

if (not turn_url or turn_url == "") and agent_url and agent_url ~= "" then
  turn_url = agent_url .. "/v1/turn"
end

local seats = {}
for seat_id, player_name in string.gmatch(roster, "([^,:]+):([^,]+)") do
  seats[#seats + 1] = {id = seat_id, name = player_name}
end

local replay_seats = {}
local replay_seat_by_name = {}
for seat_id, player_name in string.gmatch(replay_roster, "([^,:]+):([^,]+)") do
  local seat = {id = seat_id, name = player_name}
  replay_seats[#replay_seats + 1] = seat
  replay_seat_by_name[player_name] = seat
end

local function json_escape(value)
  value = string.gsub(value or "", "\\", "\\\\")
  value = string.gsub(value, '"', '\\"')
  value = string.gsub(value, "\n", "\\n")
  value = string.gsub(value, "\r", "\\r")
  value = string.gsub(value, "\t", "\\t")
  return value
end

local JSON_ARRAY = {}
local JSON_OBJECT = {}
local JSON_NULL = {}

local function utf8_character(codepoint)
  if codepoint <= 0x7f then
    return string.char(codepoint)
  elseif codepoint <= 0x7ff then
    return string.char(
      0xc0 + math.floor(codepoint / 0x40),
      0x80 + (codepoint % 0x40))
  elseif codepoint <= 0xffff then
    return string.char(
      0xe0 + math.floor(codepoint / 0x1000),
      0x80 + (math.floor(codepoint / 0x40) % 0x40),
      0x80 + (codepoint % 0x40))
  end
  return string.char(
    0xf0 + math.floor(codepoint / 0x40000),
    0x80 + (math.floor(codepoint / 0x1000) % 0x40),
    0x80 + (math.floor(codepoint / 0x40) % 0x40),
    0x80 + (codepoint % 0x40))
end

local function decode_json(text)
  local position = 1
  local length = #text

  local function json_error(message)
    error("invalid JSON at byte " .. tostring(position) .. ": " .. message, 0)
  end

  local function skip_space()
    while position <= length do
      local byte = string.byte(text, position)
      if byte ~= 0x20 and byte ~= 0x09 and byte ~= 0x0a and byte ~= 0x0d then
        return
      end
      position = position + 1
    end
  end

  local function parse_string()
    if string.sub(text, position, position) ~= '"' then
      json_error("expected string")
    end
    position = position + 1
    local pieces = {}
    while position <= length do
      local byte = string.byte(text, position)
      if byte == 0x22 then
        position = position + 1
        return table.concat(pieces)
      elseif byte < 0x20 then
        json_error("unescaped control character in string")
      elseif byte ~= 0x5c then
        pieces[#pieces + 1] = string.char(byte)
        position = position + 1
      else
        position = position + 1
        local escaped = string.sub(text, position, position)
        local simple = {
          ['"'] = '"', ['\\'] = '\\', ['/'] = '/',
          b = "\b", f = "\f", n = "\n", r = "\r", t = "\t"
        }
        if simple[escaped] then
          pieces[#pieces + 1] = simple[escaped]
          position = position + 1
        elseif escaped == "u" then
          local digits = string.sub(text, position + 1, position + 4)
          if #digits ~= 4 or not string.match(digits, "^%x%x%x%x$") then
            json_error("invalid Unicode escape")
          end
          local codepoint = tonumber(digits, 16)
          position = position + 5
          if codepoint >= 0xd800 and codepoint <= 0xdbff then
            if string.sub(text, position, position + 1) ~= "\\u" then
              json_error("high surrogate without low surrogate")
            end
            local low_digits = string.sub(text, position + 2, position + 5)
            if #low_digits ~= 4 or
               not string.match(low_digits, "^%x%x%x%x$") then
              json_error("invalid low surrogate")
            end
            local low = tonumber(low_digits, 16)
            if low < 0xdc00 or low > 0xdfff then
              json_error("invalid low surrogate")
            end
            codepoint = 0x10000 + (codepoint - 0xd800) * 0x400 +
                        (low - 0xdc00)
            position = position + 6
          elseif codepoint >= 0xdc00 and codepoint <= 0xdfff then
            json_error("low surrogate without high surrogate")
          end
          pieces[#pieces + 1] = utf8_character(codepoint)
        else
          json_error("invalid string escape")
        end
      end
    end
    json_error("unterminated string")
  end

  local parse_value

  local function parse_number()
    local start = position
    if string.sub(text, position, position) == "-" then
      position = position + 1
    end
    local first = string.sub(text, position, position)
    if first == "0" then
      position = position + 1
      if string.match(string.sub(text, position, position), "%d") then
        json_error("leading zero in number")
      end
    elseif string.match(first, "[1-9]") then
      repeat
        position = position + 1
      until not string.match(string.sub(text, position, position), "%d")
    else
      json_error("invalid number")
    end
    if string.sub(text, position, position) == "." then
      position = position + 1
      if not string.match(string.sub(text, position, position), "%d") then
        json_error("fraction requires a digit")
      end
      repeat
        position = position + 1
      until not string.match(string.sub(text, position, position), "%d")
    end
    local exponent = string.sub(text, position, position)
    if exponent == "e" or exponent == "E" then
      position = position + 1
      local sign = string.sub(text, position, position)
      if sign == "+" or sign == "-" then position = position + 1 end
      if not string.match(string.sub(text, position, position), "%d") then
        json_error("exponent requires a digit")
      end
      repeat
        position = position + 1
      until not string.match(string.sub(text, position, position), "%d")
    end
    local value = tonumber(string.sub(text, start, position - 1))
    if not value or value ~= value or value == math.huge or value == -math.huge then
      json_error("number is not finite")
    end
    return value
  end

  local function parse_array()
    position = position + 1
    local value = setmetatable({}, JSON_ARRAY)
    skip_space()
    if string.sub(text, position, position) == "]" then
      position = position + 1
      return value
    end
    while true do
      value[#value + 1] = parse_value()
      skip_space()
      local separator = string.sub(text, position, position)
      if separator == "]" then
        position = position + 1
        return value
      elseif separator ~= "," then
        json_error("expected comma or closing bracket")
      end
      position = position + 1
      skip_space()
    end
  end

  local function parse_object()
    position = position + 1
    local value = setmetatable({}, JSON_OBJECT)
    local seen = {}
    skip_space()
    if string.sub(text, position, position) == "}" then
      position = position + 1
      return value
    end
    while true do
      local key = parse_string()
      if seen[key] then json_error("duplicate object key") end
      seen[key] = true
      skip_space()
      if string.sub(text, position, position) ~= ":" then
        json_error("expected colon")
      end
      position = position + 1
      skip_space()
      value[key] = parse_value()
      skip_space()
      local separator = string.sub(text, position, position)
      if separator == "}" then
        position = position + 1
        return value
      elseif separator ~= "," then
        json_error("expected comma or closing brace")
      end
      position = position + 1
      skip_space()
    end
  end

  parse_value = function()
    skip_space()
    local first = string.sub(text, position, position)
    if first == '"' then return parse_string() end
    if first == "{" then return parse_object() end
    if first == "[" then return parse_array() end
    if first == "-" or string.match(first, "%d") then return parse_number() end
    if string.sub(text, position, position + 3) == "true" then
      position = position + 4
      return true
    end
    if string.sub(text, position, position + 4) == "false" then
      position = position + 5
      return false
    end
    if string.sub(text, position, position + 3) == "null" then
      position = position + 4
      return JSON_NULL
    end
    json_error("unexpected token")
  end

  local value = parse_value()
  skip_space()
  if position <= length then json_error("trailing content") end
  return value
end

local function bool_json(value)
  if value then return "true" end
  return "false"
end

local function public_ruleset_name(value)
  return string.gsub(value or "", "^%?[^:]+:", "")
end

local function append_bridge_status(event, turn, message)
  if not bridge_status_path or bridge_status_path == "" then
    return false, "bridge status path is missing"
  end
  local stream = io.open(bridge_status_path, "a")
  if not stream then return false, "bridge status journal is unavailable" end
  local line = '{"event":"' .. event .. '","turn":' .. tostring(turn)
  if message then
    line = line .. ',"message":"' .. json_escape(message) .. '"'
  end
  stream:write(line .. "}\n")
  stream:flush()
  stream:close()
  return true, nil
end

-- The victory record is written to its own file rather than the bridge
-- status journal: that journal is a strictly validated begin/ok/error
-- lifecycle and any extra event kind there would invalidate the benchmark.
local function write_victory(reason, winners, turn, year)
  if not victory_path or victory_path == "" then return end
  local names = {}
  for name in string.gmatch(winners or "", "[^,]+") do
    names[#names + 1] = '"' .. json_escape(name) .. '"'
  end
  local stream = io.open(victory_path, "w")
  if not stream then return end
  stream:write('{"schema_version":1,"victory":"' .. json_escape(reason) ..
               '","winners":[' .. table.concat(names, ",") ..
               '],"turn":' .. tostring(turn) ..
               ',"year":' .. tostring(year) .. '}\n')
  stream:flush()
  stream:close()
end

local function append_replay_warning(turn)
  if not replay_warnings_path or replay_warnings_path == "" then return end
  local stream = io.open(replay_warnings_path, "a")
  if not stream then return end
  stream:write('{"turn":' .. tostring(turn) ..
               ',"message":"replay capture unavailable"}\n')
  stream:flush()
  stream:close()
end

local replay_catalog_json = nil
local replay_techs = nil

local function technology_catalog()
  if replay_catalog_json and replay_techs then
    return replay_catalog_json, replay_techs
  end
  local encoded = {}
  local techs = {}
  local found_tech = false
  for tech_id = 0, 511 do
    local tech = find.tech_type(tech_id)
    if not tech then
      if found_tech then break end
    else
      found_tech = true
      local rule_name = public_ruleset_name(tech:rule_name())
      if rule_name ~= "None" and rule_name ~= "Never" then
        techs[#techs + 1] = tech
        encoded[#encoded + 1] =
          '{"id":' .. tostring(tech.id) ..
          ',"rule_name":"' .. json_escape(rule_name) .. '"' ..
          ',"name":"' ..
            json_escape(public_ruleset_name(tech:name_translation())) .. '"' ..
          ',"cost_base":' .. tostring(tech.cost_base) .. '}'
      end
    end
  end
  if #techs == 0 then error("technology catalog is unavailable", 0) end
  replay_catalog_json = '{"schema_version":1,"technologies":[' ..
                        table.concat(encoded, ",") .. ']}'
  replay_techs = techs
  return replay_catalog_json, replay_techs
end

local function ensure_replay_catalog()
  local catalog_json = technology_catalog()
  if not replay_catalog_path or replay_catalog_path == "" then
    error("replay catalog path is missing", 0)
  end
  local existing = io.open(replay_catalog_path, "r")
  if existing then
    local content = existing:read("*a")
    existing:close()
    content = string.gsub(content or "", "%s+$", "")
    if content == catalog_json then return end
  end
  local temporary_path = replay_catalog_path .. ".tmp"
  local stream = io.open(temporary_path, "w")
  if not stream then error("replay catalog is unavailable", 0) end
  stream:write(catalog_json .. "\n")
  stream:flush()
  stream:close()
  local renamed = os.rename(temporary_path, replay_catalog_path)
  if not renamed then
    os.remove(temporary_path)
    error("replay catalog is unavailable", 0)
  end
end

local function replay_player(seat, turn, year, techs)
  local player = find.player(seat.name)
  if not player then error("replay player " .. seat.name .. " is unavailable", 0) end
  local government = ""
  if player.government then
    government = public_ruleset_name(player.government:rule_name())
  end
  local nation = ""
  if player.nation then nation = public_ruleset_name(player.nation:rule_name()) end
  local population = 0
  for city in player:cities_iterate() do
    population = population + city.size
  end
  local known = {}
  for _, tech in ipairs(techs) do
    if player:knows_tech(tech) then
      known[#known + 1] = tostring(tech.id)
    end
  end
  local research_id = "null"
  local research_name = ""
  local researching = player:researching()
  if type(researching) == "string" then
    research_name = public_ruleset_name(researching)
  elseif researching ~= nil and tolua.type(researching) == "Tech_Type" then
    research_id = tostring(researching.id)
    research_name = public_ruleset_name(researching:rule_name())
  end
  return '{"seat_id":"' .. json_escape(seat.id) .. '"' ..
    ',"player_id":' .. tostring(player.id) ..
    ',"player_name":"' .. json_escape(player.name) .. '"' ..
    ',"turn":' .. tostring(turn) ..
    ',"year":' .. tostring(year) ..
    ',"nation":"' .. json_escape(nation) .. '"' ..
    ',"government":"' .. json_escape(government) .. '"' ..
    ',"alive":' .. bool_json(player.is_alive) ..
    ',"score":' .. tostring(player:civilization_score()) ..
    ',"cities":' .. tostring(player:num_cities()) ..
    ',"citizens":' .. tostring(population) ..
    ',"population":' .. tostring(population) ..
    ',"units":' .. tostring(player:num_units()) ..
    ',"gold":' .. tostring(player:gold()) ..
    ',"culture":' .. tostring(player:culture()) ..
    ',"known_tech_ids":[' .. table.concat(known, ",") .. ']' ..
    ',"research":{"tech_id":' .. research_id ..
      ',"name":"' .. json_escape(research_name) .. '"' ..
      ',"bulbs":' .. tostring(player.bulbs) ..
      ',"cost":' .. tostring(player:researching_cost()) .. '}' ..
    ',"future_techs":' .. tostring(player:num_future_techs()) .. '}'
end

local function capture_replay(turn, year)
  if not replay_path or replay_path == "" or #replay_seats == 0 then return end
  ensure_replay_catalog()
  local _, techs = technology_catalog()
  local players = {}
  for player in players_iterate() do
    local seat = replay_seat_by_name[player.name]
    if not seat then
      seat = {
        id = "dynamic-player-" .. tostring(player.id),
        name = player.name
      }
    end
    players[#players + 1] = {
      id = player.id,
      encoded = replay_player(seat, turn, year, techs)
    }
  end
  table.sort(players, function(left, right) return left.id < right.id end)
  local encoded_players = {}
  for _, player in ipairs(players) do
    encoded_players[#encoded_players + 1] = player.encoded
  end
  local stream = io.open(replay_path, "a")
  if not stream then error("replay journal is unavailable", 0) end
  stream:write('{"schema_version":1,"game_id":"' .. json_escape(game_id) ..
               '","turn":' .. tostring(turn) ..
               ',"year":' .. tostring(year) ..
               ',"players":[' .. table.concat(encoded_players, ",") .. ']}\n')
  stream:flush()
  stream:close()
end

local function bridge_failure(message)
  error(message, 0)
end

local function command_succeeded(ok, kind, code)
  if type(ok) == "number" then return ok == 0 end
  if ok ~= true then return false end
  if kind == nil and code == nil then return true end
  return kind == "exit" and code == 0
end

local function shell_quote(value)
  return "'" .. string.gsub(value, "'", "'\\''") .. "'"
end

local function own_observation(seat, turn, year)
  local player = find.player(seat.name)
  if not player then return nil end
  local government = ""
  if player.government then government = player.government:rule_name() end
  local research = ""
  local researching = player:researching()
  if type(researching) == "string" then
    research = researching
  elseif researching ~= nil and tolua.type(researching) == "Tech_Type" then
    research = researching:rule_name()
  end
  local fields = {
    '"seat_id":"' .. json_escape(seat.id) .. '"',
    '"player_id":' .. tostring(player.id),
    '"player_name":"' .. json_escape(player.name) .. '"',
    '"turn":' .. tostring(turn),
    '"year":' .. tostring(year),
    '"alive":' .. bool_json(player.is_alive),
    '"civilization_score":' .. tostring(player:civilization_score()),
    '"gold":' .. tostring(player:gold()),
    '"num_cities":' .. tostring(player:num_cities()),
    '"num_units":' .. tostring(player:num_units()),
    '"bulbs":' .. tostring(player.bulbs),
    '"culture":' .. tostring(player:culture()),
    '"government":"' .. json_escape(government) .. '"',
    '"research":"' .. json_escape(research) .. '"',
    '"traits":{' ..
      '"aggressive":' .. tostring(player:trait("Aggressive")) .. ',' ..
      '"builder":' .. tostring(player:trait("Builder")) .. ',' ..
      '"expansionist":' .. tostring(player:trait("Expansionist")) .. ',' ..
      '"trader":' .. tostring(player:trait("Trader")) .. '}',
    '"trait_bases":{' ..
      '"aggressive":' .. tostring(player:trait_base("Aggressive")) .. ',' ..
      '"builder":' .. tostring(player:trait_base("Builder")) .. ',' ..
      '"expansionist":' .. tostring(player:trait_base("Expansionist")) .. ',' ..
      '"trader":' .. tostring(player:trait_base("Trader")) .. '}',
    '"trait_modifiers":{' ..
      '"aggressive":' .. tostring(player:trait_current_mod("Aggressive")) .. ',' ..
      '"builder":' .. tostring(player:trait_current_mod("Builder")) .. ',' ..
      '"expansionist":' .. tostring(player:trait_current_mod("Expansionist")) .. ',' ..
      '"trader":' .. tostring(player:trait_current_mod("Trader")) .. '}'
  }
  return "{" .. table.concat(fields, ",") .. "}"
end

local function apply_trait(player, name, target)
  if target < -49 or target > 50 or target ~= math.floor(target) then return end
  local current = player:trait_current_mod(name)
  player:trait_mod(name, target - current)
end

local function require_object_keys(value, allowed, label)
  if type(value) ~= "table" or getmetatable(value) ~= JSON_OBJECT then
    bridge_failure(label .. " must be an object")
  end
  for key, _ in pairs(value) do
    if not allowed[key] then
      bridge_failure(label .. " has unknown key " .. tostring(key))
    end
  end
  for key, _ in pairs(allowed) do
    if value[key] == nil then
      bridge_failure(label .. " is missing key " .. tostring(key))
    end
  end
end

local function is_integer(value)
  return type(value) == "number" and value == math.floor(value)
end

local function strategic_turn_impl(turn, year)
  if not turn_url or turn_url == "" then
    bridge_failure("turn URL is missing")
  end
  if not internal_token or internal_token == "" then
    bridge_failure("internal token is missing")
  end
  if #seats == 0 then bridge_failure("agent seat roster is empty") end
  local observations = {}
  for _, seat in ipairs(seats) do
    local observation = own_observation(seat, turn, year)
    if observation then observations[#observations + 1] = observation end
  end
  local request_path = os.tmpname()
  local response_path = os.tmpname()
  local auth_path = os.tmpname()
  local stream = io.open(request_path, "w")
  if not stream then bridge_failure("could not create turn request") end
  stream:write('{"schema_version":1,"game_id":"' .. json_escape(game_id) ..
               '","turn":' .. tostring(turn) .. ',"year":' .. tostring(year) ..
               ',"observations":[' .. table.concat(observations, ",") .. ']}')
  stream:close()

  -- Never put the internal bearer in curl's argv. Create the config file at
  -- mode 0600 before opening it from Lua, then remove it immediately after
  -- curl returns. This protects process listings; same-user hostile process
  -- isolation remains a deployment boundary rather than a Lua guarantee.
  os.remove(auth_path)
  local create_ok, create_kind, create_code = os.execute(
    "umask 077; : > " .. shell_quote(auth_path))
  if not command_succeeded(create_ok, create_kind, create_code) then
    os.remove(request_path)
    os.remove(response_path)
    os.remove(auth_path)
    bridge_failure("could not create private turn authorization config")
  end
  local auth_stream = io.open(auth_path, "w")
  if not auth_stream then
    os.remove(request_path)
    os.remove(response_path)
    os.remove(auth_path)
    bridge_failure("could not open private turn authorization config")
  end
  local auth_ok = pcall(function()
    auth_stream:write(
      'header = "Authorization: Bearer ' .. internal_token .. '"\n')
    auth_stream:flush()
  end)
  pcall(function() auth_stream:close() end)
  if not auth_ok then
    os.remove(request_path)
    os.remove(response_path)
    os.remove(auth_path)
    bridge_failure("could not write private turn authorization config")
  end
  local command = "curl --silent --show-error --fail --max-time " ..
                  tostring(turn_timeout_s) .. " " ..
                  "--config " .. shell_quote(auth_path) .. " " ..
                  "--header 'Content-Type: application/json' " ..
                  "--data-binary " .. shell_quote("@" .. request_path) .. " " ..
                  shell_quote(turn_url) .. " >" .. shell_quote(response_path)
  local execute_ok, execute_kind, execute_code = os.execute(command)
  local response_stream = io.open(response_path, "r")
  local response = response_stream and response_stream:read("*a") or ""
  if response_stream then response_stream:close() end
  os.remove(auth_path)
  os.remove(request_path)
  os.remove(response_path)
  if not command_succeeded(execute_ok, execute_kind, execute_code) then
    bridge_failure("turn request transport failed")
  end
  local response_value = decode_json(response)
  require_object_keys(
    response_value,
    {
      schema_version = true, turn = true, actions = true,
      timed_out_seats = true, benchmark_valid = true
    },
    "turn response")
  if response_value.schema_version ~= 1 then
    bridge_failure("turn response has an invalid schema_version")
  end
  if not is_integer(response_value.turn) or response_value.turn ~= turn then
    bridge_failure("turn response does not match the requested turn")
  end
  if type(response_value.benchmark_valid) ~= "boolean" then
    bridge_failure("turn response benchmark_valid must be a boolean")
  end
  if type(response_value.actions) ~= "table" or
     getmetatable(response_value.actions) ~= JSON_ARRAY then
    bridge_failure("turn response actions must be an array")
  end

  local seat_names = {}
  for _, seat in ipairs(seats) do seat_names[seat.id] = seat.name end
  local parsed_actions = {}
  local parsed_seats = {}
  for _, action in ipairs(response_value.actions) do
    require_object_keys(
      action, {seat_id = true, traits = true}, "turn response action")
    local seat_id = action.seat_id
    if type(seat_id) ~= "string" or not seat_names[seat_id] or
       parsed_seats[seat_id] then
      bridge_failure("turn response has an unknown or duplicate action seat")
    end
    require_object_keys(
      action.traits,
      {
        aggressive = true, builder = true,
        expansionist = true, trader = true
      },
      "turn response action traits")
    local action = {
      player_name = seat_names[seat_id],
      aggressive = action.traits.aggressive,
      builder = action.traits.builder,
      expansionist = action.traits.expansionist,
      trader = action.traits.trader
    }
    local function valid_trait(value)
      return is_integer(value) and value >= -49 and value <= 50
    end
    if not valid_trait(action.aggressive) or
       not valid_trait(action.builder) or
       not valid_trait(action.expansionist) or
       not valid_trait(action.trader) then
      bridge_failure("turn response has an invalid traits action")
    end
    parsed_seats[seat_id] = true
    parsed_actions[#parsed_actions + 1] = action
  end

  local timed_out = response_value.timed_out_seats
  if type(timed_out) ~= "table" or getmetatable(timed_out) ~= JSON_ARRAY then
    bridge_failure("turn response timed_out_seats must be an array")
  end
  local seen_timeouts = {}
  for _, seat_id in ipairs(timed_out) do
    if type(seat_id) ~= "string" or not seat_names[seat_id] or
       seen_timeouts[seat_id] or parsed_seats[seat_id] then
      bridge_failure("turn response has an invalid timed_out_seats entry")
    end
    seen_timeouts[seat_id] = true
  end
  if #timed_out > 0 and response_value.benchmark_valid then
    bridge_failure("timed out response cannot be benchmark-valid")
  end
  for seat_id, _ in pairs(seat_names) do
    if not parsed_seats[seat_id] and not seen_timeouts[seat_id] then
      bridge_failure("turn response does not cover every controlled seat")
    end
  end

  for _, action in ipairs(parsed_actions) do
    local player = find.player(action.player_name)
    if not player then
      bridge_failure("turn response action player is unavailable")
    end
    apply_trait(player, "Aggressive", action.aggressive)
    apply_trait(player, "Builder", action.builder)
    apply_trait(player, "Expansionist", action.expansionist)
    apply_trait(player, "Trader", action.trader)
  end
end

function strategic_turn(turn, year)
  local replay_succeeded = pcall(capture_replay, turn, year)
  if not replay_succeeded then
    pcall(append_replay_warning, turn)
  end
  local recorded, record_error = append_bridge_status("begin", turn, nil)
  if not recorded then
    error("agent_eval bridge: " .. record_error, 0)
  end
  local succeeded, failure = pcall(strategic_turn_impl, turn, year)
  if not succeeded then
    local message = tostring(failure)
    append_bridge_status("error", turn, message)
    log.normal("AGENT_EVAL_NATIVE_TURN_RESPONSE_DONE turn=%d", turn)
    error("agent_eval bridge: " .. message, 0)
  end
  recorded, record_error = append_bridge_status("ok", turn, nil)
  if not recorded then
    log.normal("AGENT_EVAL_NATIVE_TURN_RESPONSE_DONE turn=%d", turn)
    error("agent_eval bridge: " .. record_error, 0)
  end
  log.normal("AGENT_EVAL_NATIVE_TURN_RESPONSE_DONE turn=%d", turn)
end

function strategic_game_over(reason, winners, turn, year)
  -- Never fail the game on a bookkeeping write; the match is already over.
  pcall(write_victory, reason, winners, turn, year)
  log.normal("AGENT_EVAL_NATIVE_GAME_OVER victory=%s turn=%d", reason, turn)
end

signal.connect("agent_turn_begin", "strategic_turn")
signal.connect("agent_game_over", "strategic_game_over")
