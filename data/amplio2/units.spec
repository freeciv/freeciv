
[spec]

; Format and options of this spec file:
options = "+Freeciv-spec-3.3-Devel-2023.Apr.05"

[info]

; Apolyton Tileset created by CapTVK with thanks to the Apolyton Civ2
; Scenario League.

; Special thanks go to:
; Alex Mor and Captain Nemo for their excellent graphics work
; in the scenarios 2194 days war, Red Front, 2nd front and other misc graphics.
; Fairline for his huge collection of original Civ2 unit spanning centuries
; Bebro for his collection of mediveal units and ships

artists = "
    Alex Mor [Alex]
    Allard H.S. Höfelt [AHS]
    Bebro [BB]
    Captain Nemo [Nemo][MHN]
    CapTVK [CT] <thomas@worldonline.nl>
    Curt Sibling [CS]
    Erwan [EW]
    Fairline [GB]
    GoPostal [GP]
    Oprisan Sorin [Sor]
    Tanelorn [T]
    Paul Klein Lankhorst / GukGuk [GG]
    Andrew ''Panda´´ Livings [APL]
    Vodvakov
    J. W. Bjerk / Eleazar <www.jwbjerk.com>
    qwm
    FiftyNine
"

[file]
gfx = "amplio2/units"

[grid_main]

x_top_left = 1
y_top_left = 1
dx = 64
dy = 48
pixel_border = 1

tiles = { "row", "column", "tag"
                                  ; Scenario League tags in brackets
  0,  0, "u.armor_Idle:0"         ; [Nemo]
  0,  1, "u.howitzer_Idle:0"      ; [Nemo]
  0,  2, "u.battleship_Idle:0"    ; [Nemo]
  0,  3, "u.bomber_Idle:0"        ; [GB]
  0,  4, "u.cannon_Idle:0"        ; [CT]
  0,  5, "u.caravan_Idle:0"       ; [Alex] & [CT]
  0,  6, "u.carrier_Idle:0"       ; [Nemo]
  0,  7, "u.catapult_Idle:0"      ; [CT]
  0,  8, "u.horsemen_Idle:0"      ; [GB]
  0,  9, "u.chariot_Idle:0"       ; [BB] & [GB]
  0, 10, "u.cruiser_Idle:0"       ; [Nemo]
  0, 11, "u.diplomat_Idle:0"      ; [Nemo]
  0, 12, "u.fighter_Idle:0"       ; [Sor]
  0, 13, "u.frigate_Idle:0"       ; [BB]
  0, 14, "u.ironclad_Idle:0"      ; [Nemo]
  0, 15, "u.knights_Idle:0"       ; [BB]
  0, 16, "u.legion_Idle:0"        ; [GB]
  0, 17, "u.mech_inf_Idle:0"      ; [GB]
  0, 18, "u.warriors_Idle:0"      ; [GB]
  0, 19, "u.musketeers_Idle:0"    ; [Alex] & [CT]
  1,  0, "u.nuclear_Idle:0"       ; [Nemo] & [CS]
  1,  1, "u.phalanx_Idle:0"       ; [GB] & [CT]
  1,  2, "u.riflemen_Idle:0"      ; [Alex]
  1,  3, "u.caravel_Idle:0"       ; [BB]
  1,  4, "u.settlers_Idle:0"      ; [MHN]
  1,  5, "u.submarine_Idle:0"     ; [GP]
  1,  6, "u.transport_Idle:0"     ; [Nemo]
  1,  7, "u.trireme_Idle:0"       ; [BB]
  1,  8, "u.archers_Idle:0"       ; [GB]
  1,  9, "u.cavalry_Idle:0"       ; [Alex]
  1, 10, "u.cruise_missile_Idle:0" ; [CS]
  1, 11, "u.destroyer_Idle:0"     ; [Nemo]
  1, 12, "u.dragoons_Idle:0"      ; [GB]
  1, 13, "u.explorer_Idle:0"      ; [Alex] & [CT]
  1, 14, "u.freight_Idle:0"       ; [CT] & qwm
  1, 15, "u.galleon_Idle:0"       ; [BB]
  1, 16, "u.partisan_Idle:0"      ; [BB] & [CT]
  1, 17, "u.pikemen_Idle:0"       ; [T]
  2,  0, "u.marines_Idle:0"       ; [GB]
  2,  1, "u.spy_Idle:0"           ; [EW] & [CT]
  2,  2, "u.engineers_Idle:0"     ; [Nemo] & [CT]
  2,  3, "u.artillery_Idle:0"     ; [GB]
  2,  4, "u.helicopter_Idle:0"    ; [T]
  2,  5, "u.alpine_troops_Idle:0" ; [Nemo]
  2,  6, "u.stealth_bomber_Idle:0" ; [GB]
  2,  7, "u.stealth_fighter_Idle:0" ; [Nemo] & [AHS]
  2,  8, "u.aegis_cruiser_Idle:0" ; [GP]
  2,  9, "u.paratroopers_Idle:0"  ; [Alex]
  2, 10, "u.elephants_Idle:0"     ; [Alex] & [GG] & [CT]
  2, 11, "u.crusaders_Idle:0"     ; [BB]
  2, 12, "u.fanatics_Idle:0"      ; [GB] & [CT]
  2, 13, "u.awacs_Idle:0"         ; [APL]
  2, 14, "u.worker_Idle:0"        ; [GB]
  2, 15, "u.leader_Idle:0"        ; [GB]
  2, 16, "u.barbarian_leader_Idle:0" ; FiftyNine
  2, 17, "u.migrants_Idle:0"      ; Eleazar
  2, 18, "u.storm_Idle:0"
;  2, 19, "u.train_Idle:0"        ; Eleazar

}
