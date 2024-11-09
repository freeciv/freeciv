
[spec]

; Format and options of this spec file:
options = "+Freeciv-spec-3.3-Devel-2023.Apr.05"

[info]

artists = "
    Hogne Håskjold
    Daniel Speyer
    Yautja
    CapTVK
    GriffonSpade
    Gyubal Wahazar
    Canik
"

[file]
gfx = "amplio2/terrain1"

[grid_main]

x_top_left = 1
y_top_left = 1
dx = 96
dy = 48
pixel_border = 1

tiles = { "row", "column", "tag"

; terrain
 0,  0, "t.l0.desert1"

 1,  0, "t.l0.plains1"

 2,  0, "t.l0.grassland1"

 3,  0, "t.l0.forest1"

 4,  0, "t.l0.hills1"

 5,  0, "t.l0.mountains1"

 6,  0, "t.l0.tundra1"

 7,  0, "t.l0.arctic1"
;7,  0, "t.l1.arctic1" not redrawn
;7,  0, "t.l2.arctic1" not redrawn

 8,  0, "t.l0.swamp1"

 9,  0, "t.l0.jungle1"
10,  0, "t.l0.inaccessible1"

; Terrain special resources:

 0,  2, "ts.oasis0"
 0,  4, "ts.oil0"

 1,  2, "ts.buffalo0"
 1,  4, "ts.wheat0"

 2,  2, "ts.pheasant0"
 2,  4, "ts.silk0"

 3,  2, "ts.coal0"
 3,  4, "ts.wine0"

 4,  2, "ts.gold0"
 4,  4, "ts.iron0"

 5,  2, "ts.tundra_game0"
 5,  4, "ts.furs0"

 6,  2, "ts.arctic_ivory0"
 6,  4, "ts.arctic_oil0"

 7,  2, "ts.peat0"
 7,  4, "ts.spice0"

 8,  2, "ts.gems0"
 8,  4, "ts.fruit0"

 9,  2, "ts.fish0"
 9,  4, "ts.whales0"

 10, 2, "ts.seals0"
 10, 4, "ts.forest_game0"

 11, 2, "ts.horses0"
 11, 4, "ts.grassland_resources0", "ts.river_resources0"

; Roads
 12, 0, "road.road_isolated"
 12, 1, "road.road_n"
 12, 2, "road.road_ne"
 12, 3, "road.road_e"
 12, 4, "road.road_se"
 12, 5, "road.road_s"
 12, 6, "road.road_sw"
 12, 7, "road.road_w"
 12, 8, "road.road_nw"

; Rails
 13, 0, "road.rail_isolated"
 13, 1, "road.rail_n"
 13, 2, "road.rail_ne"
 13, 3, "road.rail_e"
 13, 4, "road.rail_se"
 13, 5, "road.rail_s"
 13, 6, "road.rail_sw"
 13, 7, "road.rail_w"
 13, 8, "road.rail_nw"

; Add-ons
 0,  6, "tx.oil_mine0"
 1,  6, "tx.irrigation"
 2,  6, "tx.farmland"
 3,  6, "tx.mine0"
 4,  6, "tx.pollution0"
 5,  6, "tx.village0"
 6,  6, "tx.fallout0"
 7,  6, "tx.oil_rig0"
 8,  6, "tx.nets0"

 15,  0, "t.dither_tile"
 15,  0, "tx.darkness"
 15,  2, "mask.tile"
 15,  2, "t.unknown1"
  7,  0, "t.blend.arctic" ; Ice over neighbors
 15,  3, "t.blend.coast"
 15,  3, "t.blend.lake"
 15,  4, "user.attention"
 15,  5, "tx.fog"
 15,  6, "user.infratile"

; Goto path sprites
 14,  7, "path.step"            ; Turn boundary within path
 14,  8, "path.exhausted_mp"    ; Tip of path, no MP left
 15,  7, "path.normal"          ; Tip of path with MP remaining
 15,  8, "path.waypoint"
}
