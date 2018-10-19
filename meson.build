
project('freeciv', 'c')

priv_conf_data = configuration_data()
pub_conf_data = configuration_data()

priv_conf_data.set('VERSION_STRING', '3.0.91-dev')
pub_conf_data.set('FREECIV_AI_MOD_LAST', 3)
priv_conf_data.set('LOCALEDIR',
                   join_paths(get_option('prefix'),
                              get_option('datadir'), 'locale'))
pub_conf_data.set('FREECIV_STORAGE_DIR', '~/.freeciv')
priv_conf_data.set('DATASUBDIR', 'dev')

configure_file(input : 'gen_headers/meson_fc_config.h.in',
               output : 'fc_config.h',
               configuration: priv_conf_data)
configure_file(input : 'gen_headers/meson_freeciv_config.h.in',
               output : 'freeciv_config.h',
               configuration: pub_conf_data)

add_global_arguments('-DHAVE_CONFIG_H',
                     language: 'c')

c_compiler = meson.get_compiler('c')

inc = include_directories('dependencies/lua-5.3/src', 'dependencies/tolua-5.2/include',
  'utility', 'common', 'common/networking')

specenum = custom_target('specenum_gen.h', output: 'specenum_gen.h',
                         command: [files('utility/generate_specenum.py'), '@OUTPUT@'])

pack_common = custom_target('packets_common', output: ['packets_gen.h',
                                                       'packets_gen.c'],
                            command: [files('common/generate_packets.py'),
                                      '@OUTPUT0@', '@OUTPUT1@', '', '', '', ''],
                            depend_files: files('common/networking/packets.def'))

pack_server = custom_target('packets_server', output: ['hand_gen.h', 'hand_gen.c'],
                            command: [files('common/generate_packets.py'),
                                      '', '', '', '', '@OUTPUT0@', '@OUTPUT1@'],
                            depend_files: files('common/networking/packets.def'))

pack_client = custom_target('packets_client', output: ['packhand_gen',
                                                       'packhand_gen.c'],
                            command: [files('common/generate_packets.py'),
                                      '', '', '@OUTPUT0@', '@OUTPUT1@', '', ''],
                            depend_files: files('common/networking/packets.def'))

fc_deps = static_library('fc_dependencies',
  'dependencies/cvercmp/cvercmp.c',
  'dependencies/lua-5.3/src/lapi.c',
  'dependencies/lua-5.3/src/lauxlib.c',
  'dependencies/lua-5.3/src/lbaselib.c',
  'dependencies/lua-5.3/src/lbitlib.c',
  'dependencies/lua-5.3/src/lcode.c',
  'dependencies/lua-5.3/src/lcorolib.c',
  'dependencies/lua-5.3/src/lctype.c',
  'dependencies/lua-5.3/src/ldblib.c',
  'dependencies/lua-5.3/src/ldebug.c',
  'dependencies/lua-5.3/src/ldo.c',
  'dependencies/lua-5.3/src/ldump.c',
  'dependencies/lua-5.3/src/lfunc.c',
  'dependencies/lua-5.3/src/lgc.c',
  'dependencies/lua-5.3/src/linit.c',
  'dependencies/lua-5.3/src/liolib.c',
  'dependencies/lua-5.3/src/llex.c',
  'dependencies/lua-5.3/src/lmathlib.c',
  'dependencies/lua-5.3/src/lmem.c',
  'dependencies/lua-5.3/src/loadlib.c',
  'dependencies/lua-5.3/src/lobject.c',
  'dependencies/lua-5.3/src/lopcodes.c',
  'dependencies/lua-5.3/src/loslib.c',
  'dependencies/lua-5.3/src/lparser.c',
  'dependencies/lua-5.3/src/lstate.c',
  'dependencies/lua-5.3/src/lstring.c',
  'dependencies/lua-5.3/src/lstrlib.c',
  'dependencies/lua-5.3/src/ltable.c',
  'dependencies/lua-5.3/src/ltablib.c',
  'dependencies/lua-5.3/src/ltm.c',
  'dependencies/lua-5.3/src/lundump.c',
  'dependencies/lua-5.3/src/lutf8lib.c',
  'dependencies/lua-5.3/src/lvm.c',
  'dependencies/lua-5.3/src/lzio.c',
  'dependencies/tolua-5.2/src/lib/tolua_event.c',
  'dependencies/tolua-5.2/src/lib/tolua_is.c',
  'dependencies/tolua-5.2/src/lib/tolua_map.c',
  'dependencies/tolua-5.2/src/lib/tolua_push.c',
  'dependencies/tolua-5.2/src/lib/tolua_to.c',
  'dependencies/luasql/src/luasql.c',
  'dependencies/luasql/src/ls_sqlite3.c',
  include_directories : inc
  )

library('freeciv',
  'utility/astring.c',
  'utility/bitvector.c',
  'utility/bugs.c',
  'utility/capability.c',
  'utility/deprecations.c',
  'utility/distribute.c',
  'utility/fcbacktrace.c',
  'utility/fc_cmdline.c',
  'utility/fc_dirent.c',
  'utility/fciconv.c',
  'utility/fcintl.c',
  'utility/fcthread.c',
  'utility/fc_utf8.c',
  'utility/genhash.c',
  'utility/genlist.c',
  'utility/inputfile.c',
  'utility/ioz.c',
  'utility/iterator.c',
  'utility/log.c',
  'utility/md5.c',
  'utility/mem.c',
  'utility/netfile.c',
  'utility/netintf.c',
  'utility/rand.c',
  'utility/registry.c',
  'utility/registry_ini.c',
  'utility/registry_xml.c',
  'utility/section_file.c',
  'utility/shared.c',
  'utility/string_vector.c',
  'utility/support.c',
  'utility/timing.c',
  include_directories : inc,
  sources: [specenum],
  link_whole: fc_deps,
  dependencies: [c_compiler.find_library('libicuuc'),
                 c_compiler.find_library('m'),
                 c_compiler.find_library('libcurl'),
                 c_compiler.find_library('libsqlite3'),
                 dependency('threads')]
  )
