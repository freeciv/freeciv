
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

x_top_left = 0
y_top_left = 0
dx = 96
dy = 48

tiles = { "row", "column", "tag"
; Unit activity letters:  (note unit icons have just "u.")

  0, 18, "unit.auto_attack",
         "unit.auto_settler"
  0, 19, "unit.stack"
  1, 17, "unit.loaded"
  1, 18, "unit.connect"
  1, 19, "unit.auto_explore"
  0, 16, "unit.transform"
  0, 17, "unit.sentry"
  4, 16, "unit.goto"
  4, 17, "unit.mine"
  2, 16, "unit.pollution"
  2, 17, "unit.road"
  2, 18, "unit.irrigate"
  2, 19, "unit.fortifying"
;  1, 16, "unit.fortifying"
;  2, 19, "unit.fortress"
;  3, 16, "unit.airbase"
  3, 17, "unit.pillage"
  3, 18, "unit.fortified"
  3, 19, "unit.fallout"
  4, 19, "unit.patrol"
  1, 18, "unit.convert"

; Unit hit-point bars: approx percent of hp remaining

  7,  0, "unit.hp_100"
  7,  1, "unit.hp_90"
  7,  2, "unit.hp_80"
  7,  3, "unit.hp_70"
  7,  4, "unit.hp_60"
  7,  5, "unit.hp_50"
  7,  6, "unit.hp_40"
  7,  7, "unit.hp_30"
  7,  8, "unit.hp_20"
  7,  9, "unit.hp_10"
  7, 10, "unit.hp_0"

; Numbers: city size: (also used for goto)

  5,  0, "city.size_0"
  5,  1, "city.size_1"
  5,  2, "city.size_2"
  5,  3, "city.size_3"
  5,  4, "city.size_4"
  5,  5, "city.size_5"
  5,  6, "city.size_6"
  5,  7, "city.size_7"
  5,  8, "city.size_8"
  5,  9, "city.size_9"
  5, 10, "city.size_00"
  5, 11, "city.size_10"
  5, 12, "city.size_20"
  5, 13, "city.size_30"
  5, 14, "city.size_40"
  5, 15, "city.size_50"
  5, 16, "city.size_60"
  5, 17, "city.size_70"
  5, 18, "city.size_80"
  5, 19, "city.size_90"
  4,  1, "city.size_100"
  4,  2, "city.size_200"
  4,  3, "city.size_300"
  4,  4, "city.size_400"
  4,  5, "city.size_500"
  4,  6, "city.size_600"
  4,  7, "city.size_700"
  4,  8, "city.size_800"
  4,  9, "city.size_900"

; Numbers: city tile food/shields/trade y/g/b

  8,  0, "city.t_food_0"
  8,  1, "city.t_food_1"
  8,  2, "city.t_food_2"
  8,  3, "city.t_food_3"
  8,  4, "city.t_food_4"
  8,  5, "city.t_food_5"
  8,  6, "city.t_food_6"
  8,  7, "city.t_food_7"
  8,  8, "city.t_food_8"
  8,  9, "city.t_food_9"

  6,  0, "city.t_shields_0"
  6,  1, "city.t_shields_1"
  6,  2, "city.t_shields_2"
  6,  3, "city.t_shields_3"
  6,  4, "city.t_shields_4"
  6,  5, "city.t_shields_5"
  6,  6, "city.t_shields_6"
  6,  7, "city.t_shields_7"
  6,  8, "city.t_shields_8"
  6,  9, "city.t_shields_9"

  6, 10, "city.t_trade_0"
  6, 11, "city.t_trade_1"
  6, 12, "city.t_trade_2"
  6, 13, "city.t_trade_3"
  6, 14, "city.t_trade_4"
  6, 15, "city.t_trade_5"
  6, 16, "city.t_trade_6"
  6, 17, "city.t_trade_7"
  6, 18, "city.t_trade_8"
  6, 19, "city.t_trade_9"

; Base building activities
  8, 15, "unit.fortress"
  8, 16, "unit.airbase"
  8, 17, "unit.buoy"

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

x_top_left = 576
y_top_left = 0
dx = 96
dy = 19

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
