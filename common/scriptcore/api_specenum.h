/*****************************************************************************
 Freeciv - Copyright (C) 2010 - The Freeciv Project
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
*****************************************************************************/
#ifndef FC__API_SPECENUM_H
#define FC__API_SPECENUM_H

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#define API_SPECENUM_INDEX_NAME(type) api_specenum_##type##_index
#define API_SPECENUM_NAME_NAME(type) api_specenum_##type##_name

/**********************************************************************//**
  Define a the __index (table, key) -> value  metamethod
  Return the enum value whose name is the concatenation of prefix and key.
  The fetched value is written back to the lua table, and further accesses
  will resolve there instead of this function.
**************************************************************************/
#define API_SPECENUM_DEFINE_INDEX(type_name, prefix)                      \
  static int (API_SPECENUM_INDEX_NAME(type_name))(lua_State *L)           \
  {                                                                       \
    static char _buf[128];                                                \
    const char *_key;                                                     \
    enum type_name _value;                                                \
    luaL_checktype(L, 1, LUA_TTABLE);                                     \
    _key = luaL_checkstring(L, 2);                                        \
    fc_snprintf(_buf, sizeof(_buf), prefix "%s", _key);                   \
    _value = type_name##_by_name(_buf, strcmp);                           \
    if (_value != type_name##_invalid()) {                                \
      /* T[_key] = _value */                                              \
      lua_pushstring(L, _key);                                            \
      lua_pushinteger(L, _value);                                         \
      lua_rawset(L, 1);                                                   \
      lua_pushinteger(L, _value);                                         \
    } else {                                                              \
      lua_pushnil(L);                                                     \
    }                                                                     \
    return 1;                                                             \
  }

/**********************************************************************//**
  Define the __index (table, key) -> value  metamethod
  Return the enum name whose value is supplied.
  The fetched value is written back to the lua table, and further accesses
  will resolve there instead of this function.
  Note that the indices usually go from 0, not 1.
**************************************************************************/
#define API_SPECENUM_DEFINE_INDEX_REV(type_name)                          \
  static int (API_SPECENUM_NAME_NAME(type_name))(lua_State *L)            \
  {                                                                       \
    enum type_name _key;                                                  \
    const char *_value;                                                   \
    luaL_checktype(L, 1, LUA_TTABLE);                                     \
    _key = luaL_checkinteger(L, 2);                                       \
    if (type_name##_is_valid(_key)) {                                     \
      _value = type_name##_name(_key);                                    \
      /* T[_key] = _value */                                              \
      lua_pushinteger(L, _key);                                           \
      lua_pushstring(L, _value);                                          \
      lua_rawset(L, 1);                                                   \
      lua_pushstring(L, _value);                                          \
    } else {                                                              \
      lua_pushnil(L);                                                     \
    }                                                                     \
    return 1;                                                             \
  }

void api_specenum_create_table(lua_State *L, const char *name,
                               lua_CFunction findex);

#define API_SPECENUM_CREATE_TABLE(L, type, name)                             \
  api_specenum_create_table((L), (name), API_SPECENUM_INDEX_NAME(type))
#define API_SPECENUM_CREATE_TABLE_REV(L, type, name)                             \
  api_specenum_create_table((L), (name), API_SPECENUM_NAME_NAME(type))



#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* FC__API_SPECENUM_H */
