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
    Jerzy Klek <jekl@altavista.net>

    european style based on default tileset by
    Ralph Engels <rengels@hydra.informatik.uni-ulm.de>
"

[file]
gfx = "engels/cities"

[grid_main]

x_top_left = 0
y_top_left = 0
dx = 45
dy = 45

tiles = { "row", "column", "tag"

; default tiles

 1,  2, "cd.city"
 1,  3, "cd.city_wall"
 1,  4, "cd.occupied"

; used by all city styles

 0,  0, "city.disorder"

;
; city tiles
;

 1,  0, "city.european_0"
 1,  1, "city.european_5"
 1,  2, "city.european_10"
 1,  3, "city.european_wall"
 1,  4, "city.european_occupied"

 1,  0, "city.classical_0"
 1,  1, "city.classical_5"
 1,  2, "city.classical_10"
 1,  3, "city.classical_wall"
 1,  4, "city.classical_occupied"

 1,  0, "city.industrial_0"
 1,  1, "city.industrial_5"
 1,  2, "city.industrial_10"
 1,  3, "city.industrial_wall"
 1,  4, "city.industrial_occupied"

 1,  0, "city.modern_0"
 1,  1, "city.modern_5"
 1,  2, "city.modern_10"
 1,  3, "city.modern_wall"
 1,  4, "city.modern_occupied"

 1,  0, "city.postmodern_0"
 1,  1, "city.postmodern_5"
 1,  2, "city.postmodern_10"
 1,  3, "city.postmodern_wall"
 1,  4, "city.postmodern_occupied"

}
