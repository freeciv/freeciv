
[spec]

; Format and options of this spec file:
options = "+Freeciv-spec-Devel-2013.Feb.13"

[info]

artists = "
    Tim F. Smith <yoohootim@hotmail.com>
    Andreas Røsdal <andrearo@pvv.ntnu.no> (hex mode)
    Daniel Speyer <dspeyer@users.sf.net> 
    GriffonSpade
"

[file]
gfx = "hexemplio/terrain1"

[grid_main]

x_top_left = 1
y_top_left = 1
dx = 128
dy = 64
pixel_border = 1

tiles = { "row", "column","tag"

; terrain - layer 0
  0,    0,  "t.l0.desert1"
  0,    0,  "t.l0.dryhills1"
  0,    1,  "t.l0.plains1"
  0,    2,  "t.l0.grassland1"
  0,    3,  "t.l0.tundra1"
  0,    3,  "t.l0.taiga1"
  0,    3,  "t.l0.highlands1"
  0,    4,  "t.l0.arctic1"
  0,    4,  "t.l0.icyhills1"
  0,    5,  "t.l0.swamp1"
  0,    1,  "t.l0.jungle1"
  0,    1,  "t.l0.forest1"
  0,    6,  "t.l0.hills1"
  0,    6,  "t.l0.mountains1"
  5,    0,  "t.l1.mountains1"

; terrain - layer 1
  5,    0,  "t.l1.dryhills1"
  5,    0,  "t.l1.taiga1"
  5,    0,  "t.l1.highlands1"
  5,    0,  "t.l1.arctic1"
  5,    0,  "t.l1.icyhills1"
  5,    0,  "t.l1.jungle1"
  5,    0,  "t.l1.forest1"
  5,    0,  "t.l1.hills1"

;currently blank?
; 0,   10, "t.l0.ocean1"

; Terrain special resources:
 1,   0, "ts.oil", "ts.arctic_oil"
 1,   1, "ts.buffalo"
 1,   2, "ts.grassland_resources", "ts.river_resources"
 1,   3, "ts.tundra_game"
 1,   4, "ts.arctic_ivory"
 1,   5, "ts.peat"
 1,   6, "ts.gems"
 1,   7, "ts.forest_game"
 1,   8, "ts.coal"
 1,   9, "ts.gold"
 1,  10, "ts.fish"

 2,   0, "ts.oasis"
 2,   1, "ts.wheat"
 2,   7, "ts.pheasant"
 2,   3, "ts.furs"
 2,   4, "ts.seals"
 2,   5, "ts.spice"
 2,   6, "ts.fruit"
 2,   7, "ts.silk"
 2,   8, "ts.wine"
 2,   9, "ts.iron"
 2,  10, "ts.whales"

; Strategic Resources
 3,   0, "ts.saltpeter"
 3,   1, "ts.aluminum"
 3,   2, "ts.horses"
 3,   3, "ts.uranium"


;add-ons
 4, 0, "tx.oil_mine" 
 4, 1, "tx.mine"
 4, 2, "tx.irrigation"
 4, 3, "tx.farmland"
 4, 4, "tx.fallout"
 4, 5, "tx.pollution"
 4, 6, "tx.village"

;misc
 5, 1, "t.dither_tile"
 5, 1, "tx.darkness"
 5, 3, "mask.tile"
 0, 0, "t.coast_color"
; 0, 0, "t.floor_color"
 0, 0, "t.lake_color"
 0, 10, "t.blend.coast"
 0, 10, "t.blend.ocean"
 0, 10, "t.blend.floor"
 0, 10, "t.blend.lake"
 5, 4, "user.attention"
 5, 5, "tx.fog"
}
