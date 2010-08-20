function city_destroyed_callback(city, loser, destroyer)
  create_base(city.tile, "Ruins", NIL)
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
    if give_technology(player, tevo_tech) then
      give_technology(player, nil)
      give_technology(player, nil)
    end
  end
end

signal.connect("building_built", "building_built_handler")


phil_tech = find.tech_type("Philosophy")
phil_id = phil_tech.id

function tech_researched_handler(tech, player, how)
  local id = tech.id
  if id == phil_id and how == "researched" then
    give_technology(player, nil)
  end
end 

signal.connect("tech_researched", "tech_researched_handler")
