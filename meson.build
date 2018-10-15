
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

inc = include_directories('utility', 'common', 'common/networking')

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
  dependencies: [c_compiler.find_library('libicuuc'),
                 c_compiler.find_library('m'),
                 c_compiler.find_library('libcurl'),
                 dependency('threads')]
  )
