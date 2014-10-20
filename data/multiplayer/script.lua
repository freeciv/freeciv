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

-- Place Ruins at the location of the destroyed city.
function city_destroyed_callback(city, loser, destroyer)
  city.tile:create_base("Ruins", NIL)
  -- continue processing
  return false
end

signal.connect("city_destroyed", "city_destroyed_callback")


-- Look this up once, not each time building_built_handler() is called.
tevo_tech = find.tech_type("Theory of Evolution")
darw_btype = find.building_type("Darwin's Voyage")
darw_id = darw_btype.id

-- Grant two techs when the wonder Darwin`s Voyage is built.
function building_built_handler(btype, city)
  local player, id = city.owner, btype.id
  local gained = {}

  if id == darw_id then
    -- Block the player from destroying the wonder, rebuilding it and
    -- getting two free advances again.
    -- This also prevents those they share research with from getting two
    -- free advances from building Darwin`s Voyage themselves.
    if player:give_technology(tevo_tech, "researched") then
      -- Give the player two free advances.
      gained[0] = player:give_technology(nil, "researched")
      gained[1] = player:give_technology(nil, "researched")

      -- Notify the player. Include the tech names in a way that makes it
      -- look natural no matter if each tech is announced or not.
      notify.event(player, NIL, E.TECH_GAIN,
        _("%s boosts research; you gain the immediate advances %s and %s."),
        darw_btype:name_translation(),
        gained[0]:name_translation(),
        gained[1]:name_translation())

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


-- Look this up once, not each time tech_researched_handler() is called.
phil_tech = find.tech_type("Philosophy")
phil_id = phil_tech.id

-- Grant one tech when the tech Philosophy is researched.
function tech_researched_handler(tech, player, how)
  local id
  local gained

  if tech == nil then
    -- no tech was researched.
    return
  end

  id = tech.id
  if id == phil_id and how == "researched" then
    -- Give the player a free advance.
    -- This will give a free advance for each player that shares research.
    gained = player:give_technology(nil, "researched")

      -- Notify the player. Include the tech names in a way that makes it
      -- look natural no matter if each tech is announced or not.
    notify.event(player, NIL, E.TECH_GAIN,
                 _("Great philosophers from all the world join your civilization: you get the immediate advance %s."),
                 gained:name_translation())

    -- default.lua informs the embassies when the tech source is a hut.
    -- They should therefore be informed about the source here too.
    notify.embassies(player, NIL, E.TECH_GAIN,
                     _("Great philosophers from all the world join the %s: they get %s as an immediate advance."),
                       player.nation:plural_translation(),
                       gained:name_translation())
  end
end 

signal.connect("tech_researched", "tech_researched_handler")
