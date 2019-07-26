;
; The names for city tiles are not free and must follow the following rules.
; The names consists of 'style name' + '_' + 'index'. The style name is as
; specified in cities.ruleset file and the index only defines the read order
; of the images. The definitions are read starting with index 0 till the first
; missing value The index is checked against the city bonus of effect
; EFT_CITY_IMG and the resulting image is used to draw the city on the tile.
;
; Obviously the first tile must be 'style_name'_city_0 and the sizes must be
; in ascending order. There must also be a 'style_name'_wall_0 tile used to
; draw the wall and an occupied tile to indicate a military units in a city.
; The maximum number of images is only limited by the maximum size of a city
; (currently MAX_CITY_SIZE = 255).
;

[spec]

; Format and options of this spec file:
options = "+Freeciv-spec-Devel-2019-Jul-03"

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

 2,  11, "city.asian_occupied_0"
 2,  11, "city.tropical_occupied_0"
 2,  11, "city.celtic_occupied_0"
 2,  11, "city.classical_occupied_0"
 2,  11, "city.babylonian_occupied_0"


;
; city tiles
;

 0,  0, "city.asian_city_0"
 0,  1, "city.asian_city_1"
 0,  2, "city.asian_city_2"
 0,  3, "city.asian_city_3"
 0,  4, "city.asian_city_4" 
 0,  5, "city.asian_wall_0"
 0,  6, "city.asian_wall_1"
 0,  7, "city.asian_wall_2"
 0,  8, "city.asian_wall_3"
 0,  9, "city.asian_wall_4" 
   

 1,  0, "city.tropical_city_0"
 1,  1, "city.tropical_city_1"
 1,  2, "city.tropical_city_2"
 1,  3, "city.tropical_city_3"
 1,  4, "city.tropical_city_4" 
 1,  5, "city.tropical_wall_0"
 1,  6, "city.tropical_wall_1"
 1,  7, "city.tropical_wall_2"
 1,  8, "city.tropical_wall_3"
 1,  9, "city.tropical_wall_4" 


 2,  0, "city.celtic_city_0"
 2,  1, "city.celtic_city_1"
 2,  2, "city.celtic_city_2"
 2,  3, "city.celtic_city_3"
 2,  4, "city.celtic_city_4" 
 2,  5, "city.celtic_wall_0"
 2,  6, "city.celtic_wall_1"
 2,  7, "city.celtic_wall_2"
 2,  8, "city.celtic_wall_3"
 2,  9, "city.celtic_wall_4" 


 3,  0, "city.classical_city_0"
 3,  1, "city.classical_city_1"
 3,  2, "city.classical_city_2"
 3,  3, "city.classical_city_3"
 3,  4, "city.classical_city_4"
 3,  5, "city.classical_wall_0"
 3,  6, "city.classical_wall_1"
 3,  7, "city.classical_wall_2"
 3,  8, "city.classical_wall_3"
 3,  9, "city.classical_wall_4"

 4,  0, "city.babylonian_city_0"
 4,  1, "city.babylonian_city_1"
 4,  2, "city.babylonian_city_2"
 4,  3, "city.babylonian_city_3"
 4,  4, "city.babylonian_city_4"
 4,  5, "city.babylonian_wall_0"
 4,  6, "city.babylonian_wall_1"
 4,  7, "city.babylonian_wall_2"
 4,  8, "city.babylonian_wall_3"
 4,  9, "city.babylonian_wall_4"

}
