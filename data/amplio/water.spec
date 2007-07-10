
[spec]

; Format and options of this spec file:
options = "+spec3"

[info]

artists = "
    Hogne Håskjold
    Tim F. Smith <yoohootim@hotmail.com>
    Yautja
    Daniel Speyer
    Eleazar
"

[file]
gfx = "amplio/water"

[grid_main]

x_top_left = 1
y_top_left = 1
dx = 96
dy = 48
pixel_border = 1

tiles = { "row", "column","tag"

; Rivers (as special type), and whether north, south, east, west 
; also has river or is ocean:

 2,  0, "tx.s_river_n0e0s0w0"
 2,  1, "tx.s_river_n0e0s0w1"
 2,  2, "tx.s_river_n0e0s1w0"
 2,  3, "tx.s_river_n0e0s1w1"
 2,  4, "tx.s_river_n0e1s0w0"
 2,  5, "tx.s_river_n0e1s0w1"
 2,  6, "tx.s_river_n0e1s1w0"
 2,  7, "tx.s_river_n0e1s1w1"
 3,  0, "tx.s_river_n1e0s0w0"
 3,  1, "tx.s_river_n1e0s0w1"
 3,  2, "tx.s_river_n1e0s1w0"
 3,  3, "tx.s_river_n1e0s1w1"
 3,  4, "tx.s_river_n1e1s0w0"
 3,  5, "tx.s_river_n1e1s0w1"
 3,  6, "tx.s_river_n1e1s1w0"
 3,  7, "tx.s_river_n1e1s1w1"

; Rivers as overlay

 2,  0, "t.t_river_n0e0s0w0"
 2,  1, "t.t_river_n0e0s0w1"
 2,  2, "t.t_river_n0e0s1w0"
 2,  3, "t.t_river_n0e0s1w1"
 2,  4, "t.t_river_n0e1s0w0"
 2,  5, "t.t_river_n0e1s0w1"
 2,  6, "t.t_river_n0e1s1w0"
 2,  7, "t.t_river_n0e1s1w1"
 3,  0, "t.t_river_n1e0s0w0"
 3,  1, "t.t_river_n1e0s0w1"
 3,  2, "t.t_river_n1e0s1w0"
 3,  3, "t.t_river_n1e0s1w1"
 3,  4, "t.t_river_n1e1s0w0"
 3,  5, "t.t_river_n1e1s0w1"
 3,  6, "t.t_river_n1e1s1w0"
 3,  7, "t.t_river_n1e1s1w1"

;river outlets

 10, 0, "tx.river_outlet_n"
 10, 1, "tx.river_outlet_e"
 10, 2, "tx.river_outlet_s"
 10, 3, "tx.river_outlet_w"

; ocean ridge as overlay

 6,  0, "t.l1.ridge_n0e0s0w0"
 6,  1, "t.l1.ridge_n0e0s0w1"
 6,  2, "t.l1.ridge_n0e0s1w0"
 6,  3, "t.l1.ridge_n0e0s1w1"
 6,  4, "t.l1.ridge_n0e1s0w0"
 6,  5, "t.l1.ridge_n0e1s0w1"
 6,  6, "t.l1.ridge_n0e1s1w0"
 6,  7, "t.l1.ridge_n0e1s1w1"
 7,  0, "t.l1.ridge_n1e0s0w0"
 7,  1, "t.l1.ridge_n1e0s0w1"
 7,  2, "t.l1.ridge_n1e0s1w0"
 7,  3, "t.l1.ridge_n1e0s1w1"
 7,  4, "t.l1.ridge_n1e1s0w0"
 7,  5, "t.l1.ridge_n1e1s0w1"
 7,  6, "t.l1.ridge_n1e1s1w0"
 7,  7, "t.l1.ridge_n1e1s1w1"

; ocean trench as overlay

 8,  0, "t.l1.trench_n0e0s0w0"
 8,  1, "t.l1.trench_n0e0s0w1"
 8,  2, "t.l1.trench_n0e0s1w0"
 8,  3, "t.l1.trench_n0e0s1w1"
 8,  4, "t.l1.trench_n0e1s0w0"
 8,  5, "t.l1.trench_n0e1s0w1"
 8,  6, "t.l1.trench_n0e1s1w0"
 8,  7, "t.l1.trench_n0e1s1w1"
 9,  0, "t.l1.trench_n1e0s0w0"
 9,  1, "t.l1.trench_n1e0s0w1"
 9,  2, "t.l1.trench_n1e0s1w0"
 9,  3, "t.l1.trench_n1e0s1w1"
 9,  4, "t.l1.trench_n1e1s0w0"
 9,  5, "t.l1.trench_n1e1s0w1"
 9,  6, "t.l1.trench_n1e1s1w0"
 9,  7, "t.l1.trench_n1e1s1w1"

}


[grid_coasts]

x_top_left = 1
y_top_left = 645
dx = 48
dy = 24
pixel_border = 1

tiles = { "row", "column","tag"

; ocean cell sprites.  See doc/README.graphics
 0, 0,  "t.l0.ocean_cell_u000"
 0, 2,  "t.l0.ocean_cell_u100"
 0, 4,  "t.l0.ocean_cell_u010"
 0, 6,  "t.l0.ocean_cell_u110"
 0, 8,  "t.l0.ocean_cell_u001"
 0, 10, "t.l0.ocean_cell_u101"
 0, 12, "t.l0.ocean_cell_u011"
 0, 14, "t.l0.ocean_cell_u111"
 
 1, 0,  "t.l0.ocean_cell_d000"
 1, 2,  "t.l0.ocean_cell_d100"
 1, 4,  "t.l0.ocean_cell_d010"
 1, 6,  "t.l0.ocean_cell_d110"
 1, 8,  "t.l0.ocean_cell_d001"
 1, 10, "t.l0.ocean_cell_d101"
 1, 12, "t.l0.ocean_cell_d011"
 1, 14, "t.l0.ocean_cell_d111"

 2, 0,  "t.l0.ocean_cell_l000"
 2, 2,  "t.l0.ocean_cell_l100"
 2, 4,  "t.l0.ocean_cell_l010"
 2, 6,  "t.l0.ocean_cell_l110"
 2, 8,  "t.l0.ocean_cell_l001"
 2, 10, "t.l0.ocean_cell_l101"
 2, 12, "t.l0.ocean_cell_l011"
 2, 14, "t.l0.ocean_cell_l111"

 2, 1,  "t.l0.ocean_cell_r000"
 2, 3,  "t.l0.ocean_cell_r100"
 2, 5,  "t.l0.ocean_cell_r010"
 2, 7,  "t.l0.ocean_cell_r110"
 2, 9,  "t.l0.ocean_cell_r001"
 2, 11, "t.l0.ocean_cell_r101"
 2, 13, "t.l0.ocean_cell_r011"
 2, 15, "t.l0.ocean_cell_r111"

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

; shelf fallback to coast tiles
 0, 0,  "t.l0.shelf_cell_u000"
 0, 2,  "t.l0.shelf_cell_u100"
 0, 4,  "t.l0.shelf_cell_u010"
 0, 6,  "t.l0.shelf_cell_u110"
 0, 8,  "t.l0.shelf_cell_u001"
 0, 10, "t.l0.shelf_cell_u101"
 0, 12, "t.l0.shelf_cell_u011"
 0, 14, "t.l0.shelf_cell_u111"
 
 1, 0,  "t.l0.shelf_cell_d000"
 1, 2,  "t.l0.shelf_cell_d100"
 1, 4,  "t.l0.shelf_cell_d010"
 1, 6,  "t.l0.shelf_cell_d110"
 1, 8,  "t.l0.shelf_cell_d001"
 1, 10, "t.l0.shelf_cell_d101"
 1, 12, "t.l0.shelf_cell_d011"
 1, 14, "t.l0.shelf_cell_d111"

 2, 0,  "t.l0.shelf_cell_l000"
 2, 2,  "t.l0.shelf_cell_l100"
 2, 4,  "t.l0.shelf_cell_l010"
 2, 6,  "t.l0.shelf_cell_l110"
 2, 8,  "t.l0.shelf_cell_l001"
 2, 10, "t.l0.shelf_cell_l101"
 2, 12, "t.l0.shelf_cell_l011"
 2, 14, "t.l0.shelf_cell_l111"

 2, 1,  "t.l0.shelf_cell_r000"
 2, 3,  "t.l0.shelf_cell_r100"
 2, 5,  "t.l0.shelf_cell_r010"
 2, 7,  "t.l0.shelf_cell_r110"
 2, 9,  "t.l0.shelf_cell_r001"
 2, 11, "t.l0.shelf_cell_r101"
 2, 13, "t.l0.shelf_cell_r011"
 2, 15, "t.l0.shelf_cell_r111"

; Deep Ocean sprites.
 3, 0,  "t.l0.deep_cell_u000"
 3, 2,  "t.l0.deep_cell_u100"
 3, 4,  "t.l0.deep_cell_u010"
 3, 6,  "t.l0.deep_cell_u110"
 3, 8,  "t.l0.deep_cell_u001"
 3, 10, "t.l0.deep_cell_u101"
 3, 12, "t.l0.deep_cell_u011"
 3, 14, "t.l0.deep_cell_u111"
 
 4, 0,  "t.l0.deep_cell_d000"
 4, 2,  "t.l0.deep_cell_d100"
 4, 4,  "t.l0.deep_cell_d010"
 4, 6,  "t.l0.deep_cell_d110"
 4, 8,  "t.l0.deep_cell_d001"
 4, 10, "t.l0.deep_cell_d101"
 4, 12, "t.l0.deep_cell_d011"
 4, 14, "t.l0.deep_cell_d111"

 5, 0,  "t.l0.deep_cell_l000"
 5, 2,  "t.l0.deep_cell_l100"
 5, 4,  "t.l0.deep_cell_l010"
 5, 6,  "t.l0.deep_cell_l110"
 5, 8,  "t.l0.deep_cell_l001"
 5, 10, "t.l0.deep_cell_l101"
 5, 12, "t.l0.deep_cell_l011"
 5, 14, "t.l0.deep_cell_l111"

 5, 1,  "t.l0.deep_cell_r000"
 5, 3,  "t.l0.deep_cell_r100"
 5, 5,  "t.l0.deep_cell_r010"
 5, 7,  "t.l0.deep_cell_r110"
 5, 9,  "t.l0.deep_cell_r001"
 5, 11, "t.l0.deep_cell_r101"
 5, 13, "t.l0.deep_cell_r011"
 5, 15, "t.l0.deep_cell_r111"

; ocean floor sprites.
 3, 0,  "t.l0.floor_cell_u000"
 3, 2,  "t.l0.floor_cell_u100"
 3, 4,  "t.l0.floor_cell_u010"
 3, 6,  "t.l0.floor_cell_u110"
 3, 8,  "t.l0.floor_cell_u001"
 3, 10, "t.l0.floor_cell_u101"
 3, 12, "t.l0.floor_cell_u011"
 3, 14, "t.l0.floor_cell_u111"
 
 4, 0,  "t.l0.floor_cell_d000"
 4, 2,  "t.l0.floor_cell_d100"
 4, 4,  "t.l0.floor_cell_d010"
 4, 6,  "t.l0.floor_cell_d110"
 4, 8,  "t.l0.floor_cell_d001"
 4, 10, "t.l0.floor_cell_d101"
 4, 12, "t.l0.floor_cell_d011"
 4, 14, "t.l0.floor_cell_d111"

 5, 0,  "t.l0.floor_cell_l000"
 5, 2,  "t.l0.floor_cell_l100"
 5, 4,  "t.l0.floor_cell_l010"
 5, 6,  "t.l0.floor_cell_l110"
 5, 8,  "t.l0.floor_cell_l001"
 5, 10, "t.l0.floor_cell_l101"
 5, 12, "t.l0.floor_cell_l011"
 5, 14, "t.l0.floor_cell_l111"

 5, 1,  "t.l0.floor_cell_r000"
 5, 3,  "t.l0.floor_cell_r100"
 5, 5,  "t.l0.floor_cell_r010"
 5, 7,  "t.l0.floor_cell_r110"
 5, 9,  "t.l0.floor_cell_r001"
 5, 11, "t.l0.floor_cell_r101"
 5, 13, "t.l0.floor_cell_r011"
 5, 15, "t.l0.floor_cell_r111"

; ocean trench fallback to floor tiles
 3, 0,  "t.l0.trench_cell_u000"
 3, 2,  "t.l0.trench_cell_u100"
 3, 4,  "t.l0.trench_cell_u010"
 3, 6,  "t.l0.trench_cell_u110"
 3, 8,  "t.l0.trench_cell_u001"
 3, 10, "t.l0.trench_cell_u101"
 3, 12, "t.l0.trench_cell_u011"
 3, 14, "t.l0.trench_cell_u111"
 
 4, 0,  "t.l0.trench_cell_d000"
 4, 2,  "t.l0.trench_cell_d100"
 4, 4,  "t.l0.trench_cell_d010"
 4, 6,  "t.l0.trench_cell_d110"
 4, 8,  "t.l0.trench_cell_d001"
 4, 10, "t.l0.trench_cell_d101"
 4, 12, "t.l0.trench_cell_d011"
 4, 14, "t.l0.trench_cell_d111"

 5, 0,  "t.l0.trench_cell_l000"
 5, 2,  "t.l0.trench_cell_l100"
 5, 4,  "t.l0.trench_cell_l010"
 5, 6,  "t.l0.trench_cell_l110"
 5, 8,  "t.l0.trench_cell_l001"
 5, 10, "t.l0.trench_cell_l101"
 5, 12, "t.l0.trench_cell_l011"
 5, 14, "t.l0.trench_cell_l111"

 5, 1,  "t.l0.trench_cell_r000"
 5, 3,  "t.l0.trench_cell_r100"
 5, 5,  "t.l0.trench_cell_r010"
 5, 7,  "t.l0.trench_cell_r110"
 5, 9,  "t.l0.trench_cell_r001"
 5, 11, "t.l0.trench_cell_r101"
 5, 13, "t.l0.trench_cell_r011"
 5, 15, "t.l0.trench_cell_r111"

; ocean ridge fallback to floor tiles
 3, 0,  "t.l0.ridge_cell_u000"
 3, 2,  "t.l0.ridge_cell_u100"
 3, 4,  "t.l0.ridge_cell_u010"
 3, 6,  "t.l0.ridge_cell_u110"
 3, 8,  "t.l0.ridge_cell_u001"
 3, 10, "t.l0.ridge_cell_u101"
 3, 12, "t.l0.ridge_cell_u011"
 3, 14, "t.l0.ridge_cell_u111"
 
 4, 0,  "t.l0.ridge_cell_d000"
 4, 2,  "t.l0.ridge_cell_d100"
 4, 4,  "t.l0.ridge_cell_d010"
 4, 6,  "t.l0.ridge_cell_d110"
 4, 8,  "t.l0.ridge_cell_d001"
 4, 10, "t.l0.ridge_cell_d101"
 4, 12, "t.l0.ridge_cell_d011"
 4, 14, "t.l0.ridge_cell_d111"

 5, 0,  "t.l0.ridge_cell_l000"
 5, 2,  "t.l0.ridge_cell_l100"
 5, 4,  "t.l0.ridge_cell_l010"
 5, 6,  "t.l0.ridge_cell_l110"
 5, 8,  "t.l0.ridge_cell_l001"
 5, 10, "t.l0.ridge_cell_l101"
 5, 12, "t.l0.ridge_cell_l011"
 5, 14, "t.l0.ridge_cell_l111"

 5, 1,  "t.l0.ridge_cell_r000"
 5, 3,  "t.l0.ridge_cell_r100"
 5, 5,  "t.l0.ridge_cell_r010"
 5, 7,  "t.l0.ridge_cell_r110"
 5, 9,  "t.l0.ridge_cell_r001"
 5, 11, "t.l0.ridge_cell_r101"
 5, 13, "t.l0.ridge_cell_r011"
 5, 15, "t.l0.ridge_cell_r111"

; ocean vent fallback to floor tiles
 3, 0,  "t.l0.vent_cell_u000"
 3, 2,  "t.l0.vent_cell_u100"
 3, 4,  "t.l0.vent_cell_u010"
 3, 6,  "t.l0.vent_cell_u110"
 3, 8,  "t.l0.vent_cell_u001"
 3, 10, "t.l0.vent_cell_u101"
 3, 12, "t.l0.vent_cell_u011"
 3, 14, "t.l0.vent_cell_u111"
 
 4, 0,  "t.l0.vent_cell_d000"
 4, 2,  "t.l0.vent_cell_d100"
 4, 4,  "t.l0.vent_cell_d010"
 4, 6,  "t.l0.vent_cell_d110"
 4, 8,  "t.l0.vent_cell_d001"
 4, 10, "t.l0.vent_cell_d101"
 4, 12, "t.l0.vent_cell_d011"
 4, 14, "t.l0.vent_cell_d111"

 5, 0,  "t.l0.vent_cell_l000"
 5, 2,  "t.l0.vent_cell_l100"
 5, 4,  "t.l0.vent_cell_l010"
 5, 6,  "t.l0.vent_cell_l110"
 5, 8,  "t.l0.vent_cell_l001"
 5, 10, "t.l0.vent_cell_l101"
 5, 12, "t.l0.vent_cell_l011"
 5, 14, "t.l0.vent_cell_l111"

 5, 1,  "t.l0.vent_cell_r000"
 5, 3,  "t.l0.vent_cell_r100"
 5, 5,  "t.l0.vent_cell_r010"
 5, 7,  "t.l0.vent_cell_r110"
 5, 9,  "t.l0.vent_cell_r001"
 5, 11, "t.l0.vent_cell_r101"
 5, 13, "t.l0.vent_cell_r011"
 5, 15, "t.l0.vent_cell_r111"

}
