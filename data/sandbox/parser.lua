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

-- This file is for lua-functionality for parsing luadata.txt
-- of this ruleset.

function turn_callback(turn, year)
  msg = luadata.get_str(string.format("turn_%d.msg", turn))
  if msg then
    log.debug("Turn %d msg: %s", turn, msg)
    notify.event(nil, nil, E.SCRIPT, msg)
  end
end

signal.connect('turn_begin', 'turn_callback')
