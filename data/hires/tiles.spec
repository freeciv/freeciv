
[spec]

; Format and options of this spec file:
options = "+spec2"

[info]

artists = "
    Tatu Rissanen <tatu.rissanen@hut.fi>
    Jeff Mallatt <jjm@codewell.com> (miscellaneous)
"

[file]
gfx = "trident/tiles"

[grid_main]

x_top_left = 0
y_top_left = 0
dx = 30
dy = 30

tiles = { "row", "column", "tag"
; Unit activity letters:  (note unit icons have just "u.")

  9, 18, "unit.auto_attack",
         "unit.auto_settler"
  9, 19, "unit.stack"
 10, 18, "unit.connect"
 10, 19, "unit.auto_explore"
 11, 12, "unit.transform"
 11, 13, "unit.sentry"
 11, 14, "unit.goto"
 11, 15, "unit.mine"
 11, 16, "unit.pollution"
 11, 17, "unit.road"
 11, 18, "unit.irrigate"
 11, 19, "unit.fortifying",
         "unit.fortress"
 12, 16, "unit.airbase"
 12, 17, "unit.pillage"
; 12, 18, "unit.fortified" in cities.spec
 12, 19, "unit.fallout"
 13, 19, "unit.patrol"

; Unit hit-point bars: approx percent of hp remaining

 16,  0, "unit.hp_100"
 16,  1, "unit.hp_90"
 16,  2, "unit.hp_80"
 16,  3, "unit.hp_70"
 16,  4, "unit.hp_60"
 16,  5, "unit.hp_50"
 16,  6, "unit.hp_40"
 16,  7, "unit.hp_30"
 16,  8, "unit.hp_20"
 16,  9, "unit.hp_10"
 16, 10, "unit.hp_0"

; Numbers: city size:

 14,  0, "city.size_0"
 14,  1, "city.size_1"
 14,  2, "city.size_2"
 14,  3, "city.size_3"
 14,  4, "city.size_4"
 14,  5, "city.size_5"
 14,  6, "city.size_6"
 14,  7, "city.size_7"
 14,  8, "city.size_8"
 14,  9, "city.size_9"
 14, 10, "city.size_10"
 14, 11, "city.size_20"
 14, 12, "city.size_30"
 14, 13, "city.size_40"
 14, 14, "city.size_50"
 14, 15, "city.size_60"
 14, 16, "city.size_70"
 14, 17, "city.size_80"
 14, 18, "city.size_90"

; Numbers: city tile food/shields/trade y/g/b

 17,  0, "city.t_food_0"
 17,  1, "city.t_food_1"
 17,  2, "city.t_food_2"
 17,  3, "city.t_food_3"
 17,  4, "city.t_food_4"
 17,  5, "city.t_food_5"
 17,  6, "city.t_food_6"
 17,  7, "city.t_food_7"
 17,  8, "city.t_food_8"
 17,  9, "city.t_food_9"

 15,  0, "city.t_shields_0"
 15,  1, "city.t_shields_1"
 15,  2, "city.t_shields_2"
 15,  3, "city.t_shields_3"
 15,  4, "city.t_shields_4"
 15,  5, "city.t_shields_5"
 15,  6, "city.t_shields_6"
 15,  7, "city.t_shields_7"
 15,  8, "city.t_shields_8"
 15,  9, "city.t_shields_9"

 15, 10, "city.t_trade_0"
 15, 11, "city.t_trade_1"
 15, 12, "city.t_trade_2"
 15, 13, "city.t_trade_3"
 15, 14, "city.t_trade_4"
 15, 15, "city.t_trade_5"
 15, 16, "city.t_trade_6"
 15, 17, "city.t_trade_7"
 15, 18, "city.t_trade_8"
 15, 19, "city.t_trade_9"

; Unit upkeep in city dialog:
; These should probably be handled differently and have
; a different size...

 16, 15, "upkeep.food"
 16, 16, "upkeep.food2"
 16, 17, "upkeep.unhappy"
 16, 18, "upkeep.unhappy2"
 16, 19, "upkeep.shield"

; Nuclear explosion: this could maybe now be handled as one 
; big graphic (?), but for now is done old way as 3 by 3:

  1, 17, "explode.nuke_00"
  1, 18, "explode.nuke_01"
  1, 19, "explode.nuke_02"
  2, 17, "explode.nuke_10"
  2, 18, "explode.nuke_11"
  2, 19, "explode.nuke_12"
  3, 17, "explode.nuke_20"
  3, 18, "explode.nuke_21"
  3, 19, "explode.nuke_22"

}
