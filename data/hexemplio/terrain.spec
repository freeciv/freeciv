
[spec]

; Format and options of this spec file:
options = "+Freeciv-spec-3.3-Devel-2023.Apr.05"

[info]

artists = "
    Tim F. Smith <yoohootim@hotmail.com> [TS]
    Andreas Røsdal <andrearo@pvv.ntnu.no> (hex mode)
    Daniel Speyer <dspeyer@users.sf.net> [DS]
    Architetto Francesco [http://www.public-domain-photos.com/] [AF]
    Peter Arbor <peter.arbor@gmail.com> [PA]
    GriffonSpade [GS]
    Unknown Battle For Wesnoth artists [BFW]
    Unknown Compass Artist [CA]
    Unknown Opengameart.org Artist(s)
    Unknown FreeCol Artist(s)
    Vegard Stolpnessaeter [VS]
    Canik
"

[file]
gfx = "hexemplio/terrain"

[grid_main]

x_top_left = 1
y_top_left = 1
dx = 126
dy = 64
pixel_border = 1

tiles = { "row", "column","tag"

; terrain - layer 0
  0,    0,  "t.l0.desert1"			;[?]
  0,    1,  "t.l0.plains1", "t.l0.savannah1"	;[PA]
  0,    1,  "t.l0.hills1"			;[PA]
  0,    1,  "t.l0.mountains1"		;[PA]
  0,    2,  "t.l0.grassland1"		;[PA]
  0,    2,  "t.l0.jungle1"			;[PA]
  0,    2,  "t.l0.forest1"			;[PA]
  0,    3,  "t.l0.tundra1"			;[PA]
  0,    4,  "t.l0.arctic1"			;[?]
  0,    5,  "t.l0.swamp1"			;{PA}

  0,    7,  "t.l0.coast1"			;[PA][GS]
  5,    6,  "t.l0.inaccessible1"	;[?][GS]

; terrain - layer 1
  5,    0,  "t.l1.hills1"
  5,    0,  "t.l1.mountains1"
  5,    0,  "t.l1.tundra1"
  5,    0,  "t.l1.arctic1"
  5,    0,  "t.l1.swamp1"
  5,    0,  "t.l1.jungle1"
  5,    0,  "t.l1.forest1"
  5,    0,  "t.l1.inaccessible1"

; Terrain special resources:
 1,   0, "ts.oil_0"                             ;[?]
 1,   0, "ts.arctic_oil_0"                      ;[?]
 1,   1, "ts.buffalo_0"                         ;[?]
 1,   2, "ts.grassland_resources_0"             ;[?]
 1,   2, "ts.river_resources_0"                 ;[?]
 1,   3, "ts.tundra_game_0"                     ;[?]
 1,   4, "ts.arctic_ivory_0"                    ;[?]
 1,   5, "ts.peat_0"                            ;[?]
 1,   6, "ts.coal_0"                            ;[?]
 1,   7, "ts.forest_game_0"                     ;[?]
 1,   8, "ts.gold_0"                            ;[?]

 2,   0, "ts.oasis_0"                           ;[BFW]
 2,   1, "ts.wheat_0"                           ;[?]
 2,   2, "ts.pheasant_0"                        ;[?]
 2,   3, "ts.furs_0"                            ;[?]
 2,   4, "ts.seals_0"                           ;[?]
 2,   5, "ts.spice_0"                           ;[?]
 2,   6, "ts.wine_0"                            ;[?]
 2,   7, "ts.silk_0"                            ;[?]
 2,   8, "ts.iron_0"                            ;[?]

 3,   6, "ts.fruit_0"                           ;[?]
 3,   7, "ts.gems_0"                            ;[?]
 3,   8, "ts.fish_0"                            ;[?]

 4,   8, "ts.whales_0"                          ;[?]

; Strategic Resources
 3,   0, "ts.saltpeter_0"                       ;[?]
 3,   1, "ts.aluminum_0"                        ;[?]
 3,   2, "ts.uranium_0"                         ;[?]
 3,   3, "ts.horses_0"                          ;[?]
 3,   4, "ts.elephant_0"                        ;[AF]
 3,   5, "ts.rubber_0"                          ;[GS]

;add-ons
 4, 0, "tx.oil_mine_0"                          ;[?]
 4, 1, "tx.mine_0"                              ;[?]
 4, 2, "tx.oil_rig_0"                           ;[?][GS]
 4, 3, "tx.irrigation"                          ;[GS]
 4, 4, "tx.farmland"                            ;[GS]
 4, 5, "tx.fallout_0"                           ;[?][GS]
 4, 6, "tx.pollution_0"                         ;[?]
 4, 7, "tx.village_0"                           ;[BFW][GS]

;misc
 5, 5, "mask.tile"
 5, 1, "t.dither_tile"
 5, 1, "tx.darkness"										;[DS][?][GS]
 0, 0, "t.coast_color"										;[?]
; 0, 0, "t.floor_color"										;[?]
 0, 0, "t.lake_color"										;[?]
; 0, 0, "t.inaccessible_water_color"						;[?]
 5, 0, "t.blend.coast"
 5, 0, "t.blend.ocean"
 5, 0, "t.blend.floor"
 5, 0, "t.blend.lake"
 0, 4, "t.blend.arctic"										;[?]
 5, 3, "user.attention"                                                                         ;[GS]
 5, 4, "tx.fog"
; 5, 6, "t.l0.charcoal1"									;[?]
; 5, 7, "t.l0.compass1"										;[CA][PA]
 5, 8, "user.infratile"

;goto path
 6, 0, "path.step"            ; turn boundary within path
 6, 1, "path.exhausted_mp"    ; tip of path, no MP left
 6, 2, "path.normal"          ; tip of path with MP remaining
 6, 3, "path.waypoint"

 6, 4, "tx.nets_0"

; Irrigation (as special type), and whether north, south, east, west

 4,  3, "tx.irrigation_s_n0e0se0s0w0nw0"
 4,  3, "tx.irrigation_s_n1e0se0s0w0nw0"
 4,  3, "tx.irrigation_s_n0e1se0s0w0nw0"
 4,  3, "tx.irrigation_s_n1e1se0s0w0nw0"
 4,  3, "tx.irrigation_s_n0e0se0s1w0nw0"
 4,  3, "tx.irrigation_s_n1e0se0s1w0nw0"
 4,  3, "tx.irrigation_s_n0e1se0s1w0nw0"
 4,  3, "tx.irrigation_s_n1e1se0s1w0nw0"
 4,  3, "tx.irrigation_s_n0e0se0s0w1nw0"
 4,  3, "tx.irrigation_s_n1e0se0s0w1nw0"
 4,  3, "tx.irrigation_s_n0e1se0s0w1nw0"
 4,  3, "tx.irrigation_s_n1e1se0s0w1nw0"
 4,  3, "tx.irrigation_s_n0e0se0s1w1nw0"
 4,  3, "tx.irrigation_s_n1e0se0s1w1nw0"
 4,  3, "tx.irrigation_s_n0e1se0s1w1nw0"
 4,  3, "tx.irrigation_s_n1e1se0s1w1nw0"
 4,  3, "tx.irrigation_s_n0e0se1s0w0nw0"
 4,  3, "tx.irrigation_s_n1e0se1s0w0nw0"
 4,  3, "tx.irrigation_s_n0e1se1s0w0nw0"
 4,  3, "tx.irrigation_s_n1e1se1s0w0nw0"
 4,  3, "tx.irrigation_s_n0e0se1s1w0nw0"
 4,  3, "tx.irrigation_s_n1e0se1s1w0nw0"
 4,  3, "tx.irrigation_s_n0e1se1s1w0nw0"
 4,  3, "tx.irrigation_s_n1e1se1s1w0nw0"
 4,  3, "tx.irrigation_s_n0e0se1s0w1nw0"
 4,  3, "tx.irrigation_s_n1e0se1s0w1nw0"
 4,  3, "tx.irrigation_s_n0e1se1s0w1nw0"
 4,  3, "tx.irrigation_s_n1e1se1s0w1nw0"
 4,  3, "tx.irrigation_s_n0e0se1s1w1nw0"
 4,  3, "tx.irrigation_s_n1e0se1s1w1nw0"
 4,  3, "tx.irrigation_s_n0e1se1s1w1nw0"
 4,  3, "tx.irrigation_s_n1e1se1s1w1nw0"
 4,  3, "tx.irrigation_s_n0e0se0s0w0nw1"
 4,  3, "tx.irrigation_s_n1e0se0s0w0nw1"
 4,  3, "tx.irrigation_s_n0e1se0s0w0nw1"
 4,  3, "tx.irrigation_s_n1e1se0s0w0nw1"
 4,  3, "tx.irrigation_s_n0e0se0s1w0nw1"
 4,  3, "tx.irrigation_s_n1e0se0s1w0nw1"
 4,  3, "tx.irrigation_s_n0e1se0s1w0nw1"
 4,  3, "tx.irrigation_s_n1e1se0s1w0nw1"
 4,  3, "tx.irrigation_s_n0e0se0s0w1nw1"
 4,  3, "tx.irrigation_s_n1e0se0s0w1nw1"
 4,  3, "tx.irrigation_s_n0e1se0s0w1nw1"
 4,  3, "tx.irrigation_s_n1e1se0s0w1nw1"
 4,  3, "tx.irrigation_s_n0e0se0s1w1nw1"
 4,  3, "tx.irrigation_s_n1e0se0s1w1nw1"
 4,  3, "tx.irrigation_s_n0e1se0s1w1nw1"
 4,  3, "tx.irrigation_s_n1e1se0s1w1nw1"
 4,  3, "tx.irrigation_s_n0e0se1s0w0nw1"
 4,  3, "tx.irrigation_s_n1e0se1s0w0nw1"
 4,  3, "tx.irrigation_s_n0e1se1s0w0nw1"
 4,  3, "tx.irrigation_s_n1e1se1s0w0nw1"
 4,  3, "tx.irrigation_s_n0e0se1s1w0nw1"
 4,  3, "tx.irrigation_s_n1e0se1s1w0nw1"
 4,  3, "tx.irrigation_s_n0e1se1s1w0nw1"
 4,  3, "tx.irrigation_s_n1e1se1s1w0nw1"
 4,  3, "tx.irrigation_s_n0e0se1s0w1nw1"
 4,  3, "tx.irrigation_s_n1e0se1s0w1nw1"
 4,  3, "tx.irrigation_s_n0e1se1s0w1nw1"
 4,  3, "tx.irrigation_s_n1e1se1s0w1nw1"
 4,  3, "tx.irrigation_s_n0e0se1s1w1nw1"
 4,  3, "tx.irrigation_s_n1e0se1s1w1nw1"
 4,  3, "tx.irrigation_s_n0e1se1s1w1nw1"
 4,  3, "tx.irrigation_s_n1e1se1s1w1nw1"

; Farmland (as special type), and whether north, south, east, west

 4,  4, "tx.farmland_s_n0e0se0s0w0nw0"
 4,  4, "tx.farmland_s_n1e0se0s0w0nw0"
 4,  4, "tx.farmland_s_n0e1se0s0w0nw0"
 4,  4, "tx.farmland_s_n1e1se0s0w0nw0"
 4,  4, "tx.farmland_s_n0e0se0s1w0nw0"
 4,  4, "tx.farmland_s_n1e0se0s1w0nw0"
 4,  4, "tx.farmland_s_n0e1se0s1w0nw0"
 4,  4, "tx.farmland_s_n1e1se0s1w0nw0"
 4,  4, "tx.farmland_s_n0e0se0s0w1nw0"
 4,  4, "tx.farmland_s_n1e0se0s0w1nw0"
 4,  4, "tx.farmland_s_n0e1se0s0w1nw0"
 4,  4, "tx.farmland_s_n1e1se0s0w1nw0"
 4,  4, "tx.farmland_s_n0e0se0s1w1nw0"
 4,  4, "tx.farmland_s_n1e0se0s1w1nw0"
 4,  4, "tx.farmland_s_n0e1se0s1w1nw0"
 4,  4, "tx.farmland_s_n1e1se0s1w1nw0"
 4,  4, "tx.farmland_s_n0e0se1s0w0nw0"
 4,  4, "tx.farmland_s_n1e0se1s0w0nw0"
 4,  4, "tx.farmland_s_n0e1se1s0w0nw0"
 4,  4, "tx.farmland_s_n1e1se1s0w0nw0"
 4,  4, "tx.farmland_s_n0e0se1s1w0nw0"
 4,  4, "tx.farmland_s_n1e0se1s1w0nw0"
 4,  4, "tx.farmland_s_n0e1se1s1w0nw0"
 4,  4, "tx.farmland_s_n1e1se1s1w0nw0"
 4,  4, "tx.farmland_s_n0e0se1s0w1nw0"
 4,  4, "tx.farmland_s_n1e0se1s0w1nw0"
 4,  4, "tx.farmland_s_n0e1se1s0w1nw0"
 4,  4, "tx.farmland_s_n1e1se1s0w1nw0"
 4,  4, "tx.farmland_s_n0e0se1s1w1nw0"
 4,  4, "tx.farmland_s_n1e0se1s1w1nw0"
 4,  4, "tx.farmland_s_n0e1se1s1w1nw0"
 4,  4, "tx.farmland_s_n1e1se1s1w1nw0"
 4,  4, "tx.farmland_s_n0e0se0s0w0nw1"
 4,  4, "tx.farmland_s_n1e0se0s0w0nw1"
 4,  4, "tx.farmland_s_n0e1se0s0w0nw1"
 4,  4, "tx.farmland_s_n1e1se0s0w0nw1"
 4,  4, "tx.farmland_s_n0e0se0s1w0nw1"
 4,  4, "tx.farmland_s_n1e0se0s1w0nw1"
 4,  4, "tx.farmland_s_n0e1se0s1w0nw1"
 4,  4, "tx.farmland_s_n1e1se0s1w0nw1"
 4,  4, "tx.farmland_s_n0e0se0s0w1nw1"
 4,  4, "tx.farmland_s_n1e0se0s0w1nw1"
 4,  4, "tx.farmland_s_n0e1se0s0w1nw1"
 4,  4, "tx.farmland_s_n1e1se0s0w1nw1"
 4,  4, "tx.farmland_s_n0e0se0s1w1nw1"
 4,  4, "tx.farmland_s_n1e0se0s1w1nw1"
 4,  4, "tx.farmland_s_n0e1se0s1w1nw1"
 4,  4, "tx.farmland_s_n1e1se0s1w1nw1"
 4,  4, "tx.farmland_s_n0e0se1s0w0nw1"
 4,  4, "tx.farmland_s_n1e0se1s0w0nw1"
 4,  4, "tx.farmland_s_n0e1se1s0w0nw1"
 4,  4, "tx.farmland_s_n1e1se1s0w0nw1"
 4,  4, "tx.farmland_s_n0e0se1s1w0nw1"
 4,  4, "tx.farmland_s_n1e0se1s1w0nw1"
 4,  4, "tx.farmland_s_n0e1se1s1w0nw1"
 4,  4, "tx.farmland_s_n1e1se1s1w0nw1"
 4,  4, "tx.farmland_s_n0e0se1s0w1nw1"
 4,  4, "tx.farmland_s_n1e0se1s0w1nw1"
 4,  4, "tx.farmland_s_n0e1se1s0w1nw1"
 4,  4, "tx.farmland_s_n1e1se1s0w1nw1"
 4,  4, "tx.farmland_s_n0e0se1s1w1nw1"
 4,  4, "tx.farmland_s_n1e0se1s1w1nw1"
 4,  4, "tx.farmland_s_n0e1se1s1w1nw1"
 4,  4, "tx.farmland_s_n1e1se1s1w1nw1"

}
