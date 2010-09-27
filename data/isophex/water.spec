
[spec]

; Format and options of this spec file:
options = "+spec3"

[info]

artists = "
    Tim F. Smith <yoohootim@hotmail.com>
    Daniel Speyer <dspeyer@users.sf.net> (mix)
    Frederic Rodrigo <f.rodrigo@tuxfamily.org> (mix)
    Andreas Røsdal <andrearo@pvv.ntnu.no> (hex mode)
"

[file]
gfx = "isophex/water"

[grid_main]

x_top_left = 1
y_top_left = 1
dx = 64
dy = 32
pixel_border = 1

tiles = { "row", "column","tag"
; Rivers are in rivers.spec

;river outlets

 10, 0, "tx.river_outlet_n"
 10, 1, "tx.river_outlet_e"
 10, 2, "tx.river_outlet_s"
 10, 3, "tx.river_outlet_w"

}


[grid_coasts]

x_top_left = 1
y_top_left = 429
dx = 32
dy = 16
pixel_border = 1

tiles = { "row", "column","tag"

; coast cell sprites.  See doc/README.graphics
 0, 0,  "t.l0.coast_cell_u000"
 0, 2,  "t.l0.coast_cell_u100"
 0, 4,  "t.l0.coast_cell_u010"
 0, 6,  "t.l0.coast_cell_u110"
 0, 8,  "t.l0.coast_cell_u001"
 0, 10, "t.l0.coast_cell_u101"
 0, 12, "t.l0.coast_cell_u011"
 0, 14, "t.l0.coast_cell_u111"
 
 1, 0,  "t.l0.coast_cell_d000"
 1, 2,  "t.l0.coast_cell_d100"
 1, 4,  "t.l0.coast_cell_d010"
 1, 6,  "t.l0.coast_cell_d110"
 1, 8,  "t.l0.coast_cell_d001"
 1, 10, "t.l0.coast_cell_d101"
 1, 12, "t.l0.coast_cell_d011"
 1, 14, "t.l0.coast_cell_d111"

 2, 0,  "t.l0.coast_cell_l000"
 2, 2,  "t.l0.coast_cell_l100"
 2, 4,  "t.l0.coast_cell_l010"
 2, 6,  "t.l0.coast_cell_l110"
 2, 8,  "t.l0.coast_cell_l001"
 2, 10, "t.l0.coast_cell_l101"
 2, 12, "t.l0.coast_cell_l011"
 2, 14, "t.l0.coast_cell_l111"

 2, 1,  "t.l0.coast_cell_r000"
 2, 3,  "t.l0.coast_cell_r100"
 2, 5,  "t.l0.coast_cell_r010"
 2, 7,  "t.l0.coast_cell_r110"
 2, 9,  "t.l0.coast_cell_r001"
 2, 11, "t.l0.coast_cell_r101"
 2, 13, "t.l0.coast_cell_r011"
 2, 15, "t.l0.coast_cell_r111"

; Deep Ocean cell sprites
 0, 16, "t.l0.floor_cell_u000"
 0, 2,  "t.l0.floor_cell_u100"
 0, 4,  "t.l0.floor_cell_u010"
 0, 6,  "t.l0.floor_cell_u110"
 0, 8,  "t.l0.floor_cell_u001"
 0, 10, "t.l0.floor_cell_u101"
 0, 12, "t.l0.floor_cell_u011"
 0, 14, "t.l0.floor_cell_u111"
 
 1, 16, "t.l0.floor_cell_d000"
 1, 2,  "t.l0.floor_cell_d100"
 1, 4,  "t.l0.floor_cell_d010"
 1, 6,  "t.l0.floor_cell_d110"
 1, 8,  "t.l0.floor_cell_d001"
 1, 10, "t.l0.floor_cell_d101"
 1, 12, "t.l0.floor_cell_d011"
 1, 14, "t.l0.floor_cell_d111"

 2, 16, "t.l0.floor_cell_l000"
 2, 2,  "t.l0.floor_cell_l100"
 2, 4,  "t.l0.floor_cell_l010"
 2, 6,  "t.l0.floor_cell_l110"
 2, 8,  "t.l0.floor_cell_l001"
 2, 10, "t.l0.floor_cell_l101"
 2, 12, "t.l0.floor_cell_l011"
 2, 14, "t.l0.floor_cell_l111"

 2, 17, "t.l0.floor_cell_r000"
 2, 3,  "t.l0.floor_cell_r100"
 2, 5,  "t.l0.floor_cell_r010"
 2, 7,  "t.l0.floor_cell_r110"
 2, 9,  "t.l0.floor_cell_r001"
 2, 11, "t.l0.floor_cell_r101"
 2, 13, "t.l0.floor_cell_r011"
 2, 15, "t.l0.floor_cell_r111"

}
