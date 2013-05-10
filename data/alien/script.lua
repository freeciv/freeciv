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

function turn_callback(turn, year)
  if turn == 0 then
    notify.event(nil, nil, E.SCRIPT,
_("Deneb 7 was known to have strange force field surrounding it\
that made it impossible to get near planet with year 250 Galactic Era Earth\
technology. However, when you were flying past, that field suddenly\
reverted and sucked you to the planet.\n\n\
You find yourself in a strange world, probably touched by superior\
technology, where big portion of Earth science is invalid. You\
have to learn new rules, rules of this world.\n\n\
There's deadly radiation that no known shielding works against.\
There's alien life, but more surprisingly also some edible\
plants just like on Earth.\n\n\
Radio doesn't work, air doesn't allow flying, some basic Physics\
does not apply here.\n\n\
You struggle to live on this planet, and read Roadside Picnic by Strugatsky\
brothers once more."))
  end
end

signal.connect('turn_started', 'turn_callback')
