
[spec]

; Format and options of this spec file:
options = "+spec3"

[info]

artists = "
    ???
"

[file]
gfx = "misc/cursors"

[grid_main]

x_top_left = 0
y_top_left = 0
dx = 27
dy = 27
pixel_border=0

tiles = { "row", "column", "tag", "hot_x", "hot_y"
	0, 0, "cursor.goto", 25, 1
	0, 1, "cursor.patrol", 13, 13
	0, 2, "cursor.paradrop", 13, 25
	0, 3, "cursor.nuke", 13, 13
}
