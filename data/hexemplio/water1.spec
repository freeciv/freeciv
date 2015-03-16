
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
gfx = "hexemplio/water1"

[grid_main]

x_top_left = 1
y_top_left = 1
dx = 63
dy = 16
pixel_border = 1

tiles = { "row", "column","tag"

; Floor cell sprites
 0, 0,  "t.l0.floor_cell_u000"
 0, 1,  "t.l0.floor_cell_u100"
 0, 2,  "t.l0.floor_cell_u010"
 0, 3,  "t.l0.floor_cell_u110"
 0, 4,  "t.l0.floor_cell_u001"
 0, 5,  "t.l0.floor_cell_u101"
 0, 6,  "t.l0.floor_cell_u011"
 0, 7,  "t.l0.floor_cell_u111"

; Lake cell sprites
 1, 0,  "t.l0.lake_cell_u000"
 1, 1,  "t.l0.lake_cell_u100"
 1, 2,  "t.l0.lake_cell_u010"
 1, 3,  "t.l0.lake_cell_u110"
 1, 4,  "t.l0.lake_cell_u001"
 1, 5,  "t.l0.lake_cell_u101"
 1, 6,  "t.l0.lake_cell_u011"
 1, 7,  "t.l0.lake_cell_u111"

; Coast cell sprites.  See doc/README.graphics
 2, 0,  "t.l1.coast_cell_u000"
 2, 1,  "t.l1.coast_cell_u100"
 2, 2,  "t.l1.coast_cell_u010"
 2, 3,  "t.l1.coast_cell_u110"
 2, 4,  "t.l1.coast_cell_u001"
 2, 5,  "t.l1.coast_cell_u101"
 2, 6,  "t.l1.coast_cell_u011"
 2, 7,  "t.l1.coast_cell_u111"

; Deep Ocean cell sprites
 3, 0,  "t.l1.floor_cell_u000"
 3, 1,  "t.l1.floor_cell_u100"
 3, 2,  "t.l1.floor_cell_u010"
 3, 3,  "t.l1.floor_cell_u110"
 3, 4,  "t.l1.floor_cell_u001"
 3, 5,  "t.l1.floor_cell_u101"
 3, 6,  "t.l1.floor_cell_u011"
 3, 7,  "t.l1.floor_cell_u111"

; Lake cell sprites
 4, 0,  "t.l1.lake_cell_u000"
 4, 1,  "t.l1.lake_cell_u100"
 4, 2,  "t.l1.lake_cell_u010"
 4, 3,  "t.l1.lake_cell_u110"
 4, 4,  "t.l1.lake_cell_u001"
 4, 5,  "t.l1.lake_cell_u101"
 4, 6,  "t.l1.lake_cell_u011"
 4, 7,  "t.l1.lake_cell_u111"

}

