-- Freeciv - Copyright (C) 2007 - The Freeciv Project
--   This program is free software; you can redistribute it and/or modify
--   it under the terms of the GNU General Public License as published by
--   the Free Software Foundation; either version 2, or (at your option)
--   any later version.
--
--   This program is distributed in the hope that it will be useful,
--   but WITHOUT ANY WARRANTY; without even the implied warranty of
--   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
--   GNU General Public License for more details.

-- This file is for lua-functionality that is specific to a given
-- ruleset. When freeciv loads a ruleset, it also loads script
-- file called 'default.lua'. The one loaded if your ruleset
-- does not provide an override is default/default.lua.

function clear_city_spot(city, loser, destroyer)
  city.tile:remove_extra("Railroad")
  city.tile:remove_extra("Road")
  city.tile:remove_extra("Irrigation")
  city.tile:remove_extra("Mine")
  -- continue processing
  return false
end

signal.connect("city_destroyed", "clear_city_spot")
