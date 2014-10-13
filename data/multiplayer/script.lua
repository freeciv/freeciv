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
  local gained = {}

  if id == darw_id then
    -- Block the player from destroying the wonder, rebuilding it and
    -- getting two free advances again.
    -- This also prevents those he share research with from getting two
    -- free advances from building Darwin's Voyage them self.
    if player:give_technology(tevo_tech, "researched") then
      -- Give the player two free advances.
      gained[0] = player:give_technology(nil, "researched")
      gained[1] = player:give_technology(nil, "researched")

      -- default.lua informs the embassies when the tech source is a hut.
      -- They should therefore be informed about the source here too.
      notify.embassies(player, NIL, E.TECH_GAIN,
                       _("The %s gain %s and %s from %s."),
                       player.nation:plural_translation(),
                       gained[0]:name_translation(),
                       gained[1]:name_translation(),
                       darw_btype:name_translation())
    end
  end
end

signal.connect("building_built", "building_built_handler")


phil_tech = find.tech_type("Philosophy")
phil_id = phil_tech.id

function tech_researched_handler(tech, player, how)
  local id
  local gained

  if tech == nil then
    return
  end

  id = tech.id
  if id == phil_id and how == "researched" then
    -- Give the player a free advance.
    -- This will give a free advance for each player that shares research.
    gained = player:give_technology(nil, "researched")

    -- default.lua informs the embassies when the tech source is a hut.
    -- They should therefore be informed about the source here too.
    notify.embassies(player, NIL, E.TECH_GAIN,
                     _("Great philosophers from all the world join the "
                       .. "%s: they get %s as an immediate advance."),
                       player.nation:plural_translation(),
                       gained:name_translation())
  end
end 

signal.connect("tech_researched", "tech_researched_handler")
