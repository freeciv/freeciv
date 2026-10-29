
[spec]

; Format and options of this spec file:
options = "+Freeciv-spec-3.4-Devel-2026.Oct.03"

[info]
; Special thanks go to:
; Fairline for his huge collection of original Civ2 unit spanning centuries


artists = "
    Fairline [GB]
    Hani Ejeilat [EJ] <kode2457@gmail.com>
"

[file]
gfx = "amplio2/worker_unit"

[grid_main]

x_top_left = 1
y_top_left = 1
dx = 64
dy = 48
pixel_border = 1

tiles = { "row", "column", "tag"
                                  ; Scenario League tags in brackets
  0,  0, "u.worker_Idle:0"         ; [GB]
  0,  1, "u.worker_Idle:1"         ; [GB] & [EJ]
  0,  2, "u.worker_walk:0"         ; [GB] & [EJ]
  1,  0, "u.worker_Plant:0"        ; [GB] & [EJ]
  1,  1, "u.worker_Plant:1"        ; [GB] & [EJ]
  1,  2, "u.worker_Plant:2"        ; [GB] & [EJ]
  2,  0, "u.worker_Cultivate:0"    ; [GB] & [EJ]
  2,  1, "u.worker_Cultivate:1"    ; [GB] & [EJ]
  2,  2, "u.worker_Cultivate:2"    ; [GB] & [EJ]
}
