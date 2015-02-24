
[spec]

; Format and options of this spec file:
options = "+Freeciv-2.5-spec"

[info]

artists = "
    Tatu Rissanen <tatu.rissanen@hut.fi>
    Jeff Mallatt <jjm@codewell.com> (miscellaneous)
    GriffonSpade
"

[file]
gfx = "amplio2/tiles"

[grid_main]

x_top_left = 1
y_top_left = 1
dx = 96
dy = 48
pixel_border = 1

tiles = { "row", "column", "tag"

; Unit hit-point bars: approx percent of hp remaining

  0,  0, "unit.hp_100"
  0,  1, "unit.hp_90"
  0,  2, "unit.hp_80"
  0,  3, "unit.hp_70"
  0,  4, "unit.hp_60"
  0,  5, "unit.hp_50"
  0,  6, "unit.hp_40"
  0,  7, "unit.hp_30"
  0,  8, "unit.hp_20"
  0,  9, "unit.hp_10"
  0, 10, "unit.hp_0"

; Numbers: city size: (also used for goto)


  1,  0, "city.size_000"
  1,  1, "city.size_100"
  1,  2, "city.size_200"
  1,  3, "city.size_300"
  1,  4, "city.size_400"
  1,  5, "city.size_500"
  1,  6, "city.size_600"
  1,  7, "city.size_700"
  1,  8, "city.size_800"
  1,  9, "city.size_900"

  2, 0, "city.size_00"
  2, 1, "city.size_10"
  2, 2, "city.size_20"
  2, 3, "city.size_30"
  2, 4, "city.size_40"
  2, 5, "city.size_50"
  2, 6, "city.size_60"
  2, 7, "city.size_70"
  2, 8, "city.size_80"
  2, 9, "city.size_90"

  3,  0, "city.size_0"
  3,  1, "city.size_1"
  3,  2, "city.size_2"
  3,  3, "city.size_3"
  3,  4, "city.size_4"
  3,  5, "city.size_5"
  3,  6, "city.size_6"
  3,  7, "city.size_7"
  3,  8, "city.size_8"
  3,  9, "city.size_9"

; Numbers: city tile food/shields/trade y/g/b

  4,  0, "city.t_food_0"
  4,  1, "city.t_food_1"
  4,  2, "city.t_food_2"
  4,  3, "city.t_food_3"
  4,  4, "city.t_food_4"
  4,  5, "city.t_food_5"
  4,  6, "city.t_food_6"
  4,  7, "city.t_food_7"
  4,  8, "city.t_food_8"
  4,  9, "city.t_food_9"

  5,  0, "city.t_shields_0"
  5,  1, "city.t_shields_1"
  5,  2, "city.t_shields_2"
  5,  3, "city.t_shields_3"
  5,  4, "city.t_shields_4"
  5,  5, "city.t_shields_5"
  5,  6, "city.t_shields_6"
  5,  7, "city.t_shields_7"
  5,  8, "city.t_shields_8"
  5,  9, "city.t_shields_9"

  6, 0, "city.t_trade_0"
  6, 1, "city.t_trade_1"
  6, 2, "city.t_trade_2"
  6, 3, "city.t_trade_3"
  6, 4, "city.t_trade_4"
  6, 5, "city.t_trade_5"
  6, 6, "city.t_trade_6"
  6, 7, "city.t_trade_7"
  6, 8, "city.t_trade_8"
  6, 9, "city.t_trade_9"

}

[grid_activities]

x_top_left = 1
y_top_left = 344
dx = 96
dy = 48
pixel_border = 1

tiles = { "row", "column", "tag"
; Unit activity letters:  (note unit icons have just "u.")


  0, 0, "unit.road"
  0, 1, "unit.rail"
  0, 2, "unit.maglev"

  1, 0, "unit.outpost"
  1, 1, "unit.airstrip"
  1, 2, "unit.fortress"
  1, 3, "unit.airbase"
  1, 4, "unit.buoy"

  2, 0, "unit.fortified"
  2, 1, "unit.fortifying"
  2, 2, "unit.sentry"
  2, 3, "unit.patrol"
  2, 4, "unit.pillage"

  3, 0, "unit.connect"
  3, 1, "unit.auto_attack",
         "unit.auto_settler"
  3, 2, "unit.stack"
  3, 3, "unit.loaded"

  4, 0, "unit.irrigate"
  4, 1, "unit.mine"
  4, 2, "unit.transform"
  4, 3, "unit.pollution"
  4, 4, "unit.fallout"

  5, 0, "unit.goto"
  5, 1, "unit.convert"
  5, 2, "unit.auto_explore"

}

[grid_nuclear]

x_top_left = 486
y_top_left = 344
dx = 96
dy = 48
pixel_border = 1

tiles = { "row", "column", "tag"

; Nuclear explosion: this could maybe now be handled as one 
; big graphic (?), but for now is done old way as 3 by 3:

  0,  0, "explode.nuke_00"
  0,  1, "explode.nuke_01"
  0,  2, "explode.nuke_02"
  1,  0, "explode.nuke_10"
  1,  1, "explode.nuke_11"
  1,  2, "explode.nuke_12"
  2,  0, "explode.nuke_20"
  2,  1, "explode.nuke_21"
  2,  2, "explode.nuke_22"

}

[grid_unitcost]

x_top_left = 1
y_top_left = 638
dx = 96
dy = 19
pixel_border = 1

tiles = { "row", "column", "tag"
  0, 0, "upkeep.shield"
  0, 1, "upkeep.shield2"
  0, 2, "upkeep.shield3"
  0, 3, "upkeep.shield4"
  0, 4, "upkeep.shield5"
  0, 5, "upkeep.shield6"
  0, 6, "upkeep.shield7"
  0, 7, "upkeep.shield8"
  0, 8, "upkeep.shield9"
  0, 9, "upkeep.shield10"
  1, 0, "upkeep.gold"
  1, 1, "upkeep.gold2"
  1, 2, "upkeep.gold3"
  1, 3, "upkeep.gold4"
  1, 4, "upkeep.gold5"
  1, 5, "upkeep.gold6"
  1, 6, "upkeep.gold7"
  1, 7, "upkeep.gold8"
  1, 8, "upkeep.gold9"
  1, 9, "upkeep.gold10"
  2, 0, "upkeep.food"
  2, 1, "upkeep.food2"
  2, 2, "upkeep.food3"
  2, 3, "upkeep.food4"
  2, 4, "upkeep.food5"
  2, 5, "upkeep.food6"
  2, 6, "upkeep.food7"
  2, 7, "upkeep.food8"
  2, 8, "upkeep.food9"
  2, 9, "upkeep.food10"
  3, 0, "upkeep.unhappy"
  3, 1, "upkeep.unhappy2"
  3, 2, "upkeep.unhappy3"
  3, 3, "upkeep.unhappy4"
  3, 4, "upkeep.unhappy5"
  3, 5, "upkeep.unhappy6"
  3, 6, "upkeep.unhappy7"
  3, 7, "upkeep.unhappy8"
  3, 8, "upkeep.unhappy9"
  3, 9, "upkeep.unhappy10"
}
