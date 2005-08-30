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
options = "+spec3"

[info]

artists = "
    Asian style by CapTVK
    Polynesian style by CapTVK
    Celtic style by Erwan, adapted to 96x48 by CapTVK
    Roman style by CapTVK
    City walls by Hogne Håskjold
"

[file]
gfx = "amplio/ancientcities"

[grid_main]

x_top_left = 1
y_top_left = 1
dx = 96
dy = 72
pixel_border = 1

tiles = { "row", "column", "tag"

; used by all city styles

 2,  11, "city.asian_occupied"
 2,  11, "city.tropical_occupied"
 2,  11, "city.celtic_occupied"
 2,  11, "city.classical_occupied"
 2,  11, "city.babylonian_occupied"


;
; city tiles
;

 0,  0, "city.asian_0"
 0,  1, "city.asian_4"
 0,  2, "city.asian_8"
 0,  3, "city.asian_12"
 0,  4, "city.asian_16" 
 0,  5, "city.asian_0_wall"
 0,  6, "city.asian_4_wall"
 0,  7, "city.asian_8_wall"
 0,  8, "city.asian_12_wall"
 0,  9, "city.asian_16_wall" 
   

 1,  0, "city.tropical_0"
 1,  1, "city.tropical_4"
 1,  2, "city.tropical_8"
 1,  3, "city.tropical_12"
 1,  4, "city.tropical_16" 
 1,  5, "city.tropical_0_wall"
 1,  6, "city.tropical_4_wall"
 1,  7, "city.tropical_8_wall"
 1,  8, "city.tropical_12_wall"
 1,  9, "city.tropical_16_wall" 


 2,  0, "city.celtic_0"
 2,  1, "city.celtic_4"
 2,  2, "city.celtic_8"
 2,  3, "city.celtic_12"
 2,  4, "city.celtic_16" 
 2,  5, "city.celtic_0_wall"
 2,  6, "city.celtic_4_wall"
 2,  7, "city.celtic_8_wall"
 2,  8, "city.celtic_12_wall"
 2,  9, "city.celtic_16_wall" 


 3,  0, "city.classical_0"
 3,  1, "city.classical_4"
 3,  2, "city.classical_8"
 3,  3, "city.classical_12"
 3,  4, "city.classical_16"
 3,  5, "city.classical_0_wall"
 3,  6, "city.classical_4_wall"
 3,  7, "city.classical_8_wall"
 3,  8, "city.classical_12_wall"
 3,  9, "city.classical_16_wall"

 4,  0, "city.babylonian_0"
 4,  1, "city.babylonian_4"
 4,  2, "city.babylonian_8"
 4,  3, "city.babylonian_12"
 4,  4, "city.babylonian_16"
 4,  5, "city.babylonian_0_wall"
 4,  6, "city.babylonian_4_wall"
 4,  7, "city.babylonian_8_wall"
 4,  8, "city.babylonian_12_wall"
 4,  9, "city.babylonian_16_wall"

}
