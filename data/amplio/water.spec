
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

tiles = { "row", "column", "tag"

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

; ocean vent as overlay

 4,  0, "t.l1.vent_n0e0s0w0"
 4,  1, "t.l1.vent_n0e0s0w1"
 4,  2, "t.l1.vent_n0e0s1w0"
 4,  3, "t.l1.vent_n0e0s1w1"
 4,  4, "t.l1.vent_n0e1s0w0"
 4,  5, "t.l1.vent_n0e1s0w1"
 4,  6, "t.l1.vent_n0e1s1w0"
 4,  7, "t.l1.vent_n0e1s1w1"
 5,  0, "t.l1.vent_n1e0s0w0"
 5,  1, "t.l1.vent_n1e0s0w1"
 5,  2, "t.l1.vent_n1e0s1w0"
 5,  3, "t.l1.vent_n1e0s1w1"
 5,  4, "t.l1.vent_n1e1s0w0"
 5,  5, "t.l1.vent_n1e1s0w1"
 5,  6, "t.l1.vent_n1e1s1w0"
 5,  7, "t.l1.vent_n1e1s1w1"

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

tiles = { "row", "column", "tag"

; coastal ice cell sprites:
 0, 0,  "t.l1.coast_cell_u_w_w_w" ;vacant cell
 0, 2,  "t.l1.coast_cell_u_i_w_w"
 0, 4,  "t.l1.coast_cell_u_w_i_w"
 0, 6,  "t.l1.coast_cell_u_i_i_w"
 0, 8,  "t.l1.coast_cell_u_w_w_i"
 0, 10, "t.l1.coast_cell_u_i_w_i"
 0, 12, "t.l1.coast_cell_u_w_i_i"
 0, 14, "t.l1.coast_cell_u_i_i_i"
 
 1, 0,  "t.l1.coast_cell_d_w_w_w" ;vacant cell
 1, 2,  "t.l1.coast_cell_d_i_w_w"
 1, 4,  "t.l1.coast_cell_d_w_i_w"
 1, 6,  "t.l1.coast_cell_d_i_i_w"
 1, 8,  "t.l1.coast_cell_d_w_w_i"
 1, 10, "t.l1.coast_cell_d_i_w_i"
 1, 12, "t.l1.coast_cell_d_w_i_i"
 1, 14, "t.l1.coast_cell_d_i_i_i"

 2, 0,  "t.l1.coast_cell_l_w_w_w" ;vacant cell
 2, 2,  "t.l1.coast_cell_l_i_w_w"
 2, 4,  "t.l1.coast_cell_l_w_i_w"
 2, 6,  "t.l1.coast_cell_l_i_i_w"
 2, 8,  "t.l1.coast_cell_l_w_w_i"
 2, 10, "t.l1.coast_cell_l_i_w_i"
 2, 12, "t.l1.coast_cell_l_w_i_i"
 2, 14, "t.l1.coast_cell_l_i_i_i"

 2, 1,  "t.l1.coast_cell_r_w_w_w"
 2, 3,  "t.l1.coast_cell_r_i_w_w"
 2, 5,  "t.l1.coast_cell_r_w_i_w"
 2, 7,  "t.l1.coast_cell_r_i_i_w"
 2, 9,  "t.l1.coast_cell_r_w_w_i"
 2, 11, "t.l1.coast_cell_r_i_w_i"
 2, 13, "t.l1.coast_cell_r_w_i_i"
 2, 15, "t.l1.coast_cell_r_i_i_i"

; ocean shelf ice cell sprites:
 0, 0,  "t.l1.shelf_cell_u_w_w_w" ;vacant cell
 0, 2,  "t.l1.shelf_cell_u_i_w_w"
 0, 4,  "t.l1.shelf_cell_u_w_i_w"
 0, 6,  "t.l1.shelf_cell_u_i_i_w"
 0, 8,  "t.l1.shelf_cell_u_w_w_i"
 0, 10, "t.l1.shelf_cell_u_i_w_i"
 0, 12, "t.l1.shelf_cell_u_w_i_i"
 0, 14, "t.l1.shelf_cell_u_i_i_i"
 
 1, 0,  "t.l1.shelf_cell_d_w_w_w" ;vacant cell
 1, 2,  "t.l1.shelf_cell_d_i_w_w"
 1, 4,  "t.l1.shelf_cell_d_w_i_w"
 1, 6,  "t.l1.shelf_cell_d_i_i_w"
 1, 8,  "t.l1.shelf_cell_d_w_w_i"
 1, 10, "t.l1.shelf_cell_d_i_w_i"
 1, 12, "t.l1.shelf_cell_d_w_i_i"
 1, 14, "t.l1.shelf_cell_d_i_i_i"

 2, 0,  "t.l1.shelf_cell_l_w_w_w" ;vacant cell
 2, 2,  "t.l1.shelf_cell_l_i_w_w"
 2, 4,  "t.l1.shelf_cell_l_w_i_w"
 2, 6,  "t.l1.shelf_cell_l_i_i_w"
 2, 8,  "t.l1.shelf_cell_l_w_w_i"
 2, 10, "t.l1.shelf_cell_l_i_w_i"
 2, 12, "t.l1.shelf_cell_l_w_i_i"
 2, 14, "t.l1.shelf_cell_l_i_i_i"

 2, 1,  "t.l1.shelf_cell_r_w_w_w"
 2, 3,  "t.l1.shelf_cell_r_i_w_w"
 2, 5,  "t.l1.shelf_cell_r_w_i_w"
 2, 7,  "t.l1.shelf_cell_r_i_i_w"
 2, 9,  "t.l1.shelf_cell_r_w_w_i"
 2, 11, "t.l1.shelf_cell_r_i_w_i"
 2, 13, "t.l1.shelf_cell_r_w_i_i"
 2, 15, "t.l1.shelf_cell_r_i_i_i"

; ocean floor ice cell sprites:
 0, 0,  "t.l1.floor_cell_u_w_w_w" ;vacant cell
 0, 2,  "t.l1.floor_cell_u_i_w_w"
 0, 4,  "t.l1.floor_cell_u_w_i_w"
 0, 6,  "t.l1.floor_cell_u_i_i_w"
 0, 8,  "t.l1.floor_cell_u_w_w_i"
 0, 10, "t.l1.floor_cell_u_i_w_i"
 0, 12, "t.l1.floor_cell_u_w_i_i"
 0, 14, "t.l1.floor_cell_u_i_i_i"
 
 1, 0,  "t.l1.floor_cell_d_w_w_w" ;vacant cell
 1, 2,  "t.l1.floor_cell_d_i_w_w"
 1, 4,  "t.l1.floor_cell_d_w_i_w"
 1, 6,  "t.l1.floor_cell_d_i_i_w"
 1, 8,  "t.l1.floor_cell_d_w_w_i"
 1, 10, "t.l1.floor_cell_d_i_w_i"
 1, 12, "t.l1.floor_cell_d_w_i_i"
 1, 14, "t.l1.floor_cell_d_i_i_i"

 2, 0,  "t.l1.floor_cell_l_w_w_w" ;vacant cell
 2, 2,  "t.l1.floor_cell_l_i_w_w"
 2, 4,  "t.l1.floor_cell_l_w_i_w"
 2, 6,  "t.l1.floor_cell_l_i_i_w"
 2, 8,  "t.l1.floor_cell_l_w_w_i"
 2, 10, "t.l1.floor_cell_l_i_w_i"
 2, 12, "t.l1.floor_cell_l_w_i_i"
 2, 14, "t.l1.floor_cell_l_i_i_i"

 2, 1,  "t.l1.floor_cell_r_w_w_w"
 2, 3,  "t.l1.floor_cell_r_i_w_w"
 2, 5,  "t.l1.floor_cell_r_w_i_w"
 2, 7,  "t.l1.floor_cell_r_i_i_w"
 2, 9,  "t.l1.floor_cell_r_w_w_i"
 2, 11, "t.l1.floor_cell_r_i_w_i"
 2, 13, "t.l1.floor_cell_r_w_i_i"
 2, 15, "t.l1.floor_cell_r_i_i_i"

; ocean trench ice cell sprites:
 0, 0,  "t.l2.trench_cell_u_w_w_w" ;vacant cell
 0, 2,  "t.l2.trench_cell_u_i_w_w"
 0, 4,  "t.l2.trench_cell_u_w_i_w"
 0, 6,  "t.l2.trench_cell_u_i_i_w"
 0, 8,  "t.l2.trench_cell_u_w_w_i"
 0, 10, "t.l2.trench_cell_u_i_w_i"
 0, 12, "t.l2.trench_cell_u_w_i_i"
 0, 14, "t.l2.trench_cell_u_i_i_i"
 
 1, 0,  "t.l2.trench_cell_d_w_w_w" ;vacant cell
 1, 2,  "t.l2.trench_cell_d_i_w_w"
 1, 4,  "t.l2.trench_cell_d_w_i_w"
 1, 6,  "t.l2.trench_cell_d_i_i_w"
 1, 8,  "t.l2.trench_cell_d_w_w_i"
 1, 10, "t.l2.trench_cell_d_i_w_i"
 1, 12, "t.l2.trench_cell_d_w_i_i"
 1, 14, "t.l2.trench_cell_d_i_i_i"

 2, 0,  "t.l2.trench_cell_l_w_w_w" ;vacant cell
 2, 2,  "t.l2.trench_cell_l_i_w_w"
 2, 4,  "t.l2.trench_cell_l_w_i_w"
 2, 6,  "t.l2.trench_cell_l_i_i_w"
 2, 8,  "t.l2.trench_cell_l_w_w_i"
 2, 10, "t.l2.trench_cell_l_i_w_i"
 2, 12, "t.l2.trench_cell_l_w_i_i"
 2, 14, "t.l2.trench_cell_l_i_i_i"

 2, 1,  "t.l2.trench_cell_r_w_w_w"
 2, 3,  "t.l2.trench_cell_r_i_w_w"
 2, 5,  "t.l2.trench_cell_r_w_i_w"
 2, 7,  "t.l2.trench_cell_r_i_i_w"
 2, 9,  "t.l2.trench_cell_r_w_w_i"
 2, 11, "t.l2.trench_cell_r_i_w_i"
 2, 13, "t.l2.trench_cell_r_w_i_i"
 2, 15, "t.l2.trench_cell_r_i_i_i"

; ocean ridge ice cell sprites:
 0, 0,  "t.l2.ridge_cell_u_w_w_w" ;vacant cell
 0, 2,  "t.l2.ridge_cell_u_i_w_w"
 0, 4,  "t.l2.ridge_cell_u_w_i_w"
 0, 6,  "t.l2.ridge_cell_u_i_i_w"
 0, 8,  "t.l2.ridge_cell_u_w_w_i"
 0, 10, "t.l2.ridge_cell_u_i_w_i"
 0, 12, "t.l2.ridge_cell_u_w_i_i"
 0, 14, "t.l2.ridge_cell_u_i_i_i"
 
 1, 0,  "t.l2.ridge_cell_d_w_w_w" ;vacant cell
 1, 2,  "t.l2.ridge_cell_d_i_w_w"
 1, 4,  "t.l2.ridge_cell_d_w_i_w"
 1, 6,  "t.l2.ridge_cell_d_i_i_w"
 1, 8,  "t.l2.ridge_cell_d_w_w_i"
 1, 10, "t.l2.ridge_cell_d_i_w_i"
 1, 12, "t.l2.ridge_cell_d_w_i_i"
 1, 14, "t.l2.ridge_cell_d_i_i_i"

 2, 0,  "t.l2.ridge_cell_l_w_w_w" ;vacant cell
 2, 2,  "t.l2.ridge_cell_l_i_w_w"
 2, 4,  "t.l2.ridge_cell_l_w_i_w"
 2, 6,  "t.l2.ridge_cell_l_i_i_w"
 2, 8,  "t.l2.ridge_cell_l_w_w_i"
 2, 10, "t.l2.ridge_cell_l_i_w_i"
 2, 12, "t.l2.ridge_cell_l_w_i_i"
 2, 14, "t.l2.ridge_cell_l_i_i_i"

 2, 1,  "t.l2.ridge_cell_r_w_w_w"
 2, 3,  "t.l2.ridge_cell_r_i_w_w"
 2, 5,  "t.l2.ridge_cell_r_w_i_w"
 2, 7,  "t.l2.ridge_cell_r_i_i_w"
 2, 9,  "t.l2.ridge_cell_r_w_w_i"
 2, 11, "t.l2.ridge_cell_r_i_w_i"
 2, 13, "t.l2.ridge_cell_r_w_i_i"
 2, 15, "t.l2.ridge_cell_r_i_i_i"

; ocean vent ice cell sprites:
 0, 0,  "t.l2.vent_cell_u_w_w_w" ;vacant cell
 0, 2,  "t.l2.vent_cell_u_i_w_w"
 0, 4,  "t.l2.vent_cell_u_w_i_w"
 0, 6,  "t.l2.vent_cell_u_i_i_w"
 0, 8,  "t.l2.vent_cell_u_w_w_i"
 0, 10, "t.l2.vent_cell_u_i_w_i"
 0, 12, "t.l2.vent_cell_u_w_i_i"
 0, 14, "t.l2.vent_cell_u_i_i_i"
 
 1, 0,  "t.l2.vent_cell_d_w_w_w" ;vacant cell
 1, 2,  "t.l2.vent_cell_d_i_w_w"
 1, 4,  "t.l2.vent_cell_d_w_i_w"
 1, 6,  "t.l2.vent_cell_d_i_i_w"
 1, 8,  "t.l2.vent_cell_d_w_w_i"
 1, 10, "t.l2.vent_cell_d_i_w_i"
 1, 12, "t.l2.vent_cell_d_w_i_i"
 1, 14, "t.l2.vent_cell_d_i_i_i"

 2, 0,  "t.l2.vent_cell_l_w_w_w" ;vacant cell
 2, 2,  "t.l2.vent_cell_l_i_w_w"
 2, 4,  "t.l2.vent_cell_l_w_i_w"
 2, 6,  "t.l2.vent_cell_l_i_i_w"
 2, 8,  "t.l2.vent_cell_l_w_w_i"
 2, 10, "t.l2.vent_cell_l_i_w_i"
 2, 12, "t.l2.vent_cell_l_w_i_i"
 2, 14, "t.l2.vent_cell_l_i_i_i"

 2, 1,  "t.l2.vent_cell_r_w_w_w"
 2, 3,  "t.l2.vent_cell_r_i_w_w"
 2, 5,  "t.l2.vent_cell_r_w_i_w"
 2, 7,  "t.l2.vent_cell_r_i_i_w"
 2, 9,  "t.l2.vent_cell_r_w_w_i"
 2, 11, "t.l2.vent_cell_r_i_w_i"
 2, 13, "t.l2.vent_cell_r_w_i_i"
 2, 15, "t.l2.vent_cell_r_i_i_i"

}
