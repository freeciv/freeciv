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

function city_destroyed_callback(city, loser, destroyer)
  city.tile:create_base("Ruins", NIL)
  -- continue processing
  return false
end

signal.connect("city_destroyed", "city_destroyed_callback")


tevo_tech = find.tech_type("Theory of Evolution")
darw_btype = find.building_type("Darwin's Voyage")
darw_id = darw_btype.id

function building_built_handler(btype, city)
  local player, id = city.owner, btype.id
  if id == darw_id then
    if player:give_technology(tevo_tech, "researched") then
      player:give_technology(nil, "researched")
      player:give_technology(nil, "researched")
    end
  end
end

signal.connect("building_built", "building_built_handler")


phil_tech = find.tech_type("Philosophy")
phil_id = phil_tech.id

function tech_researched_handler(tech, player, how)
  local id

  if tech == nil then
    return
  end

  id = tech.id
  if id == phil_id and how == "researched" then
    player:give_technology(nil, "researched")
  end
end 

signal.connect("tech_researched", "tech_researched_handler")
