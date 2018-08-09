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


-- Place Ruins at the location of the destroyed city.
function city_destroyed_callback(city, loser, destroyer)
  city.tile:create_extra("Ruins", NIL)
  -- continue processing
  return false
end

signal.connect("city_destroyed", "city_destroyed_callback")

-- Build road on city center river if bridge tech is not known.
function city_built_callback(city)
  city.tile:create_extra("Road", NIL)
end

signal.connect("city_built", "city_built_callback")


-- Grant tech when the wonder Darwin`s Voyage is built.
function building_built_handler(btype, city)
  local darw_btype = find.building_type("Darwin's Voyage")
  local darw_id = darw_btype.id
  local player, id = city.owner, btype.id
  local gained

  if id == darw_id and not darwin_built then
      darwin_built = true
      gained = player:give_tech(nil, 0, false, "researched")

      -- Notify the player. Include the tech name in a way that makes it
      -- look natural no matter if tech is announced or not.
      notify.event(player, NIL, E.TECH_GAIN,
        _("%s boosts research; you gain the immediate advance %s."),
        darw_btype:name_translation(),
        gained:name_translation())

      notify.research(player, false, E.TECH_GAIN,
        _("%s boosts %s research; you gain the immediate advance %s."),
        darw_btype:name_translation(),
        player.nation:name_translation(),
        gained:name_translation())

      -- default.lua informs the embassies when the tech source is a hut.
      -- They should therefore be informed about the source here too.
      notify.research_embassies(player, E.TECH_EMBASSY,
                                -- /* TRANS: 1st %s is leader or team name */
                                _("%s gains %s from %s."),
                                player:research_name_translation(),
                                gained:name_translation(),
                                darw_btype:name_translation())
  end
end

signal.connect("building_built", "building_built_handler")


-- Grant one tech the first time Philosophy is researched.
function tech_researched_handler(tech, player, how)
  local id
  local gained

  if tech == nil then
    -- no tech was researched.
    return
  end

  id = tech.id
  if id == find.tech_type("Philosophy").id and how == "researched" and not philo_researched then

    philo_researched = true

    -- Give the player a free advance.
    -- This will give a free advance for each player that shares research.
    gained = player:give_tech(nil, -1, false, "researched")

    -- Notify all.
    notify.all(_("Great philosophers from all the world join the %s: they get an immediate advance."),
               player.nation:plural_translation())
  end
end 

signal.connect("tech_researched", "tech_researched_handler")
