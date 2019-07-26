
[spec]

; Format and options of this spec file:
options = "+Freeciv-spec-Devel-2019-Jul-03"

[info]

artists = "
    Tommy <yobbo3@hotmail.com> (used tree, mountain and hill from isotrident tileset)
"

[file]
gfx = "hex2t/overlays"

[grid_main]

x_top_left = 2
y_top_left = 2
dx = 40
dy = 72
pixel_border = 2

tiles = { "row", "column","tag"
; All matched Overlays go in this file.
; This includes rivers, hills, mountains, forest and beaches.
; 
; Note that for matching a hex2 tileset
; the directions are classed as follows:
;
;          west (w)     north (n)
;                  .+'+.
;                 |     |  north-east (ne)
; south-west (sw) |     |
;                  '+,+'
;         south (s)     east (e)
;
; thus road.river_s_n1ne1e0s0sw1w0  will be connected to other river tiles
; at the top-right, right and left sides.


; river mouths:
; currently _ne and _sw don't seem to be implemented (2.0.0-rc1)

 1,  16, "road.river_outlet_n"
 1,  17, "road.river_outlet_ne"
 1,  18, "road.river_outlet_e"
 1,  19, "road.river_outlet_s"
 1,  20, "road.river_outlet_sw"
 1,  21, "road.river_outlet_w"

; river tiles:

 0,  0, "road.river_s_n0ne0e0s0sw0w0"
 0,  1, "road.river_s_n1ne0e0s0sw0w0"
 0,  2, "road.river_s_n0ne1e0s0sw0w0"
 0,  3, "road.river_s_n1ne1e0s0sw0w0"
 0,  4, "road.river_s_n0ne0e1s0sw0w0"
 0,  5, "road.river_s_n1ne0e1s0sw0w0"
 0,  6, "road.river_s_n0ne1e1s0sw0w0"
 0,  7, "road.river_s_n1ne1e1s0sw0w0"
 0,  8, "road.river_s_n0ne0e0s1sw0w0"
 0,  9, "road.river_s_n1ne0e0s1sw0w0"
 0, 10, "road.river_s_n0ne1e0s1sw0w0"
 0, 11, "road.river_s_n1ne1e0s1sw0w0"
 0, 12, "road.river_s_n0ne0e1s1sw0w0"
 0, 13, "road.river_s_n1ne0e1s1sw0w0"
 0, 14, "road.river_s_n0ne1e1s1sw0w0"
 0, 15, "road.river_s_n1ne1e1s1sw0w0"

 1,  0, "road.river_s_n0ne0e0s0sw1w0"
 1,  1, "road.river_s_n1ne0e0s0sw1w0"
 1,  2, "road.river_s_n0ne1e0s0sw1w0"
 1,  3, "road.river_s_n1ne1e0s0sw1w0"
 1,  4, "road.river_s_n0ne0e1s0sw1w0"
 1,  5, "road.river_s_n1ne0e1s0sw1w0"
 1,  6, "road.river_s_n0ne1e1s0sw1w0"
 1,  7, "road.river_s_n1ne1e1s0sw1w0"
 1,  8, "road.river_s_n0ne0e0s1sw1w0"
 1,  9, "road.river_s_n1ne0e0s1sw1w0"
 1, 10, "road.river_s_n0ne1e0s1sw1w0"
 1, 11, "road.river_s_n1ne1e0s1sw1w0"
 1, 12, "road.river_s_n0ne0e1s1sw1w0"
 1, 13, "road.river_s_n1ne0e1s1sw1w0"
 1, 14, "road.river_s_n0ne1e1s1sw1w0"
 1, 15, "road.river_s_n1ne1e1s1sw1w0"

 2,  0, "road.river_s_n0ne0e0s0sw0w1"
 2,  1, "road.river_s_n1ne0e0s0sw0w1"
 2,  2, "road.river_s_n0ne1e0s0sw0w1"
 2,  3, "road.river_s_n1ne1e0s0sw0w1"
 2,  4, "road.river_s_n0ne0e1s0sw0w1"
 2,  5, "road.river_s_n1ne0e1s0sw0w1"
 2,  6, "road.river_s_n0ne1e1s0sw0w1"
 2,  7, "road.river_s_n1ne1e1s0sw0w1"
 2,  8, "road.river_s_n0ne0e0s1sw0w1"
 2,  9, "road.river_s_n1ne0e0s1sw0w1"
 2, 10, "road.river_s_n0ne1e0s1sw0w1"
 2, 11, "road.river_s_n1ne1e0s1sw0w1"
 2, 12, "road.river_s_n0ne0e1s1sw0w1"
 2, 13, "road.river_s_n1ne0e1s1sw0w1"
 2, 14, "road.river_s_n0ne1e1s1sw0w1"
 2, 15, "road.river_s_n1ne1e1s1sw0w1"

 3,  0, "road.river_s_n0ne0e0s0sw1w1"
 3,  1, "road.river_s_n1ne0e0s0sw1w1"
 3,  2, "road.river_s_n0ne1e0s0sw1w1"
 3,  3, "road.river_s_n1ne1e0s0sw1w1"
 3,  4, "road.river_s_n0ne0e1s0sw1w1"
 3,  5, "road.river_s_n1ne0e1s0sw1w1"
 3,  6, "road.river_s_n0ne1e1s0sw1w1"
 3,  7, "road.river_s_n1ne1e1s0sw1w1"
 3,  8, "road.river_s_n0ne0e0s1sw1w1"
 3,  9, "road.river_s_n1ne0e0s1sw1w1"
 3, 10, "road.river_s_n0ne1e0s1sw1w1"
 3, 11, "road.river_s_n1ne1e0s1sw1w1"
 3, 12, "road.river_s_n0ne0e1s1sw1w1"
 3, 13, "road.river_s_n1ne0e1s1sw1w1"
 3, 14, "road.river_s_n0ne1e1s1sw1w1"
 3, 15, "road.river_s_n1ne1e1s1sw1w1"

; forest

 5,  0, "t.l1.forest_n0ne0e0s0sw0w0"
 5,  1, "t.l1.forest_n1ne0e0s0sw0w0"
 5,  2, "t.l1.forest_n0ne1e0s0sw0w0"
 5,  3, "t.l1.forest_n1ne1e0s0sw0w0"
 5,  4, "t.l1.forest_n0ne0e1s0sw0w0"
 5,  5, "t.l1.forest_n1ne0e1s0sw0w0"
 5,  6, "t.l1.forest_n0ne1e1s0sw0w0"
 5,  7, "t.l1.forest_n1ne1e1s0sw0w0"
 5,  8, "t.l1.forest_n0ne0e0s1sw0w0"
 5,  9, "t.l1.forest_n1ne0e0s1sw0w0"
 5, 10, "t.l1.forest_n0ne1e0s1sw0w0"
 5, 11, "t.l1.forest_n1ne1e0s1sw0w0"
 5, 12, "t.l1.forest_n0ne0e1s1sw0w0"
 5, 13, "t.l1.forest_n1ne0e1s1sw0w0"
 5, 14, "t.l1.forest_n0ne1e1s1sw0w0"
 5, 15, "t.l1.forest_n1ne1e1s1sw0w0"

 6,  0, "t.l1.forest_n0ne0e0s0sw1w0"
 6,  1, "t.l1.forest_n1ne0e0s0sw1w0"
 6,  2, "t.l1.forest_n0ne1e0s0sw1w0"
 6,  3, "t.l1.forest_n1ne1e0s0sw1w0"
 6,  4, "t.l1.forest_n0ne0e1s0sw1w0"
 6,  5, "t.l1.forest_n1ne0e1s0sw1w0"
 6,  6, "t.l1.forest_n0ne1e1s0sw1w0"
 6,  7, "t.l1.forest_n1ne1e1s0sw1w0"
 6,  8, "t.l1.forest_n0ne0e0s1sw1w0"
 6,  9, "t.l1.forest_n1ne0e0s1sw1w0"
 6, 10, "t.l1.forest_n0ne1e0s1sw1w0"
 6, 11, "t.l1.forest_n1ne1e0s1sw1w0"
 6, 12, "t.l1.forest_n0ne0e1s1sw1w0"
 6, 13, "t.l1.forest_n1ne0e1s1sw1w0"
 6, 14, "t.l1.forest_n0ne1e1s1sw1w0"
 6, 15, "t.l1.forest_n1ne1e1s1sw1w0"

 7,  0, "t.l1.forest_n0ne0e0s0sw0w1"
 7,  1, "t.l1.forest_n1ne0e0s0sw0w1"
 7,  2, "t.l1.forest_n0ne1e0s0sw0w1"
 7,  3, "t.l1.forest_n1ne1e0s0sw0w1"
 7,  4, "t.l1.forest_n0ne0e1s0sw0w1"
 7,  5, "t.l1.forest_n1ne0e1s0sw0w1"
 7,  6, "t.l1.forest_n0ne1e1s0sw0w1"
 7,  7, "t.l1.forest_n1ne1e1s0sw0w1"
 7,  8, "t.l1.forest_n0ne0e0s1sw0w1"
 7,  9, "t.l1.forest_n1ne0e0s1sw0w1"
 7, 10, "t.l1.forest_n0ne1e0s1sw0w1"
 7, 11, "t.l1.forest_n1ne1e0s1sw0w1"
 7, 12, "t.l1.forest_n0ne0e1s1sw0w1"
 7, 13, "t.l1.forest_n1ne0e1s1sw0w1"
 7, 14, "t.l1.forest_n0ne1e1s1sw0w1"
 7, 15, "t.l1.forest_n1ne1e1s1sw0w1"

 8,  0, "t.l1.forest_n0ne0e0s0sw1w1"
 8,  1, "t.l1.forest_n1ne0e0s0sw1w1"
 8,  2, "t.l1.forest_n0ne1e0s0sw1w1"
 8,  3, "t.l1.forest_n1ne1e0s0sw1w1"
 8,  4, "t.l1.forest_n0ne0e1s0sw1w1"
 8,  5, "t.l1.forest_n1ne0e1s0sw1w1"
 8,  6, "t.l1.forest_n0ne1e1s0sw1w1"
 8,  7, "t.l1.forest_n1ne1e1s0sw1w1"
 8,  8, "t.l1.forest_n0ne0e0s1sw1w1"
 8,  9, "t.l1.forest_n1ne0e0s1sw1w1"
 8, 10, "t.l1.forest_n0ne1e0s1sw1w1"
 8, 11, "t.l1.forest_n1ne1e0s1sw1w1"
 8, 12, "t.l1.forest_n0ne0e1s1sw1w1"
 8, 13, "t.l1.forest_n1ne0e1s1sw1w1"
 8, 14, "t.l1.forest_n0ne1e1s1sw1w1"
 8, 15, "t.l1.forest_n1ne1e1s1sw1w1"

; hills

 9,   0, "t.l1.hills_n0ne0e0s0sw0w0"
 9,   1, "t.l1.hills_n1ne0e0s0sw0w0"
 9,   2, "t.l1.hills_n0ne1e0s0sw0w0"
 9,   3, "t.l1.hills_n1ne1e0s0sw0w0"
 9,   4, "t.l1.hills_n0ne0e1s0sw0w0"
 9,   5, "t.l1.hills_n1ne0e1s0sw0w0"
 9,   6, "t.l1.hills_n0ne1e1s0sw0w0"
 9,   7, "t.l1.hills_n1ne1e1s0sw0w0"
 9,   8, "t.l1.hills_n0ne0e0s1sw0w0"
 9,   9, "t.l1.hills_n1ne0e0s1sw0w0"
 9,  10, "t.l1.hills_n0ne1e0s1sw0w0"
 9,  11, "t.l1.hills_n1ne1e0s1sw0w0"
 9,  12, "t.l1.hills_n0ne0e1s1sw0w0"
 9,  13, "t.l1.hills_n1ne0e1s1sw0w0"
 9,  14, "t.l1.hills_n0ne1e1s1sw0w0"
 9,  15, "t.l1.hills_n1ne1e1s1sw0w0"

 10,  0, "t.l1.hills_n0ne0e0s0sw1w0"
 10,  1, "t.l1.hills_n1ne0e0s0sw1w0"
 10,  2, "t.l1.hills_n0ne1e0s0sw1w0"
 10,  3, "t.l1.hills_n1ne1e0s0sw1w0"
 10,  4, "t.l1.hills_n0ne0e1s0sw1w0"
 10,  5, "t.l1.hills_n1ne0e1s0sw1w0"
 10,  6, "t.l1.hills_n0ne1e1s0sw1w0"
 10,  7, "t.l1.hills_n1ne1e1s0sw1w0"
 10,  8, "t.l1.hills_n0ne0e0s1sw1w0"
 10,  9, "t.l1.hills_n1ne0e0s1sw1w0"
 10, 10, "t.l1.hills_n0ne1e0s1sw1w0"
 10, 11, "t.l1.hills_n1ne1e0s1sw1w0"
 10, 12, "t.l1.hills_n0ne0e1s1sw1w0"
 10, 13, "t.l1.hills_n1ne0e1s1sw1w0"
 10, 14, "t.l1.hills_n0ne1e1s1sw1w0"
 10, 15, "t.l1.hills_n1ne1e1s1sw1w0"

 11,  0, "t.l1.hills_n0ne0e0s0sw0w1"
 11,  1, "t.l1.hills_n1ne0e0s0sw0w1"
 11,  2, "t.l1.hills_n0ne1e0s0sw0w1"
 11,  3, "t.l1.hills_n1ne1e0s0sw0w1"
 11,  4, "t.l1.hills_n0ne0e1s0sw0w1"
 11,  5, "t.l1.hills_n1ne0e1s0sw0w1"
 11,  6, "t.l1.hills_n0ne1e1s0sw0w1"
 11,  7, "t.l1.hills_n1ne1e1s0sw0w1"
 11,  8, "t.l1.hills_n0ne0e0s1sw0w1"
 11,  9, "t.l1.hills_n1ne0e0s1sw0w1"
 11, 10, "t.l1.hills_n0ne1e0s1sw0w1"
 11, 11, "t.l1.hills_n1ne1e0s1sw0w1"
 11, 12, "t.l1.hills_n0ne0e1s1sw0w1"
 11, 13, "t.l1.hills_n1ne0e1s1sw0w1"
 11, 14, "t.l1.hills_n0ne1e1s1sw0w1"
 11, 15, "t.l1.hills_n1ne1e1s1sw0w1"

 12,  0, "t.l1.hills_n0ne0e0s0sw1w1"
 12,  1, "t.l1.hills_n1ne0e0s0sw1w1"
 12,  2, "t.l1.hills_n0ne1e0s0sw1w1"
 12,  3, "t.l1.hills_n1ne1e0s0sw1w1"
 12,  4, "t.l1.hills_n0ne0e1s0sw1w1"
 12,  5, "t.l1.hills_n1ne0e1s0sw1w1"
 12,  6, "t.l1.hills_n0ne1e1s0sw1w1"
 12,  7, "t.l1.hills_n1ne1e1s0sw1w1"
 12,  8, "t.l1.hills_n0ne0e0s1sw1w1"
 12,  9, "t.l1.hills_n1ne0e0s1sw1w1"
 12, 10, "t.l1.hills_n0ne1e0s1sw1w1"
 12, 11, "t.l1.hills_n1ne1e0s1sw1w1"
 12, 12, "t.l1.hills_n0ne0e1s1sw1w1"
 12, 13, "t.l1.hills_n1ne0e1s1sw1w1"
 12, 14, "t.l1.hills_n0ne1e1s1sw1w1"
 12, 15, "t.l1.hills_n1ne1e1s1sw1w1"

; mountains!

 13,   0, "t.l1.mountains_n0ne0e0s0sw0w0"
 13,   1, "t.l1.mountains_n1ne0e0s0sw0w0"
 13,   2, "t.l1.mountains_n0ne1e0s0sw0w0"
 13,   3, "t.l1.mountains_n1ne1e0s0sw0w0"
 13,   4, "t.l1.mountains_n0ne0e1s0sw0w0"
 13,   5, "t.l1.mountains_n1ne0e1s0sw0w0"
 13,   6, "t.l1.mountains_n0ne1e1s0sw0w0"
 13,   7, "t.l1.mountains_n1ne1e1s0sw0w0"
 13,   8, "t.l1.mountains_n0ne0e0s1sw0w0"
 13,   9, "t.l1.mountains_n1ne0e0s1sw0w0"
 13,  10, "t.l1.mountains_n0ne1e0s1sw0w0"
 13,  11, "t.l1.mountains_n1ne1e0s1sw0w0"
 13,  12, "t.l1.mountains_n0ne0e1s1sw0w0"
 13,  13, "t.l1.mountains_n1ne0e1s1sw0w0"
 13,  14, "t.l1.mountains_n0ne1e1s1sw0w0"
 13,  15, "t.l1.mountains_n1ne1e1s1sw0w0"

 14,  0, "t.l1.mountains_n0ne0e0s0sw1w0"
 14,  1, "t.l1.mountains_n1ne0e0s0sw1w0"
 14,  2, "t.l1.mountains_n0ne1e0s0sw1w0"
 14,  3, "t.l1.mountains_n1ne1e0s0sw1w0"
 14,  4, "t.l1.mountains_n0ne0e1s0sw1w0"
 14,  5, "t.l1.mountains_n1ne0e1s0sw1w0"
 14,  6, "t.l1.mountains_n0ne1e1s0sw1w0"
 14,  7, "t.l1.mountains_n1ne1e1s0sw1w0"
 14,  8, "t.l1.mountains_n0ne0e0s1sw1w0"
 14,  9, "t.l1.mountains_n1ne0e0s1sw1w0"
 14, 10, "t.l1.mountains_n0ne1e0s1sw1w0"
 14, 11, "t.l1.mountains_n1ne1e0s1sw1w0"
 14, 12, "t.l1.mountains_n0ne0e1s1sw1w0"
 14, 13, "t.l1.mountains_n1ne0e1s1sw1w0"
 14, 14, "t.l1.mountains_n0ne1e1s1sw1w0"
 14, 15, "t.l1.mountains_n1ne1e1s1sw1w0"

 15,  0, "t.l1.mountains_n0ne0e0s0sw0w1"
 15,  1, "t.l1.mountains_n1ne0e0s0sw0w1"
 15,  2, "t.l1.mountains_n0ne1e0s0sw0w1"
 15,  3, "t.l1.mountains_n1ne1e0s0sw0w1"
 15,  4, "t.l1.mountains_n0ne0e1s0sw0w1"
 15,  5, "t.l1.mountains_n1ne0e1s0sw0w1"
 15,  6, "t.l1.mountains_n0ne1e1s0sw0w1"
 15,  7, "t.l1.mountains_n1ne1e1s0sw0w1"
 15,  8, "t.l1.mountains_n0ne0e0s1sw0w1"
 15,  9, "t.l1.mountains_n1ne0e0s1sw0w1"
 15, 10, "t.l1.mountains_n0ne1e0s1sw0w1"
 15, 11, "t.l1.mountains_n1ne1e0s1sw0w1"
 15, 12, "t.l1.mountains_n0ne0e1s1sw0w1"
 15, 13, "t.l1.mountains_n1ne0e1s1sw0w1"
 15, 14, "t.l1.mountains_n0ne1e1s1sw0w1"
 15, 15, "t.l1.mountains_n1ne1e1s1sw0w1"

 16,  0, "t.l1.mountains_n0ne0e0s0sw1w1"
 16,  1, "t.l1.mountains_n1ne0e0s0sw1w1"
 16,  2, "t.l1.mountains_n0ne1e0s0sw1w1"
 16,  3, "t.l1.mountains_n1ne1e0s0sw1w1"
 16,  4, "t.l1.mountains_n0ne0e1s0sw1w1"
 16,  5, "t.l1.mountains_n1ne0e1s0sw1w1"
 16,  6, "t.l1.mountains_n0ne1e1s0sw1w1"
 16,  7, "t.l1.mountains_n1ne1e1s0sw1w1"
 16,  8, "t.l1.mountains_n0ne0e0s1sw1w1"
 16,  9, "t.l1.mountains_n1ne0e0s1sw1w1"
 16, 10, "t.l1.mountains_n0ne1e0s1sw1w1"
 16, 11, "t.l1.mountains_n1ne1e0s1sw1w1"
 16, 12, "t.l1.mountains_n0ne0e1s1sw1w1"
 16, 13, "t.l1.mountains_n1ne0e1s1sw1w1"
 16, 14, "t.l1.mountains_n0ne1e1s1sw1w1"
 16, 15, "t.l1.mountains_n1ne1e1s1sw1w1"

  2, 16, "t.l0.coast1"
  2, 17, "t.l0.coast2"
  2, 18, "t.l0.floor1"
  2, 19, "t.l0.floor2"

}


[grid_coasts]

x_top_left = 674
y_top_left = 224
dx = 20
dy = 36
pixel_border = 1

tiles = { "row", "column","tag"

; cell_type = "rect" does work for a hex tileset, but it still uses rectangles.
; It's divided up like so:
;  ______    ______    ______    ______
; | |  | |  |      |  |__    |  |    __|
; | |__| |  |  __  |  |  |   |  |   |  |
; |      |  | |  | |  |__|   |  |   |__|
; |______|  |_|__|_|  |______|  |______|
;    up       down      left     right
;
; The little boxes are what gets drawn. The big boxes are the size of the normal
; drawing tile. The little boxes are each 1/4 the size of the big box.
; The overlap can be dealt with however we want. The area not covered is
; probably not an important part of the tile. If it is, make your
; normal_tile_height bigger.

; the usage of u{xxx} is similar to an isometric tileset.
; The ux1x and dx1x tags are just copies of ux0x and dx0x respectively
; (as with hex-2 the tiles are not connected vertically).

; coast tiles:
 0, 0,  "t.l0.coast_cell_u000"
 0, 1,  "t.l0.coast_cell_u100"
 0, 2,  "t.l0.coast_cell_u010"
 0, 3,  "t.l0.coast_cell_u110"
 0, 4,  "t.l0.coast_cell_u001"
 0, 5,  "t.l0.coast_cell_u101"
 0, 6,  "t.l0.coast_cell_u011"
 0, 7,  "t.l0.coast_cell_u111"
 
 1, 0,  "t.l0.coast_cell_r000"
 1, 1,  "t.l0.coast_cell_r100"
 1, 2,  "t.l0.coast_cell_r010"
 1, 3,  "t.l0.coast_cell_r110"
 1, 4,  "t.l0.coast_cell_r001"
 1, 5,  "t.l0.coast_cell_r101"
 1, 6,  "t.l0.coast_cell_r011"
 1, 7,  "t.l0.coast_cell_r111"

 2, 0,  "t.l0.coast_cell_d000"
 2, 1,  "t.l0.coast_cell_d100"
 2, 2,  "t.l0.coast_cell_d010"
 2, 3,  "t.l0.coast_cell_d110"
 2, 4,  "t.l0.coast_cell_d001"
 2, 5,  "t.l0.coast_cell_d101"
 2, 6,  "t.l0.coast_cell_d011"
 2, 7,  "t.l0.coast_cell_d111"

 3, 0,  "t.l0.coast_cell_l000"
 3, 1,  "t.l0.coast_cell_l100"
 3, 2,  "t.l0.coast_cell_l010"
 3, 3,  "t.l0.coast_cell_l110"
 3, 4,  "t.l0.coast_cell_l001"
 3, 5,  "t.l0.coast_cell_l101"
 3, 6,  "t.l0.coast_cell_l011"
 3, 7,  "t.l0.coast_cell_l111"

; ocean floor tiles:
 4, 0,  "t.l0.floor_cell_u000"
 4, 1,  "t.l0.floor_cell_u100"
 4, 2,  "t.l0.floor_cell_u010"
 4, 3,  "t.l0.floor_cell_u110"
 4, 4,  "t.l0.floor_cell_u001"
 4, 5,  "t.l0.floor_cell_u101"
 4, 6,  "t.l0.floor_cell_u011"
 4, 7,  "t.l0.floor_cell_u111"
 
 5, 0,  "t.l0.floor_cell_r000"
 5, 1,  "t.l0.floor_cell_r100"
 5, 2,  "t.l0.floor_cell_r010"
 5, 3,  "t.l0.floor_cell_r110"
 5, 4,  "t.l0.floor_cell_r001"
 5, 5,  "t.l0.floor_cell_r101"
 5, 6,  "t.l0.floor_cell_r011"
 5, 7,  "t.l0.floor_cell_r111"

 6, 0,  "t.l0.floor_cell_d000"
 6, 1,  "t.l0.floor_cell_d100"
 6, 2,  "t.l0.floor_cell_d010"
 6, 3,  "t.l0.floor_cell_d110"
 6, 4,  "t.l0.floor_cell_d001"
 6, 5,  "t.l0.floor_cell_d101"
 6, 6,  "t.l0.floor_cell_d011"
 6, 7,  "t.l0.floor_cell_d111"

 7, 0,  "t.l0.floor_cell_l000"
 7, 1,  "t.l0.floor_cell_l100"
 7, 2,  "t.l0.floor_cell_l010"
 7, 3,  "t.l0.floor_cell_l110"
 7, 4,  "t.l0.floor_cell_l001"
 7, 5,  "t.l0.floor_cell_l101"
 7, 6,  "t.l0.floor_cell_l011"
 7, 7,  "t.l0.floor_cell_l111"

}

