
[spec]

; Format and options of this spec file:
options = "+Freeciv-spec-Devel-2013.Feb.13"

[info]

artists = "
	 Tim F. Smith <yoohootim@hotmail.com>
	 Daniel Speyer <dspeyer@users.sf.net> (mix)
	 Frederic Rodrigo <f.rodrigo@tuxfamily.org> (mix)
	 GriffonSpade
"

[file]
gfx = "hexemplio/terrain3"

[grid_main]

x_top_left = 1
y_top_left = 1
dx = 128
dy = 64
pixel_border = 1

tiles = { "row", "column","tag"

;jungles as overlay
;Center

 0,  0, "t.l2.jungle_n0e0se0s0w0nw0"
 0,  0, "t.l2.jungle_n0e1se0s0w0nw0"
 0,  0, "t.l2.jungle_n0e0se0s1w0nw0"
 0,  0, "t.l2.jungle_n0e1se0s1w0nw0"
 0,  0, "t.l2.jungle_n0e0se0s0w0nw1"
 0,  0, "t.l2.jungle_n0e1se0s0w0nw1"
 0,  0, "t.l2.jungle_n0e0se0s1w0nw1"
 0,  0, "t.l2.jungle_n0e1se0s1w0nw1"
 0,  0, "t.l2.jungle_n0e0se1s0w0nw0"
 0,  0, "t.l2.jungle_n0e1se1s0w0nw0"
 0,  0, "t.l2.jungle_n0e0se1s1w0nw0"
 0,  0, "t.l2.jungle_n0e1se1s1w0nw0"
 0,  0, "t.l2.jungle_n0e0se1s0w0nw1"
 0,  0, "t.l2.jungle_n0e1se1s0w0nw1"
 0,  0, "t.l2.jungle_n0e0se1s1w0nw1"
 0,  0, "t.l2.jungle_n0e1se1s1w0nw1"




;North
 0,  1, "t.l2.jungle_n1e0se0s0w0nw0"
 0,  1, "t.l2.jungle_n1e1se0s0w0nw0"
 0,  1, "t.l2.jungle_n1e0se0s1w0nw0"
 0,  1, "t.l2.jungle_n1e1se0s1w0nw0"
 0,  1, "t.l2.jungle_n1e0se0s0w0nw1"
 0,  1, "t.l2.jungle_n1e1se0s0w0nw1"
 0,  1, "t.l2.jungle_n1e0se0s1w0nw1"
 0,  1, "t.l2.jungle_n1e1se0s1w0nw1"
 0,  1, "t.l2.jungle_n1e0se1s0w0nw0"
 0,  1, "t.l2.jungle_n1e1se1s0w0nw0"
 0,  1, "t.l2.jungle_n1e0se1s1w0nw0"
 0,  1, "t.l2.jungle_n1e1se1s1w0nw0"
 0,  1, "t.l2.jungle_n1e0se1s0w0nw1"
 0,  1, "t.l2.jungle_n1e1se1s0w0nw1"
 0,  1, "t.l2.jungle_n1e0se1s1w0nw1"
 0,  1, "t.l2.jungle_n1e1se1s1w0nw1"


;West
 0,  2, "t.l2.jungle_n0e0se0s0w1nw0"
 0,  2, "t.l2.jungle_n0e1se0s0w1nw0"
 0,  2, "t.l2.jungle_n0e0se0s1w1nw0"
 0,  2, "t.l2.jungle_n0e1se0s1w1nw0"
 0,  2, "t.l2.jungle_n0e0se0s0w1nw1"
 0,  2, "t.l2.jungle_n0e1se0s0w1nw1"
 0,  2, "t.l2.jungle_n0e0se0s1w1nw1"
 0,  2, "t.l2.jungle_n0e1se0s1w1nw1"
 0,  2, "t.l2.jungle_n0e0se1s0w1nw0"
 0,  2, "t.l2.jungle_n0e1se1s0w1nw0"
 0,  2, "t.l2.jungle_n0e0se1s1w1nw0"
 0,  2, "t.l2.jungle_n0e1se1s1w1nw0"
 0,  2, "t.l2.jungle_n0e0se1s0w1nw1"
 0,  2, "t.l2.jungle_n0e1se1s0w1nw1"
 0,  2, "t.l2.jungle_n0e0se1s1w1nw1"
 0,  2, "t.l2.jungle_n0e1se1s1w1nw1"


;NorthWest
 0,  3, "t.l2.jungle_n1e0se0s0w1nw0"
 0,  3, "t.l2.jungle_n1e1se0s0w1nw0"
 0,  3, "t.l2.jungle_n1e0se0s1w1nw0"
 0,  3, "t.l2.jungle_n1e1se0s1w1nw0"
 0,  3, "t.l2.jungle_n1e0se0s0w1nw1"
 0,  3, "t.l2.jungle_n1e1se0s0w1nw1"
 0,  3, "t.l2.jungle_n1e0se0s1w1nw1"
 0,  3, "t.l2.jungle_n1e1se0s1w1nw1"
 0,  3, "t.l2.jungle_n1e0se1s0w1nw0"
 0,  3, "t.l2.jungle_n1e1se1s0w1nw0"
 0,  3, "t.l2.jungle_n1e0se1s1w1nw0"
 0,  3, "t.l2.jungle_n1e1se1s1w1nw0"
 0,  3, "t.l2.jungle_n1e0se1s0w1nw1"
 0,  3, "t.l2.jungle_n1e1se1s0w1nw1"
 0,  3, "t.l2.jungle_n1e0se1s1w1nw1"
 0,  3, "t.l2.jungle_n1e1se1s1w1nw1"

;forests as overlay

;Center
 1,  0, "t.l2.forest_n0e0se0s0w0nw0"
 1,  0, "t.l2.forest_n0e1se0s0w0nw0"
 1,  0, "t.l2.forest_n0e0se0s1w0nw0"
 1,  0, "t.l2.forest_n0e1se0s1w0nw0"
 1,  0, "t.l2.forest_n0e0se0s0w0nw1"
 1,  0, "t.l2.forest_n0e1se0s0w0nw1"
 1,  0, "t.l2.forest_n0e0se0s1w0nw1"
 1,  0, "t.l2.forest_n0e1se0s1w0nw1"
 1,  0, "t.l2.forest_n0e0se1s0w0nw0"
 1,  0, "t.l2.forest_n0e1se1s0w0nw0"
 1,  0, "t.l2.forest_n0e0se1s1w0nw0"
 1,  0, "t.l2.forest_n0e1se1s1w0nw0"
 1,  0, "t.l2.forest_n0e0se1s0w0nw1"
 1,  0, "t.l2.forest_n0e1se1s0w0nw1"
 1,  0, "t.l2.forest_n0e0se1s1w0nw1"
 1,  0, "t.l2.forest_n0e1se1s1w0nw1"


;North
 1,  1, "t.l2.forest_n1e0se0s0w0nw0"
 1,  1, "t.l2.forest_n1e1se0s0w0nw0"
 1,  1, "t.l2.forest_n1e0se0s1w0nw0"
 1,  1, "t.l2.forest_n1e1se0s1w0nw0"
 1,  1, "t.l2.forest_n1e0se0s0w0nw1"
 1,  1, "t.l2.forest_n1e1se0s0w0nw1"
 1,  1, "t.l2.forest_n1e0se0s1w0nw1"
 1,  1, "t.l2.forest_n1e1se0s1w0nw1"
 1,  1, "t.l2.forest_n1e0se1s0w0nw0"
 1,  1, "t.l2.forest_n1e1se1s0w0nw0"
 1,  1, "t.l2.forest_n1e0se1s1w0nw0"
 1,  1, "t.l2.forest_n1e1se1s1w0nw0"
 1,  1, "t.l2.forest_n1e0se1s0w0nw1"
 1,  1, "t.l2.forest_n1e1se1s0w0nw1"
 1,  1, "t.l2.forest_n1e0se1s1w0nw1"
 1,  1, "t.l2.forest_n1e1se1s1w0nw1"

;West
 1,  2, "t.l2.forest_n0e0se0s0w1nw0"
 1,  2, "t.l2.forest_n0e1se0s0w1nw0"
 1,  2, "t.l2.forest_n0e0se0s1w1nw0"
 1,  2, "t.l2.forest_n0e1se0s1w1nw0"
 1,  2, "t.l2.forest_n0e0se0s0w1nw1"
 1,  2, "t.l2.forest_n0e1se0s0w1nw1"
 1,  2, "t.l2.forest_n0e0se0s1w1nw1"
 1,  2, "t.l2.forest_n0e1se0s1w1nw1"
 1,  2, "t.l2.forest_n0e0se1s0w1nw0"
 1,  2, "t.l2.forest_n0e1se1s0w1nw0"
 1,  2, "t.l2.forest_n0e0se1s1w1nw0"
 1,  2, "t.l2.forest_n0e1se1s1w1nw0"
 1,  2, "t.l2.forest_n0e0se1s0w1nw1"
 1,  2, "t.l2.forest_n0e1se1s0w1nw1"
 1,  2, "t.l2.forest_n0e0se1s1w1nw1"
 1,  2, "t.l2.forest_n0e1se1s1w1nw1"



;NorthWest
 1,  3, "t.l2.forest_n1e0se0s0w1nw0"
 1,  3, "t.l2.forest_n1e1se0s0w1nw0"
 1,  3, "t.l2.forest_n1e0se0s1w1nw0"
 1,  3, "t.l2.forest_n1e1se0s1w1nw0"
 1,  3, "t.l2.forest_n1e0se0s0w1nw1"
 1,  3, "t.l2.forest_n1e1se0s0w1nw1"
 1,  3, "t.l2.forest_n1e0se0s1w1nw1"
 1,  3, "t.l2.forest_n1e1se0s1w1nw1"
 1,  3, "t.l2.forest_n1e0se1s0w1nw0"
 1,  3, "t.l2.forest_n1e1se1s0w1nw0"
 1,  3, "t.l2.forest_n1e0se1s1w1nw0"
 1,  3, "t.l2.forest_n1e1se1s1w1nw0"
 1,  3, "t.l2.forest_n1e0se1s0w1nw1"
 1,  3, "t.l2.forest_n1e1se1s0w1nw1"
 1,  3, "t.l2.forest_n1e0se1s1w1nw1"
 1,  3, "t.l2.forest_n1e1se1s1w1nw1"

;taigas as overlay
;Center

 2,  0, "t.l2.taiga_n0e0se0s0w0nw0"
 2,  0, "t.l2.taiga_n0e1se0s0w0nw0"
 2,  0, "t.l2.taiga_n0e0se0s1w0nw0"
 2,  0, "t.l2.taiga_n0e1se0s1w0nw0"
 2,  0, "t.l2.taiga_n0e0se0s0w0nw1"
 2,  0, "t.l2.taiga_n0e1se0s0w0nw1"
 2,  0, "t.l2.taiga_n0e0se0s1w0nw1"
 2,  0, "t.l2.taiga_n0e1se0s1w0nw1"
 2,  0, "t.l2.taiga_n0e0se1s0w0nw0"
 2,  0, "t.l2.taiga_n0e1se1s0w0nw0"
 2,  0, "t.l2.taiga_n0e0se1s1w0nw0"
 2,  0, "t.l2.taiga_n0e1se1s1w0nw0"
 2,  0, "t.l2.taiga_n0e0se1s0w0nw1"
 2,  0, "t.l2.taiga_n0e1se1s0w0nw1"
 2,  0, "t.l2.taiga_n0e0se1s1w0nw1"
 2,  0, "t.l2.taiga_n0e1se1s1w0nw1"




;North
 2,  1, "t.l2.taiga_n1e0se0s0w0nw0"
 2,  1, "t.l2.taiga_n1e1se0s0w0nw0"
 2,  1, "t.l2.taiga_n1e0se0s1w0nw0"
 2,  1, "t.l2.taiga_n1e1se0s1w0nw0"
 2,  1, "t.l2.taiga_n1e0se0s0w0nw1"
 2,  1, "t.l2.taiga_n1e1se0s0w0nw1"
 2,  1, "t.l2.taiga_n1e0se0s1w0nw1"
 2,  1, "t.l2.taiga_n1e1se0s1w0nw1"
 2,  1, "t.l2.taiga_n1e0se1s0w0nw0"
 2,  1, "t.l2.taiga_n1e1se1s0w0nw0"
 2,  1, "t.l2.taiga_n1e0se1s1w0nw0"
 2,  1, "t.l2.taiga_n1e1se1s1w0nw0"
 2,  1, "t.l2.taiga_n1e0se1s0w0nw1"
 2,  1, "t.l2.taiga_n1e1se1s0w0nw1"
 2,  1, "t.l2.taiga_n1e0se1s1w0nw1"
 2,  1, "t.l2.taiga_n1e1se1s1w0nw1"


;West
 2,  2, "t.l2.taiga_n0e0se0s0w1nw0"
 2,  2, "t.l2.taiga_n0e1se0s0w1nw0"
 2,  2, "t.l2.taiga_n0e0se0s1w1nw0"
 2,  2, "t.l2.taiga_n0e1se0s1w1nw0"
 2,  2, "t.l2.taiga_n0e0se0s0w1nw1"
 2,  2, "t.l2.taiga_n0e1se0s0w1nw1"
 2,  2, "t.l2.taiga_n0e0se0s1w1nw1"
 2,  2, "t.l2.taiga_n0e1se0s1w1nw1"
 2,  2, "t.l2.taiga_n0e0se1s0w1nw0"
 2,  2, "t.l2.taiga_n0e1se1s0w1nw0"
 2,  2, "t.l2.taiga_n0e0se1s1w1nw0"
 2,  2, "t.l2.taiga_n0e1se1s1w1nw0"
 2,  2, "t.l2.taiga_n0e0se1s0w1nw1"
 2,  2, "t.l2.taiga_n0e1se1s0w1nw1"
 2,  2, "t.l2.taiga_n0e0se1s1w1nw1"
 2,  2, "t.l2.taiga_n0e1se1s1w1nw1"


;NorthWest
 2,  3, "t.l2.taiga_n1e0se0s0w1nw0"
 2,  3, "t.l2.taiga_n1e1se0s0w1nw0"
 2,  3, "t.l2.taiga_n1e0se0s1w1nw0"
 2,  3, "t.l2.taiga_n1e1se0s1w1nw0"
 2,  3, "t.l2.taiga_n1e0se0s0w1nw1"
 2,  3, "t.l2.taiga_n1e1se0s0w1nw1"
 2,  3, "t.l2.taiga_n1e0se0s1w1nw1"
 2,  3, "t.l2.taiga_n1e1se0s1w1nw1"
 2,  3, "t.l2.taiga_n1e0se1s0w1nw0"
 2,  3, "t.l2.taiga_n1e1se1s0w1nw0"
 2,  3, "t.l2.taiga_n1e0se1s1w1nw0"
 2,  3, "t.l2.taiga_n1e1se1s1w1nw0"
 2,  3, "t.l2.taiga_n1e0se1s0w1nw1"
 2,  3, "t.l2.taiga_n1e1se1s0w1nw1"
 2,  3, "t.l2.taiga_n1e0se1s1w1nw1"
 2,  3, "t.l2.taiga_n1e1se1s1w1nw1"

}
