
[spec]

; Format and options of this spec file:
options = "+spec3"

[info]

artists = "
     Madeline Book <madeline.book@gmail.com>

     Images base on parts from various places:
        Tango Desktop Project <http://tango.freedesktop.org/>
        Freeciv Trident Tileset
        Freeciv HiRes Tileset
        Freeciv JRR Tileset
        Freeciv Amplio Tileset

     Others are unattributed and believed to be in the
     public domain. If you have evidence to the contrary,
     and do not wish the image to be used here, then
     please contact the author to have it removed.
"

[file]
gfx = "misc/editor"

[grid_main]

x_top_left = 1
y_top_left = 1
dx = 30
dy = 30
pixel_border = 1

tiles = { "row", "column", "tag"

  0,  0, "editor.erase"
  0,  1, "editor.brush"
  0,  2, "editor.startpos"
  0,  3, "editor.terrain"
  0,  4, "editor.terrain_resource"
  0,  5, "editor.terrain_special"
  0,  6, "editor.unit"
  0,  7, "editor.city"
  0,  8, "editor.vision"
  0,  9, "editor.territory"
  0, 10, "editor.properties"
  0,  5, "editor.military_base"
}
