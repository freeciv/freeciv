[spec]

; Format and options of this spec file:
options = "+spec3"

[info]

artists = "
    Falk Hueffner <falk.hueffner@student.uni-tuebingen.de>
    Tomasz Wegrzanowski <maniek@beer.com> (Bulgaria...Boer, Silesia)
    Paul Zastoupil <paulz@adnc.com> (Dunedain, Mordor)
    Jeff Mallatt <jjm@codewell.com> (Bavarian, Rome, Cornwall)
"

[file]
gfx = "misc/shields"

[grid_main]

x_top_left = 0
y_top_left = 0
dx = 45
dy = 45

tiles = { "row", "column", "tag"
  0,  0, "f.italy"
  0,  1, "f.iraq_old"
  0,  2, "f.germany"
  0,  3, "f.egypt"
  0,  4, "f.usa"
  0,  5, "f.greece"
  0,  6, "f.india"
  0,  7, "f.russia"
  0,  8, "f.rwanda"       ; for Zulu
  0,  9, "f.france"
  0, 10, "f.mexico"
  0, 11, "f.china"
  0, 12, "f.united_kingdom"
  0, 13, "f.mongolia"
  1,  0, "f.denmark"
  1,  1, "f.australia"
  1,  2, "f.brasil"
  1,  3, "f.soviet"
  1,  4, "f.japan"
  1,  5, "f.spain"
  1,  6, "f.finland"
  1,  7, "f.hungary"
  1,  8, "f.poland"
  1,  9, "f.iran"
  1, 10, "f.peru"
  1, 11, "f.turkey"
  1, 12, "f.tunisia"
  1, 13, "f.arab"         ; Saudi Arabia
  2,  0, "f.south_africa" ; Republic of South Africa, for Zulus
  2,  1, "f.sweden"
  2,  2, "f.netherlands", 
	 "f.holland"      ; backward compatibility
  2,  3, "f.syria"
  2,  4, "f.macedonia"
  2,  5, "f.ukraine"
  2,  6, "f.cheyenne"
  2,  7, "f.norway"
  2,  8, "f.portugal"
  2,  9, "f.czech"
  2, 10, "f.england"
  2, 11, "f.scotland"
  2, 12, "f.unknown"      ; useful for alternates
  2, 13, "f.barbarian"
  3,  0, "f.europe"
  3,  1, "f.canada"
  3,  2, "f.korea"
  3,  3, "f.israel"
  3,  4, "f.ireland"
  3,  5, "f.belgium"
  3,  6, "f.iceland"
  3,  7, "f.pakistan"
  3,  8, "f.greenland"
  3,  9, "f.austria"
  3, 10, "f.argentina"
  3, 11, "f.united_nations"
  3, 12, "f.nato"
  3, 13, "f.vietnam"
  4,  0, "f.thailand"
  4,  1, "f.olympic"
  4,  2, "f.krev"
  4,  3, "f.wales"
  4,  4, "f.lithuania"
  4,  5, "f.kenya"
  4,  6, "f.dunedain"
  4,  7, "f.bulgaria"
  4,  8, "f.armenia"
  4,  9, "f.azerbaijan"
  4, 10, "f.boer"         ; old south african
  4, 11, "f.mordor"
  4, 12, "f.bavarian"
  4, 13, "f.rome"         ; Roman republic flag
  5,  0, "f.cornwall"
  5,  1, "f.philippines"
  5,  2, "f.estonia"
  5,  3, "f.latvia"
  5,  4, "f.silesia"
  5,  5, "f.singapore"
  5,  6, "f.chile"
  5,  7, "f.catalan"
  5,  8, "f.croatia"
  5,  9, "f.slovenia"
  5, 10, "f.serbia"
}

; These new-style shield graphics are taken from the auto-rendered shield
; graphics in the development version.  Their sources are included in the
; development version which may be downloaded from http://freeciv.org/.
[extra]
sprites =
	{	"tag", "file"
		"f.afghanistan", "flags/afghanistan-shield"
		"f.assyria", "flags/assyria-shield"
		"f.bosnia", "flags/bosnia-shield"
		"f.columbia", "flags/columbia-shield"
		"f.elves", "flags/elves-shield"
		"f.ethiopia", "flags/ethiopia-shield"
		"f.galicia", "flags/galicia-shield"
		"f.hobbits", "flags/hobbits-shield"
		"f.indonesia", "flags/indonesia-shield"
		"f.kampuchea", "flags/kampuchea-shield"
		"f.malaysia", "flags/malaysia-shield"
		"f.mars", "flags/mars-shield"
		"f.nigeria", "flags/nigeria-shield"
		"f.phoenicia", "flags/phoenicia-shield"
		"f.quebec", "flags/quebec-shield"
		"f.sumeria", "flags/sumeria-shield"
		"f.switzerland", "flags/swiss-shield"
		"f.taiwan", "flags/taiwan-shield"
	}
