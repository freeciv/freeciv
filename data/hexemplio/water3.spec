
[spec]

; Format and options of this spec file:
options = "+Freeciv-spec-Devel-2015-Mar-25"

[info]

artists = "
    Tim F. Smith <yoohootim@hotmail.com>
    Daniel Speyer <dspeyer@users.sf.net> (mix)
    Frederic Rodrigo <f.rodrigo@tuxfamily.org> (mix)
    Andreas Røsdal <andrearo@pvv.ntnu.no> (hex mode)
"

[file]
gfx = "hexemplio/water3"

[grid_main]

x_top_left = 1
y_top_left = 1
dx = 63
dy = 41
pixel_border = 1

tiles = { "row", "column","tag"

; Floor cell sprites
 0, 0,  "t.l0.floor_cell_l000"
 0, 0,  "t.l0.floor_cell_l010"
 0, 1,  "t.l0.floor_cell_l100"
 0, 1,  "t.l0.floor_cell_l110"
 0, 2,  "t.l0.floor_cell_l001"
 0, 2,  "t.l0.floor_cell_l011"
 0, 3,  "t.l0.floor_cell_l101"
 0, 3,  "t.l0.floor_cell_l111"

 1, 0,  "t.l0.floor_cell_r000"
 1, 0,  "t.l0.floor_cell_r010"
 1, 1,  "t.l0.floor_cell_r100"
 1, 1,  "t.l0.floor_cell_r110"
 1, 2,  "t.l0.floor_cell_r001"
 1, 2,  "t.l0.floor_cell_r011"
 1, 3,  "t.l0.floor_cell_r101"
 1, 3,  "t.l0.floor_cell_r111"

; Lake cell sprites
2, 0,  "t.l0.lake_cell_l000"
2, 0,  "t.l0.lake_cell_l010"
2, 1,  "t.l0.lake_cell_l100"
2, 1,  "t.l0.lake_cell_l110"
2, 2,  "t.l0.lake_cell_l001"
2, 2,  "t.l0.lake_cell_l011"
2, 3,  "t.l0.lake_cell_l101"
2, 3,  "t.l0.lake_cell_l111"

3, 0,  "t.l0.lake_cell_r000"
3, 0,  "t.l0.lake_cell_r010"
3, 1,  "t.l0.lake_cell_r100"
3, 1,  "t.l0.lake_cell_r110"
3, 2,  "t.l0.lake_cell_r001"
3, 2,  "t.l0.lake_cell_r011"
3, 3,  "t.l0.lake_cell_r101"
3, 3,  "t.l0.lake_cell_r111"

; coast cell sprites.  See doc/README.graphics
4, 0,  "t.l1.coast_cell_l000"
4, 0,  "t.l1.coast_cell_l010"
4, 1,  "t.l1.coast_cell_l100"
4, 1,  "t.l1.coast_cell_l110"
4, 2,  "t.l1.coast_cell_l001"
4, 2,  "t.l1.coast_cell_l011"
4, 3,  "t.l1.coast_cell_l101"
4, 3,  "t.l1.coast_cell_l111"

5, 0,  "t.l1.coast_cell_r000"
5, 0,  "t.l1.coast_cell_r010"
5, 1,  "t.l1.coast_cell_r100"
5, 1,  "t.l1.coast_cell_r110"
5, 2,  "t.l1.coast_cell_r001"
5, 2,  "t.l1.coast_cell_r011"
5, 3,  "t.l1.coast_cell_r101"
5, 3,  "t.l1.coast_cell_r111"

; Deep Ocean cell sprites
6, 0,  "t.l1.floor_cell_l000"
6, 0,  "t.l1.floor_cell_l010"
6, 1,  "t.l1.floor_cell_l100"
6, 1,  "t.l1.floor_cell_l110"
6, 2,  "t.l1.floor_cell_l001"
6, 2,  "t.l1.floor_cell_l011"
6, 3,  "t.l1.floor_cell_l101"
6, 3,  "t.l1.floor_cell_l111"

7, 0,  "t.l1.floor_cell_r000"
7, 0,  "t.l1.floor_cell_r010"
7, 1,  "t.l1.floor_cell_r100"
7, 1,  "t.l1.floor_cell_r110"
7, 2,  "t.l1.floor_cell_r001"
7, 2,  "t.l1.floor_cell_r011"
7, 3,  "t.l1.floor_cell_r101"
7, 3,  "t.l1.floor_cell_r111"

; Lake cell sprites
 8, 0,  "t.l1.lake_cell_l000"
 8, 0,  "t.l1.lake_cell_l010"
 8, 1,  "t.l1.lake_cell_l100"
 8, 1,  "t.l1.lake_cell_l110"
 8, 2,  "t.l1.lake_cell_l001"
 8, 2,  "t.l1.lake_cell_l011"
 8, 3,  "t.l1.lake_cell_l101"
 8, 3,  "t.l1.lake_cell_l111"

 9, 0,  "t.l1.lake_cell_r000"
 9, 0,  "t.l1.lake_cell_r010"
 9, 1,  "t.l1.lake_cell_r100"
 9, 1,  "t.l1.lake_cell_r110"
 9, 2,  "t.l1.lake_cell_r001"
 9, 2,  "t.l1.lake_cell_r011"
 9, 3,  "t.l1.lake_cell_r101"
 9, 3,  "t.l1.lake_cell_r111"

}
