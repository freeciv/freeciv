
[spec]

; Format and options of this spec file:
options = "+Freeciv-spec-3.3-Devel-2023.Apr.05"

[info]

artists = "
    Tim F. Smith <yoohootim@hotmail.com>
    GriffonSpade (oil_rig)
"

[file]
gfx = "isotrident/terrain1"

[grid_main]

x_top_left = 1
y_top_left = 1
dx = 64
dy = 32
pixel_border = 1

tiles = { "row", "column","tag"

; terrain
  0,    0,  "t.l0.desert1"
;  0,    1,  "t.l0.desert2"

  1,    0,  "t.l0.plains1"
;  1,    1,  "t.l0.plains2"

  2,    0,  "t.l0.grassland1"
;  2,    1, "t.l0.grassland2"

  3,    0, "t.l0.forest1"
;  3,    1, "t.l0.forest2"

  4,    0, "t.l0.hills1"
;  4,    1, "t.l0.hills2"

  5,    0, "t.l0.mountains1"
;  5,    1, "t.l0.mountains2"

  6,    0, "t.l0.tundra1"

  7,    0, "t.l0.arctic1"

  8,    0, "t.l0.swamp1"

  9,    0, "t.l0.jungle1"

 10,    0, "t.l0.inaccessible1"

; Terrain special resources:

 0,   2, "ts.oasis:0"
 0,   3, "ts.oil:0"

 1,   2, "ts.buffalo:0"
 1,   3, "ts.wheat:0"

 2,  2, "ts.fish:0"
 2,  3, "ts.whales:0"

 7,   7, "ts.grassland_resources:0", "ts.river_resources:0"

 3,   2, "ts.pheasant:0"
 3,   3, "ts.silk:0"

 4,   2, "ts.coal:0"
 4,   3, "ts.wine:0"

 5,   2, "ts.gold:0"
 5,   3, "ts.iron:0"

 6,   2, "ts.tundra_game:0"
 6,   3, "ts.furs:0"

 7,   2, "ts.arctic_ivory:0"
 7,   3, "ts.arctic_oil:0"

 8,   2, "ts.peat:0"
 8,   3, "ts.spice:0"

 9,   2, "ts.gems:0"
 9,   3, "ts.fruit:0"
 9,   4, "ts.horses:0"
 9,   5, "ts.seals:0"
 9,   6, "ts.forest_game:0"


; Maglevs
 10, 1, "road.maglev_n:0"
 10, 2, "road.maglev_ne:0"
 10, 3, "road.maglev_e:0"
 10, 4, "road.maglev_se:0"
 10, 5, "road.maglev_s:0"
 10, 6, "road.maglev_sw:0"
 10, 7, "road.maglev_w:0"
 10, 8, "road.maglev_nw:0"

; Roads
 11, 1, "road.road_n:0"
 11, 2, "road.road_ne:0"
 11, 3, "road.road_e:0"
 11, 4, "road.road_se:0"
 11, 5, "road.road_s:0"
 11, 6, "road.road_sw:0"
 11, 7, "road.road_w:0"
 11, 8, "road.road_nw:0"

; Rails
 12, 1, "road.rail_n:0"
 12, 2, "road.rail_ne:0"
 12, 3, "road.rail_e:0"
 12, 4, "road.rail_se:0"
 12, 5, "road.rail_s:0"
 12, 6, "road.rail_sw:0"
 12, 7, "road.rail_w:0"
 12, 8, "road.rail_nw:0"

; Add-ons
 2, 7, "tx.oil_mine:0"
 2, 8, "tx.oil_rig:0"
 3, 7, "tx.irrigation:0"
 4, 7, "tx.farmland:0"
 5, 7, "tx.mine:0"
 6, 7, "tx.pollution:0"
 8, 7, "tx.village:0"
 8, 8, "tx.nets:0"
 9, 7, "tx.fallout:0"
}


[grid_extra]

x_top_left = 1
y_top_left = 447
dx = 64
dy = 32
pixel_border = 1

tiles = { "row", "column","tag"
  0, 0, "t.dither_tile"
  0, 0, "tx.darkness"
  0, 2, "mask.tile"
  0, 2, "t.unknown1"
  0, 3, "t.blend.ocean"
  0, 3, "t.blend.coast"
  0, 4, "user.attention", "user.infratile"
  0, 5, "tx.fog"

  1, 0, "path.step"            ; Turn boundary within path
  1, 1, "path.exhausted_mp"    ; Tip of path, no MP left
  1, 2, "path.normal"          ; Tip of path with MP remaining
  1, 3, "path.waypoint"
}


[grid_isolated]

x_top_left = 380
y_top_left = 1
dx = 64
dy = 32
pixel_border = 1

tiles = { "row", "column","tag"
  0, 0, "road.road_isolated:0"
  1, 0, "road.rail_isolated:0"
  2, 0, "road.maglev_isolated:0"
}
