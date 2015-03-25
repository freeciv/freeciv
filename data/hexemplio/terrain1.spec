
[spec]

; Format and options of this spec file:
options = "+Freeciv-spec-Devel-2015-Mar-25"

[info]

artists = "
    Tim F. Smith <yoohootim@hotmail.com>
    Andreas Røsdal <andrearo@pvv.ntnu.no> (hex mode)
    Daniel Speyer <dspeyer@users.sf.net> 
    Architetto Francesco [http://www.public-domain-photos.com/]
    GriffonSpade
"

[file]
gfx = "hexemplio/terrain1"

[grid_main]

x_top_left = 1
y_top_left = 1
dx = 126
dy = 64
pixel_border = 1

tiles = { "row", "column","tag"

; terrain - layer 0
  0,    0,  "t.l0.desert1"
  0,    1,  "t.l0.plains1"
  0,    2,  "t.l0.grassland1"
  0,    3,  "t.l0.tundra1"
  0,    4,  "t.l0.arctic1"
  0,    5,  "t.l0.swamp1"
  0,    1,  "t.l0.jungle1"
  0,    1,  "t.l0.forest1"
  0,    6,  "t.l0.hills1"
  0,    6,  "t.l0.mountains1"
  0,    7,  "t.l0.coast1"
  0,    8,  "t.l0.inaccessible1"

; terrain - layer 1
  5,    0,  "t.l1.desert1"
  5,    0,  "t.l1.plains1"
  5,    0,  "t.l1.grassland1"
  5,    0,  "t.l1.tundra1"
  5,    0,  "t.l1.arctic1"
  5,    0,  "t.l1.swamp1"
  5,    0,  "t.l1.inaccessible1"

  5,    0,  "t.l1.jungle1"
  5,    0,  "t.l1.forest1"
  5,    0,  "t.l1.hills1"
  5,    0,  "t.l1.mountains1"

; Terrain special resources:
 1,   0, "ts.oil", "ts.arctic_oil"
 1,   1, "ts.buffalo"
 1,   2, "ts.grassland_resources", "ts.river_resources"
 1,   3, "ts.tundra_game"
 1,   4, "ts.arctic_ivory"
 1,   5, "ts.peat"

 2,   0, "ts.oasis"
 2,   1, "ts.wheat"
 2,   2, "ts.pheasant"
 2,   3, "ts.furs"
 2,   4, "ts.seals"
 2,   5, "ts.spice"

 1,   6, "ts.coal"
 2,   6, "ts.wine"

 3,   6, "ts.gold"
 4,   6, "ts.iron"

 1,   7, "ts.forest_game"
 2,   7, "ts.silk"

 3,   7, "ts.gems"
 4,   7, "ts.fruit"

 5,  6,  "ts.fish"
 5,  7,  "ts.whales"


; Strategic Resources
 3,   0, "ts.saltpeter"
 3,   1, "ts.aluminum"
 3,   2, "ts.uranium"
 3,   3, "ts.horses"
 3,   4, "ts.elephant"

;add-ons
 4, 0, "tx.oil_mine" 
 4, 1, "tx.mine"
 4, 2, "tx.irrigation"
 4, 3, "tx.farmland"
 4, 4, "tx.fallout"
 4, 5, "tx.pollution"
 3, 5, "tx.village"

;misc
 5, 1, "t.dither_tile"
 5, 1, "tx.darkness"
 0, 0, "t.coast_color"
; 0, 0, "t.floor_color"
 0, 0, "t.lake_color"
; 0, 0, "t.inaccessiblewater_color"
 5, 3, "t.blend.coast"
 5, 3, "t.blend.ocean"
 5, 3, "t.blend.floor"
 5, 3, "t.blend.lake"
 0, 4, "t.blend.arctic"
 0, 4, "t.blend.icyhills"
 5, 0, "t.blend.inaccessiblewater"
 5, 4, "user.attention"
 5, 5, "tx.fog"
 5, 8, "mask.tile"
}
