;
; The names for city tiles are not free and must follow the following rules.
; The names consists of style name, _ , size. The style name is as specified
; in cities.ruleset file. The size indicates which size city must
; have to be drawn with a tile. E.g. european_4 means that the tile is to be
; used for cities of size 4+ in european style. Obviously the first tile
; must be style_name_0. The sizes must be in ascending order.
; There must also be a style_name_wall tile used to draw the wall and
; an occupied tile to indicate a miltary units in a city.
; The maximum size supported now is 31, but there can only be MAX_CITY_TILES
; normal tiles. The constant is defined in common/city.h and set to 8 now.
;

[spec]

; Format and options of this spec file:
options = "+spec2"

[info]

artists = "
    Tim F. Smith <yoohootim@hotmail.com>
"

[file]
gfx = "hires/cities"

[grid_main]

x_top_left = 1
y_top_left = 39
dx = 64
dy = 48
is_pixel_border = 1

tiles = { "row", "column", "tag"

; default tiles

 1,  2, "cd.city"
; 1,  3, "cd.city_wall"
; 1,  4, "cd.occupied"

; used by all city styles

; 0,  0, "city.disorder"

;
; city tiles
;

 1,  0, "city.classical_0"
 1,  1, "city.classical_5"
 1,  2, "city.classical_10"
 1,  3, "city.classical_15"

 3,  0, "city.european_0"
 3,  1, "city.european_5"
 3,  2, "city.european_10"
 3,  3, "city.european_15"

 4,  0, "city.industrial_0"
 4,  1, "city.industrial_5"
 4,  2, "city.industrial_10"
 4,  3, "city.industrial_15"

 5,  0, "city.modern_0"
 5,  1, "city.modern_5"
 5,  2, "city.modern_10"
 5,  2, "city.modern_15"

;we just use modern here too
 5,  0, "city.postmodern_0"
 5,  1, "city.postmodern_5"
 5,  2, "city.postmodern_10"
 5,  3, "city.postmodern_15"

}

[grid_specials]

x_top_left = 143
y_top_left = 423
dx = 64
dy = 48
is_pixel_border = 1

tiles = { "row", "column", "tag"

 0, 0, "unit.fortified"
 0, 1, "tx.fortress"
 0, 2, "tx.airbase"
 0, 3, "tx.airbase_full"
}

[grid_flags]

x_top_left = 1
y_top_left = 425
dx = 14
dy = 22
is_pixel_border = 1

tiles = { "row", "column", "tag"

 0, 6, "city.european_occupied", "city.classical_occupied",
       "city.industrial_occupied", "city.modern_occupied",
       "city.postmodern_occupied"
}

[grid_walls]

x_top_left = 334
y_top_left = 39
dx = 64
dy = 48
is_pixel_border = 1

tiles = { "row", "column", "tag"

; city tiles

 1,  0, "city.classical_0_wall"
 1,  1, "city.classical_5_wall"
 1,  2, "city.classical_10_wall"
 1,  3, "city.classical_15_wall"

 3,  0, "city.european_0_wall"
 3,  1, "city.european_5_wall"
 3,  2, "city.european_10_wall"
 3,  3, "city.european_15_wall"

 4,  0, "city.industrial_0_wall"
 4,  1, "city.industrial_5_wall"
 4,  2, "city.industrial_10_wall"
 4,  3, "city.industrial_15_wall"

 5,  0, "city.modern_0_wall"
 5,  1, "city.modern_5_wall"
 5,  2, "city.modern_10_wall"
 5,  2, "city.modern_15_wall"

;we just use modern here too
 5,  0, "city.postmodern_0_wall"
 5,  1, "city.postmodern_5_wall"
 5,  2, "city.postmodern_10_wall"
 5,  3, "city.postmodern_15_wall"

}
