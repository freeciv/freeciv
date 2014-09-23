
[spec]

; Format and options of this spec file:
options = "+Freeciv-spec-Devel-2013.Feb.13"

[info]

artists = "
    Tim F. Smith <yoohootim@hotmail.com>
    Daniel Speyer <dspeyer@users.sf.net> (mix)
    Frederic Rodrigo <f.rodrigo@tuxfamily.org> (mix)
    Andreas Røsdal <andrearo@pvv.ntnu.no> (hex mode)
"

[file]
gfx = "hexemplio/terrain2"

[grid_main]

x_top_left = 1
y_top_left = 1
dx = 64
dy = 32
pixel_border = 1

tiles = { "row", "column","tag"



; coast cell sprites.  See doc/README.graphics
 0, 0,  "t.l0.coast_cell_u000"
 0, 1,  "t.l0.coast_cell_u100"
; 0, 2,  "t.l0.coast_cell_u010"
; 0, 3,  "t.l0.coast_cell_u110"
 0, 4,  "t.l0.coast_cell_u001"
 0, 5,  "t.l0.coast_cell_u101"
; 0, 6,  "t.l0.coast_cell_u011"
; 0, 7,  "t.l0.coast_cell_u111"
; Alternate Up river mouth sprites
 0, 8,  "t.l0.coast_cell_u010"
 0, 9,  "t.l0.coast_cell_u110"
 0, 10, "t.l0.coast_cell_u011"
 0, 11, "t.l0.coast_cell_u111"

 4, 0,  "t.l0.coast_cell_d000"
 4, 1,  "t.l0.coast_cell_d100"
; 4, 2,  "t.l0.coast_cell_d010"
; 4, 3,  "t.l0.coast_cell_d110"
 4, 4,  "t.l0.coast_cell_d001"
 4, 5, "t.l0.coast_cell_d101"
; 4, 6, "t.l0.coast_cell_d011"
; 4, 7, "t.l0.coast_cell_d111"
; Alternate Down river mouth sprites
 4, 8,  "t.l0.coast_cell_d010"
 4, 9,  "t.l0.coast_cell_d110"
 4, 10, "t.l0.coast_cell_d011"
 4, 11, "t.l0.coast_cell_d111"

 8, 0,  "t.l0.coast_cell_l000"
 8, 0,  "t.l0.coast_cell_l010"
 8, 2,  "t.l0.coast_cell_l100"
 8, 2,  "t.l0.coast_cell_l110"
 8, 4,  "t.l0.coast_cell_l001"
 8, 4,  "t.l0.coast_cell_l011"
 8, 6,  "t.l0.coast_cell_l101"
 8, 6,  "t.l0.coast_cell_l111"

 8, 1,  "t.l0.coast_cell_r000"
 8, 1,  "t.l0.coast_cell_r010"
 8, 3,  "t.l0.coast_cell_r100"
 8, 3,  "t.l0.coast_cell_r110"
 8, 5,  "t.l0.coast_cell_r001"
 8, 5,  "t.l0.coast_cell_r011"
 8, 7,  "t.l0.coast_cell_r101"
 8, 7,  "t.l0.coast_cell_r111"

; Deep Ocean cell sprites
 1, 0,  "t.l0.floor_cell_u000"
 1, 1,  "t.l0.floor_cell_u100"
; 1, 2,  "t.l0.floor_cell_u010"
; 1, 3,  "t.l0.floor_cell_u110"
 1, 4,  "t.l0.floor_cell_u001"
 1, 5,  "t.l0.floor_cell_u101"
; 1, 6,  "t.l0.floor_cell_u011"
; 1, 7,  "t.l0.floor_cell_u111"
; Alternate Up river mouth sprites
 1, 8,  "t.l0.floor_cell_u010"
 1, 9,  "t.l0.floor_cell_u110"
 1, 10, "t.l0.floor_cell_u011"
 1, 11, "t.l0.floor_cell_u111"


 5, 0,  "t.l0.floor_cell_d000"
 5, 1,  "t.l0.floor_cell_d100"
; 5, 2,  "t.l0.floor_cell_d010"
; 5, 3,  "t.l0.floor_cell_d110"
 5, 4,  "t.l0.floor_cell_d001"
 5, 5,  "t.l0.floor_cell_d101"
; 5, 6,  "t.l0.floor_cell_d011"
; 5, 7,  "t.l0.floor_cell_d111"
; Alternate Down river mouth sprites
 5, 8,  "t.l0.floor_cell_d010"
 5, 9,  "t.l0.floor_cell_d110"
 5, 10, "t.l0.floor_cell_d011"
 5, 11, "t.l0.floor_cell_d111"

 9, 0,  "t.l0.floor_cell_l000"
 9, 0,  "t.l0.floor_cell_l010"
 9, 2,  "t.l0.floor_cell_l100"
 9, 2,  "t.l0.floor_cell_l110"
 9, 4,  "t.l0.floor_cell_l001"
 9, 4,  "t.l0.floor_cell_l011"
 9, 6,  "t.l0.floor_cell_l101"
 9, 6,  "t.l0.floor_cell_l111"

 9, 1,  "t.l0.floor_cell_r000"
 9, 1,  "t.l0.floor_cell_r010"
 9, 3,  "t.l0.floor_cell_r100"
 9, 3,  "t.l0.floor_cell_r110"
 9, 5,  "t.l0.floor_cell_r001"
 9, 5,  "t.l0.floor_cell_r011"
 9, 7,  "t.l0.floor_cell_r101"
 9, 7,  "t.l0.floor_cell_r111"

; Lake cell sprites
 2, 0,  "t.l0.lake_cell_u000"
 2, 1,  "t.l0.lake_cell_u100"
; 2, 2,  "t.l0.lake_cell_u010"
; 2, 3,  "t.l0.lake_cell_u110"
 2, 4,  "t.l0.lake_cell_u001"
 2, 5,  "t.l0.lake_cell_u101"
; 2, 6,  "t.l0.lake_cell_u011"
; 2, 7,  "t.l0.lake_cell_u111"
; Alternate Up river mouth sprites
 2, 8,  "t.l0.lake_cell_u010"
 2, 9,  "t.l0.lake_cell_u110"
 2, 10, "t.l0.lake_cell_u011"
 2, 11, "t.l0.lake_cell_u111"


 6, 0,  "t.l0.lake_cell_d000"
 6, 1,  "t.l0.lake_cell_d100"
; 6, 2,  "t.l0.lake_cell_d010"
; 6, 3,  "t.l0.lake_cell_d110"
 6, 4,  "t.l0.lake_cell_d001"
 6, 5,  "t.l0.lake_cell_d101"
; 6, 6,  "t.l0.lake_cell_d011"
; 6, 7,  "t.l0.lake_cell_d111"
; Alternate Down river mouth sprites
 6, 8,  "t.l0.lake_cell_d010"
 6, 9,  "t.l0.lake_cell_d110"
 6, 10, "t.l0.lake_cell_d011"
 6, 11, "t.l0.lake_cell_d111"

10, 0,  "t.l0.lake_cell_l000"
10, 0,  "t.l0.lake_cell_l010"
10, 2,  "t.l0.lake_cell_l100"
10, 2,  "t.l0.lake_cell_l110"
10, 4,  "t.l0.lake_cell_l001"
10, 4,  "t.l0.lake_cell_l011"
10, 6,  "t.l0.lake_cell_l101"
10, 6,  "t.l0.lake_cell_l111"

10, 1,  "t.l0.lake_cell_r000"
10, 1,  "t.l0.lake_cell_r010"
10, 3,  "t.l0.lake_cell_r100"
10, 3,  "t.l0.lake_cell_r110"
10, 5,  "t.l0.lake_cell_r001"
10, 5,  "t.l0.lake_cell_r011"
10, 7,  "t.l0.lake_cell_r101"
10, 7,  "t.l0.lake_cell_r111"


;Shallow Ocean ice shelf
12, 0,  "t.l1.coast_cell_u_n_n_n"
12, 1,  "t.l1.coast_cell_u_i_n_n"
12, 2,  "t.l1.coast_cell_u_n_i_n"
12, 3,  "t.l1.coast_cell_u_i_i_n"
12, 4,  "t.l1.coast_cell_u_n_n_i"
12, 5,  "t.l1.coast_cell_u_i_n_i"
12, 6,  "t.l1.coast_cell_u_n_i_i"
12, 7,  "t.l1.coast_cell_u_i_i_i"

13, 0,  "t.l1.coast_cell_d_n_n_n"
13, 1,  "t.l1.coast_cell_d_i_n_n"
13, 2,  "t.l1.coast_cell_d_n_i_n"
13, 3,  "t.l1.coast_cell_d_i_i_n"
13, 4,  "t.l1.coast_cell_d_n_n_i"
13, 5,  "t.l1.coast_cell_d_i_n_i"
13, 6,  "t.l1.coast_cell_d_n_i_i"
13, 7,  "t.l1.coast_cell_d_i_i_i"

14, 0,  "t.l1.coast_cell_l_n_n_n"
14, 0,  "t.l1.coast_cell_l_n_i_n"
14, 2,  "t.l1.coast_cell_l_i_n_n"
14, 2,  "t.l1.coast_cell_l_i_i_n"
14, 4,  "t.l1.coast_cell_l_n_n_i"
14, 4,  "t.l1.coast_cell_l_n_i_i"
14, 6,  "t.l1.coast_cell_l_i_n_i"
14, 6,  "t.l1.coast_cell_l_i_i_i"

14, 1,  "t.l1.coast_cell_r_n_n_n"
14, 1,  "t.l1.coast_cell_r_n_i_n"
14, 3,  "t.l1.coast_cell_r_i_n_n"
14, 3,  "t.l1.coast_cell_r_i_i_n"
14, 5,  "t.l1.coast_cell_r_n_n_i"
14, 5,  "t.l1.coast_cell_r_n_i_i"
14, 7,  "t.l1.coast_cell_r_i_n_i"
14, 7,  "t.l1.coast_cell_r_i_i_i"

;Deep Ocean ice shelf
12, 0,  "t.l1.floor_cell_u_n_n_n"
12, 1,  "t.l1.floor_cell_u_i_n_n"
12, 2,  "t.l1.floor_cell_u_n_i_n"
12, 3,  "t.l1.floor_cell_u_i_i_n"
12, 4,  "t.l1.floor_cell_u_n_n_i"
12, 5,  "t.l1.floor_cell_u_i_n_i"
12, 6,  "t.l1.floor_cell_u_n_i_i"
12, 7,  "t.l1.floor_cell_u_i_i_i"

13, 0,  "t.l1.floor_cell_d_n_n_n"
13, 1,  "t.l1.floor_cell_d_i_n_n"
13, 2,  "t.l1.floor_cell_d_n_i_n"
13, 3,  "t.l1.floor_cell_d_i_i_n"
13, 4,  "t.l1.floor_cell_d_n_n_i"
13, 5,  "t.l1.floor_cell_d_i_n_i"
13, 6,  "t.l1.floor_cell_d_n_i_i"
13, 7,  "t.l1.floor_cell_d_i_i_i"

14, 0,  "t.l1.floor_cell_l_n_n_n"
14, 0,  "t.l1.floor_cell_l_n_i_n"
14, 2,  "t.l1.floor_cell_l_i_n_n"
14, 2,  "t.l1.floor_cell_l_i_i_n"
14, 4,  "t.l1.floor_cell_l_n_n_i"
14, 4,  "t.l1.floor_cell_l_n_i_i"
14, 6,  "t.l1.floor_cell_l_i_n_i"
14, 6,  "t.l1.floor_cell_l_i_i_i"

14, 1,  "t.l1.floor_cell_r_n_n_n"
14, 1,  "t.l1.floor_cell_r_n_i_n"
14, 3,  "t.l1.floor_cell_r_i_n_n"
14, 3,  "t.l1.floor_cell_r_i_i_n"
14, 5,  "t.l1.floor_cell_r_n_n_i"
14, 5,  "t.l1.floor_cell_r_n_i_i"
14, 7,  "t.l1.floor_cell_r_i_n_i"
14, 7,  "t.l1.floor_cell_r_i_i_i"

;Lake ice shelf
12, 0,  "t.l1.lake_cell_u_n_n_n"
12, 1,  "t.l1.lake_cell_u_i_n_n"
12, 2,  "t.l1.lake_cell_u_n_i_n"
12, 3,  "t.l1.lake_cell_u_i_i_n"
12, 4,  "t.l1.lake_cell_u_n_n_i"
12, 5,  "t.l1.lake_cell_u_i_n_i"
12, 6,  "t.l1.lake_cell_u_n_i_i"
12, 7,  "t.l1.lake_cell_u_i_i_i"

13, 0,  "t.l1.lake_cell_d_n_n_n"
13, 1,  "t.l1.lake_cell_d_i_n_n"
13, 2,  "t.l1.lake_cell_d_n_i_n"
13, 3,  "t.l1.lake_cell_d_i_i_n"
13, 4,  "t.l1.lake_cell_d_n_n_i"
13, 5,  "t.l1.lake_cell_d_i_n_i"
13, 6,  "t.l1.lake_cell_d_n_i_i"
13, 7,  "t.l1.lake_cell_d_i_i_i"

14, 0,  "t.l1.lake_cell_l_n_n_n"
14, 0,  "t.l1.lake_cell_l_n_i_n"
14, 2,  "t.l1.lake_cell_l_i_n_n"
14, 2,  "t.l1.lake_cell_l_i_i_n"
14, 4,  "t.l1.lake_cell_l_n_n_i"
14, 4,  "t.l1.lake_cell_l_n_i_i"
14, 6,  "t.l1.lake_cell_l_i_n_i"
14, 6,  "t.l1.lake_cell_l_i_i_i"

14, 1,  "t.l1.lake_cell_r_n_n_n"
14, 1,  "t.l1.lake_cell_r_n_i_n"
14, 3,  "t.l1.lake_cell_r_i_n_n"
14, 3,  "t.l1.lake_cell_r_i_i_n"
14, 5,  "t.l1.lake_cell_r_n_n_i"
14, 5,  "t.l1.lake_cell_r_n_i_i"
14, 7,  "t.l1.lake_cell_r_i_n_i"
14, 7,  "t.l1.lake_cell_r_i_i_i"

;Shallow Ocean blended with Deep Ocean
15, 0,  "t.l2.coast_cell_u_c_c_c"
15, 1,  "t.l2.coast_cell_u_f_c_c"
15, 2,  "t.l2.coast_cell_u_c_f_c"
15, 3,  "t.l2.coast_cell_u_f_f_c"
15, 4,  "t.l2.coast_cell_u_c_c_f"
15, 5,  "t.l2.coast_cell_u_f_c_f"
15, 6,  "t.l2.coast_cell_u_c_f_f"
15, 7,  "t.l2.coast_cell_u_f_f_f"

16, 0,  "t.l2.coast_cell_d_c_c_c"
16, 1,  "t.l2.coast_cell_d_f_c_c"
16, 2,  "t.l2.coast_cell_d_c_f_c"
16, 3,  "t.l2.coast_cell_d_f_f_c"
16, 4,  "t.l2.coast_cell_d_c_c_f"
16, 5,  "t.l2.coast_cell_d_f_c_f"
16, 6,  "t.l2.coast_cell_d_c_f_f"
16, 7,  "t.l2.coast_cell_d_f_f_f"

17, 0,  "t.l2.coast_cell_l_c_c_c"
17, 0,  "t.l2.coast_cell_l_c_f_c"
17, 2,  "t.l2.coast_cell_l_f_c_c"
17, 2,  "t.l2.coast_cell_l_f_f_c"
17, 4,  "t.l2.coast_cell_l_c_c_f"
17, 4,  "t.l2.coast_cell_l_c_f_f"
17, 6,  "t.l2.coast_cell_l_f_c_f"
17, 6,  "t.l2.coast_cell_l_f_f_f"

17, 1,  "t.l2.coast_cell_r_c_c_c"
17, 1,  "t.l2.coast_cell_r_c_f_c"
17, 3,  "t.l2.coast_cell_r_f_c_c"
17, 3,  "t.l2.coast_cell_r_f_f_c"
17, 5,  "t.l2.coast_cell_r_c_c_f"
17, 5,  "t.l2.coast_cell_r_c_f_f"
17, 7,  "t.l2.coast_cell_r_f_c_f"
17, 7,  "t.l2.coast_cell_r_f_f_f"

;Deep Ocean blended with Shallow Ocean
15, 0,  "t.l2.floor_cell_u_f_f_f"
15, 1,  "t.l2.floor_cell_u_c_f_f"
15, 2,  "t.l2.floor_cell_u_f_c_f"
15, 3,  "t.l2.floor_cell_u_c_c_f"
15, 4,  "t.l2.floor_cell_u_f_f_c"
15, 5,  "t.l2.floor_cell_u_c_f_c"
15, 6,  "t.l2.floor_cell_u_f_c_c"
15, 7,  "t.l2.floor_cell_u_c_c_c"

16, 0,  "t.l2.floor_cell_d_f_f_f"
16, 1,  "t.l2.floor_cell_d_c_f_f"
16, 2,  "t.l2.floor_cell_d_f_c_f"
16, 3,  "t.l2.floor_cell_d_c_c_f"
16, 4,  "t.l2.floor_cell_d_f_f_c"
16, 5,  "t.l2.floor_cell_d_c_f_c"
16, 6,  "t.l2.floor_cell_d_f_c_c"
16, 7,  "t.l2.floor_cell_d_c_c_c"

17, 0,  "t.l2.floor_cell_l_f_f_f"
17, 0,  "t.l2.floor_cell_l_f_c_f"
17, 2,  "t.l2.floor_cell_l_c_f_f"
17, 2,  "t.l2.floor_cell_l_c_c_f"
17, 4,  "t.l2.floor_cell_l_f_f_c"
17, 4,  "t.l2.floor_cell_l_f_c_c"
17, 6,  "t.l2.floor_cell_l_c_f_c"
17, 6,  "t.l2.floor_cell_l_c_c_c"

17, 1,  "t.l2.floor_cell_r_f_f_f"
17, 1,  "t.l2.floor_cell_r_f_c_f"
17, 3,  "t.l2.floor_cell_r_c_f_f"
17, 3,  "t.l2.floor_cell_r_c_c_f"
17, 5,  "t.l2.floor_cell_r_f_f_c"
17, 5,  "t.l2.floor_cell_r_f_c_c"
17, 7,  "t.l2.floor_cell_r_c_f_c"
17, 7,  "t.l2.floor_cell_r_c_c_c"

; void cell sprites.  See doc/README.graphics
21,  0,  "t.l1.void2_cell_u000"
21,  1,  "t.l1.void2_cell_u100"
21,  2,  "t.l1.void2_cell_u010"
21,  3,  "t.l1.void2_cell_u110"
21,  4,  "t.l1.void2_cell_u001"
21,  5,  "t.l1.void2_cell_u101"
21,  6,  "t.l1.void2_cell_u011"
21,  7,  "t.l1.void2_cell_u111"

21, 0,   "t.l1.void2_cell_d000"
21, 0,   "t.l1.void2_cell_d100"
21, 0,   "t.l1.void2_cell_d010"
21, 0,   "t.l1.void2_cell_d110"
21, 0,   "t.l1.void2_cell_d001"
21, 0,   "t.l1.void2_cell_d101"
21, 0,   "t.l1.void2_cell_d011"
21, 0,   "t.l1.void2_cell_d111"

21,  0,  "t.l1.void2_cell_l000"
21,  0,  "t.l1.void2_cell_l010"
21,  0,  "t.l1.void2_cell_l100"
21,  0,  "t.l1.void2_cell_l110"
21,  8,  "t.l1.void2_cell_l001"
21,  8,  "t.l1.void2_cell_l011"
21,  8,  "t.l1.void2_cell_l101"
21,  8,  "t.l1.void2_cell_l111"

21,  0,  "t.l1.void2_cell_r000"
21,  0,  "t.l1.void2_cell_r010"
21,  9,  "t.l1.void2_cell_r100"
21,  9,  "t.l1.void2_cell_r110"
21,  0,  "t.l1.void2_cell_r001"
21,  0,  "t.l1.void2_cell_r011"
21,  9,  "t.l1.void2_cell_r101"
21,  9,  "t.l1.void2_cell_r111"


}
