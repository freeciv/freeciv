
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
    Canik
"

[file]
gfx = "amplio/terrain1"

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

; Terrain special resources:

 0,  2, "ts.oasis:0"
 0,  4, "ts.oil:0"

 1,  2, "ts.buffalo:0"
 1,  4, "ts.wheat:0"

 2,  2, "ts.pheasant:0"
 2,  4, "ts.silk:0"

 3,  2, "ts.coal:0"
 3,  4, "ts.wine:0"

 4,  2, "ts.gold:0"
 4,  4, "ts.iron:0"

 5,  2, "ts.tundra_game:0"
 5,  4, "ts.furs:0"

 6,  2, "ts.arctic_ivory:0"
 6,  4, "ts.arctic_oil:0"

 7,  2, "ts.peat:0"
 7,  4, "ts.spice:0"

 8,  2, "ts.gems:0"
 8,  4, "ts.fruit:0"

 9,  2, "ts.fish:0"
 9,  4, "ts.whales:0"

 10, 2, "ts.seals:0"
 10, 4, "ts.forest_game:0"

 11, 2, "ts.horses:0"
 11, 4, "ts.grassland_resources:0", "ts.river_resources:0"

; Roads
 12, 0, "road.road_isolated:0"
 12, 1, "road.road_n:0"
 12, 2, "road.road_ne:0"
 12, 3, "road.road_e:0"
 12, 4, "road.road_se:0"
 12, 5, "road.road_s:0"
 12, 6, "road.road_sw:0"
 12, 7, "road.road_w:0"
 12, 8, "road.road_nw:0"

; Rails
 13, 0, "road.rail_isolated:0"
 13, 1, "road.rail_n:0"
 13, 2, "road.rail_ne:0"
 13, 3, "road.rail_e:0"
 13, 4, "road.rail_se:0"
 13, 5, "road.rail_s:0"
 13, 6, "road.rail_sw:0"
 13, 7, "road.rail_w:0"
 13, 8, "road.rail_nw:0"

; Other extras
 0,  6, "tx.oil_mine:0"
 1,  6, "tx.irrigation:0"
 2,  6, "tx.farmland:0"
 3,  6, "tx.mine:0"
 4,  6, "tx.pollution:0"
 5,  6, "tx.village:0"
 6,  6, "tx.fallout:0"
 7,  6, "tx.oil_rig:0"
 8,  6, "tx.nets:0"

 15,  0, "t.dither_tile"
 15,  0, "tx.darkness"
 15,  2, "mask.tile"
 15,  2, "t.unknown1"
  7,  0, "t.blend.arctic" ;ice over neighbors
 15,  3, "t.blend.coast"
 15,  3, "t.blend.lake"
 15,  4, "user.attention"
 15,  5, "tx.fog"
 15,  6, "user.infratile"

;goto path sprites
 14,  7, "path.exhausted_mp"    ; tip of path, no MP left
 15,  7, "path.normal"          ; tip of path with MP remaining
 14,  8, "path.step"            ; turn boundary within path
 15,  8, "path.waypoint"
}
