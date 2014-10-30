-- Freeciv - Copyright (C) 2007 - The Freeciv Project
--   This program is free software; you can redistribute it and/or modify
--   it under the terms of the GNU General Public License as published by
--   the Free Software Foundation; either version 2, or (at your option)
--   any later version.
--
--   This program is distributed in the hope that it will be useful,
--   but WITHOUT ANY WARRANTY; without even the implied warranty of
--   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
--   GNU General Public License for more details.

-- Show a pop up telling the beginning of the story when the game starts.
function turn_callback(turn, year)
  if turn == 0 then
    notify.event(nil, nil, E.SCRIPT,
_("Deneb 7 was known to have strange force field surrounding it\nthat made it impossible to get near planet with year\n250 Galactic Era Earth technology. However, when you were\nflying past, that field suddenly reverted and sucked you\nto the planet.\n\nYou find yourself in a strange world, probably touched\nby superior technology, where big portion of Earth science\nis invalid. You have to learn new rules,\nrules of this world.\n\nThere's deadly radiation that no known shielding works against.\nThere's alien life, but more surprisingly also some\nedible plants just like on Earth.\n\nRadio doesn't work,\nair doesn't allow flying, some basic Physics does\nnot apply here.\n\nYou struggle to live on this planet, and read\nRoadside Picnic by Strugatsky brothers once more."))
  end
end

signal.connect('turn_started', 'turn_callback')
