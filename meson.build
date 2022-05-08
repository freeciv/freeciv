
project('freeciv', ['c', 'cpp'], meson_version: '>= 0.57.0')

if not get_option('ack_experimental')
  error('Autotools are still the only fully supported way to build freeciv.\nSet configure option "ack_experimental" to true if you really want to do experimental meson build.\n' +
        'Most scripts that do freeciv build with meson allow you to pass configure options\nvia environment variable EXTRA_CONFIG.')
endif

add_global_arguments('-Wno-nonnull-compare', language : 'c')

add_global_arguments('-fPIC', language : 'cpp')
add_global_arguments('-DQT_DISABLE_DEPRECATED_BEFORE=0x050b00', language : 'cpp')

host_system = host_machine.system()

priv_conf_data = configuration_data()
pub_conf_data = configuration_data()
liblua_conf_data = configuration_data()

pub_conf_data.set('FREECIV_AI_MOD_LAST', 3)
priv_conf_data.set('BINDIR',
                   join_paths(get_option('prefix'), get_option('bindir')))

priv_conf_data.set('DATADIR',
                   join_paths(get_option('prefix'), get_option('datadir')))
priv_conf_data.set('DATASUBDIR', 'test')
priv_conf_data.set('SYSCONFDIR',
                   join_paths(get_option('prefix'), get_option('sysconfdir')))

priv_conf_data.set('IS_DEVEL_VERSION', 1)

if host_machine.endian() == 'big'
  priv_conf_data.set('WORDS_BIGENDIAN', 1)
endif

if get_option('debug')
  priv_conf_data.set('FREECIV_DEBUG', 1)
  pub_conf_data.set('FREECIV_DEBUG', 1)
  add_global_arguments('-Werror', language: ['c', 'cpp'])
else
  add_global_arguments('-DQT_NO_DEBUG', language : 'cpp')
endif

if host_system == 'windows'
  pub_conf_data.set('FREECIV_STORAGE_DIR', '~\\\\.freeciv')
  priv_conf_data.set('LOCALEDIR',
                     join_paths('.', get_option('datadir'), 'locale'))

  priv_conf_data.set('DEFAULT_DATA_PATH',
    '".;data;@FREECIV_STORAGE_DIR@/@DATASUBDIR@;@DATADIR@/freeciv"')
  priv_conf_data.set('DEFAULT_SAVE_PATH',
    '".;@FREECIV_STORAGE_DIR@/saves"')
  priv_conf_data.set('DEFAULT_SCENARIO_PATH',
    '".;data/scenarios;@FREECIV_STORAGE_DIR@/@DATASUBDIR@/scenarios;@FREECIV_STORAGE_DIR@/scenarios;@DATADIR@/freeciv/scenarios"')
else
  if host_system == 'haiku'
    pub_conf_data.set('FREECIV_STORAGE_DIR', '~/config/settings/freeciv')
  else
    pub_conf_data.set('FREECIV_STORAGE_DIR', '~/.freeciv')
  endif
  priv_conf_data.set('LOCALEDIR',
                     join_paths(get_option('prefix'),
                                get_option('datadir'), 'locale'))

  priv_conf_data.set('DEFAULT_DATA_PATH',
    '".:data:@FREECIV_STORAGE_DIR@/@DATASUBDIR@:@DATADIR@/freeciv"')
  priv_conf_data.set('DEFAULT_SAVE_PATH',
    '".:@FREECIV_STORAGE_DIR@/saves"')
  priv_conf_data.set('DEFAULT_SCENARIO_PATH',
    '".:data/scenarios:@FREECIV_STORAGE_DIR@/@DATASUBDIR@/scenarios:@FREECIV_STORAGE_DIR@/scenarios:@DATADIR@/freeciv/scenarios"')
endif

if get_option('freeciv-web')
  pub_conf_data.set('FREECIV_WEB', 1)
  server_binary_name = 'freeciv-web'
else
  pub_conf_data.set('FREECIV_DELTA_PROTOCOL', 1)
  server_binary_name = 'freeciv-server'
endif

c_compiler = meson.get_compiler('c')

if meson.is_cross_build()
  cross_inc_str = meson.get_cross_property('cross_inc_path')
  cross_inc_path = [cross_inc_str]
  cross_lib_path = [meson.get_cross_property('cross_lib_path')]
else
  cross_inc_str = ''
  cross_inc_path = []
  cross_lib_path = []
endif

# From this, at least the _WIN32_WINNT must be set before
# trying to find the functions.
if host_system == 'windows'
  pub_conf_data.set('FREECIV_MSWINDOWS', 1)
  pub_conf_data.set('FREECIV_HAVE_TINYCTHR', 1)
  pub_conf_data.set('FREECIV_HAVE_WINSOCK', 1)
  pub_conf_data.set('FREECIV_SOCKET_ZERO_NOT_STDIN', 1)
  priv_conf_data.set('ALWAYS_ROOT', 1)
  add_global_arguments('-D_WIN32_WINNT=0x0603', language : ['c', 'cpp'])
  net_dep = c_compiler.find_library('ws2_32')
elif host_system == 'haiku'
  net_dep = c_compiler.find_library('network')
  pub_conf_data.set('FREECIV_HAVE_PTHREAD', 1)
  priv_conf_data.set('ALWAYS_ROOT', 1)
else
  pub_conf_data.set('FREECIV_HAVE_PTHREAD', 1)
  net_dep = []
endif

pub_headers = [
  'locale.h',
  'libintl.h',
  'inttypes.h',
  'stdint.h',
  'ws2tcpip.h',
  'sys/types.h',
  'unistd.h',
  'sys/time.h',
  'sys/socket.h',
  'sys/select.h',
  'netinet/in.h',
  'dirent.h',
  'stdbool.h',
  'winsock2.h'
  ]

priv_headers = [
  'arpa/inet.h',
  'bcrypt.h',
  'bzlib.h',
  'direct.h',
  'dlfcn.h',
  'execinfo.h',
  'fcntl.h',
  'time.h',
  'libgen.h',
  'lzma.h',
  'zstd.h',
  'memory.h',
  'netdb.h',
  'pwd.h',
  'signal.h',
  'stdlib.h',
  'strings.h',
  'string.h',
  'sys/file.h',
  'sys/ioctl.h',
  'sys/random.h',
  'sys/signal.h',
  'sys/stat.h',
  'sys/termio.h',
  'sys/uio.h',
  'sys/utsname.h',
  'sys/wait.h',
  'termios.h',
  'vfork.h'
  ]

if cross_inc_str == ''
  header_arg = []
else
  header_arg = '-I' + cross_inc_str
endif
foreach hdr : pub_headers
  if c_compiler.has_header(hdr, args: header_arg)
    pub_conf_data.set('FREECIV_HAVE_' + hdr.underscorify().to_upper(), 1)
    priv_conf_data.set('HAVE_' + hdr.underscorify().to_upper(), 1)
  endif
endforeach

foreach hdr : priv_headers
  if c_compiler.has_header(hdr, args: header_arg)
    priv_conf_data.set('HAVE_' + hdr.underscorify().to_upper(), 1)
  endif
endforeach

priv_functions = [
  'fork',
  'vfork',
  'backtrace',
  'bind',
  'clock_gettime',
  'connect',
  'fdopen',
  'fileno',
  'flock',
  'getentropy',
  'gettimeofday',
  'ftime',
  'getaddrinfo',
  'gethostbyname',
  'getline',
  'getnameinfo',
  'getpwuid',
  'inet_aton',
  'inet_ntop',
  'inet_pton',
  'opendir',
  'putenv',
  'getcwd',
  'select',
  'setenv',
  'snooze',
  'strcasecoll',
  'strcasestr',
  'strcoll',
  'strerror',
  'stricoll',
  'strstr',
  'uname',
  'nanosleep',
  'usleep',
  'vprintf',
  'vsnprintf',
  '_mkdir',
  '_strcoll',
  '_stricoll',
  'fcntl',
  'ioctl'
  ]

foreach func : priv_functions
  if c_compiler.has_function(func,
                             dependencies: net_dep)
    priv_conf_data.set('HAVE_' + func.underscorify().to_upper(), 1)
  endif
endforeach

liblua_functions = [
  'mkstemp',
  'popen',
  'pclose',
  '_longjmp',
  '_setjmp',
  'gmtime_r',
  'localtime_r',
  'fseeko'
  ]

foreach func : liblua_functions
  if c_compiler.has_function(func,
                             dependencies: net_dep)
    liblua_conf_data.set('HAVE_' + func.underscorify().to_upper(), 1)
  endif
endforeach

if c_compiler.has_header('unistd.h')
  liblua_conf_data.set('FREECIV_HAVE_UNISTD_H', 1)
endif

if c_compiler.has_function('BCryptGenRandom', args: '-lbcrypt')
  bcrypt_lib_dep = c_compiler.find_library('bcrypt')
  priv_conf_data.set('HAVE_BCRYPTGENRANDOM', 1)
else
  bcrypt_lib_dep = []
endif

if c_compiler.has_header('libcharset.h', args: header_arg)
  if c_compiler.has_function('locale_charset')
    priv_conf_data.set('HAVE_LIBCHARSET', 1)
    charset_dep = []
  else
    charset_dep = c_compiler.find_library('charset', dirs: cross_lib_path,
                                          required: false)
    if charset_dep.found() and c_compiler.has_function('locale_charset', dependencies: charset_dep)
      priv_conf_data.set('HAVE_LIBCHARSET', 1)
    else
      charset_dep = []
    endif
  endif
else
  charset_dep = []
endif

readline_dep = c_compiler.find_library('readline', dirs: cross_lib_path,
                                       required:false)

if readline_dep.found()
  pub_conf_data.set('FREECIV_HAVE_LIBREADLINE', 1)
endif

lzma_dep = c_compiler.find_library('lzma', dirs: cross_lib_path,
                                   required:false)

if lzma_dep.found()
  pub_conf_data.set('FREECIV_HAVE_LIBLZMA', 1)
endif

zstd_dep = c_compiler.find_library('zstd', dirs: cross_lib_path,
                                   required:false)

if zstd_dep.found()
  pub_conf_data.set('FREECIV_HAVE_LIBZSTD', 1)
endif

syslua = get_option('syslua')
lua_dep_tmp = dependency('lua-5.4', required:false)

if syslua != 'false' and lua_dep_tmp.found()
  lua_inc_path = []
  lua_sources = []
  lua_dep = lua_dep_tmp
elif syslua == 'true'
  error('Syslua requested but not found.')
else
  lua_inc_path = 'dependencies/lua-5.4/src'
  lua_sources = [
    'dependencies/lua-5.4/src/lapi.c',
    'dependencies/lua-5.4/src/lauxlib.c',
    'dependencies/lua-5.4/src/lbaselib.c',
    'dependencies/lua-5.4/src/lcode.c',
    'dependencies/lua-5.4/src/lcorolib.c',
    'dependencies/lua-5.4/src/lctype.c',
    'dependencies/lua-5.4/src/ldblib.c',
    'dependencies/lua-5.4/src/ldebug.c',
    'dependencies/lua-5.4/src/ldo.c',
    'dependencies/lua-5.4/src/ldump.c',
    'dependencies/lua-5.4/src/lfunc.c',
    'dependencies/lua-5.4/src/lgc.c',
    'dependencies/lua-5.4/src/linit.c',
    'dependencies/lua-5.4/src/liolib.c',
    'dependencies/lua-5.4/src/llex.c',
    'dependencies/lua-5.4/src/lmathlib.c',
    'dependencies/lua-5.4/src/lmem.c',
    'dependencies/lua-5.4/src/loadlib.c',
    'dependencies/lua-5.4/src/lobject.c',
    'dependencies/lua-5.4/src/lopcodes.c',
    'dependencies/lua-5.4/src/loslib.c',
    'dependencies/lua-5.4/src/lparser.c',
    'dependencies/lua-5.4/src/lstate.c',
    'dependencies/lua-5.4/src/lstring.c',
    'dependencies/lua-5.4/src/lstrlib.c',
    'dependencies/lua-5.4/src/ltable.c',
    'dependencies/lua-5.4/src/ltablib.c',
    'dependencies/lua-5.4/src/ltm.c',
    'dependencies/lua-5.4/src/lundump.c',
    'dependencies/lua-5.4/src/lutf8lib.c',
    'dependencies/lua-5.4/src/lvm.c',
    'dependencies/lua-5.4/src/lzio.c',
  ]
  lua_dep = dependency('', required:false)
endif

if c_compiler.compiles('''#include <netinet/in.h>
int main(void) { struct ip_mreqn req; req.imr_ifindex = 0; return 0; }''')
  priv_conf_data.set('HAVE_IP_MREQN', 1)
endif

if c_compiler.has_function('iconv', args: header_arg)
  priv_conf_data.set('HAVE_ICONV', 1)
  iconv_lib_dep = []
elif c_compiler.has_header_symbol('iconv.h', 'iconv', args: header_arg)
  priv_conf_data.set('HAVE_ICONV', 1)
  iconv_lib_dep = c_compiler.find_library('iconv', dirs: cross_lib_path)
else
  iconv_lib_dep = []
endif

if c_compiler.compiles('''#include <stddef.h>
#include <iconv.h>
int main(void) { iconv_t cd; const char **c; iconv(cd, c, NULL, NULL); return 0; }''')
  priv_conf_data.set('ICONV_CONST', 'const')
else
  priv_conf_data.set('ICONV_CONST', '')
endif

if get_option('cacert-path') != ''
  priv_conf_data.set('CUSTOM_CACERT_PATH',
                     '"' + get_option('cacert-path') + '"')
endif

potential_size_t_formats = [ '%zu', '%ld', '%lld', '%I64d', '%I32d' ]

foreach format : potential_size_t_formats
  if c_compiler.compiles('''#include <stdio.h>
#if defined(__GNUC__)
void fr(const char *form, ...)
  __attribute__((__format__(__printf__, 1, 2)));
#else
#define fr(_a_,_b_) printf(_a_,_b_)
#endif
int main(void) { size_t var = 0; fr("''' + format + '''", var); return 0; }''',
  args: ['-Werror', '-Wall'])
    priv_conf_data.set('SIZE_T_PRINTF', '"' + format + '"')
    break
  endif
endforeach

if get_option('audio') or get_option('clients').contains('sdl2')
  if host_system == 'windows'
    sdl2main_dep = [c_compiler.find_library('libmingw32', dirs: cross_lib_path),
                    c_compiler.find_library('libSDL2main', dirs: cross_lib_path),
                    c_compiler.find_library('libSDL2', dirs: cross_lib_path)]
  else
    sdl2main_dep = [c_compiler.find_library('libSDL2', dirs: cross_lib_path)]
  endif
endif

if get_option('qtver') == 'qt5'
  priv_conf_data.set('FC_QT5_MODE', 1)
endif

if get_option('audio')
  priv_conf_data.set('AUDIO_SDL', 1)

  audio_sdl2_dep = c_compiler.find_library('libSDL2_mixer',
                                           dirs: cross_lib_path)
  audio_dep = [sdl2main_dep, audio_sdl2_dep]
else
  audio_dep = []
endif

if get_option('json-protocol')
  if not c_compiler.has_header('jansson.h')
    error('json-protocol requires jansson.h header, but it is not found')
  endif
  pub_conf_data.set('FREECIV_JSON_CONNECTION', 1)
  jansson_dep = c_compiler.find_library('jansson')
else
  jansson_dep = []
endif

if get_option('nls')
  pub_conf_data.set('FREECIV_ENABLE_NLS', 1)
  priv_conf_data.set('ENABLE_NLS', 1)
  gettext_dep = c_compiler.find_library('intl', dirs: cross_lib_path,
                                        required: false)
else
  gettext_dep = []
endif

sqlite3_dep = c_compiler.find_library('libsqlite3', dirs: cross_lib_path)
m_dep = c_compiler.find_library('m', dirs: cross_lib_path)

if get_option('gitrev')
  priv_conf_data.set('GITREV', 1)
endif

configure_file(input : 'gen_headers/meson_fc_config.h.in',
               output : 'fc_config.h',
               configuration: priv_conf_data)
configure_file(input : 'gen_headers/meson_freeciv_config.h.in',
               output : 'freeciv_config.h',
               configuration: pub_conf_data)
configure_file(input : 'gen_headers/meson_liblua_config.h.in',
               output : 'liblua_config.h',
               configuration: liblua_conf_data)

add_global_arguments('-DHAVE_CONFIG_H',
                     language: ['c', 'cpp'])

if get_option('nls')
  subdir('translations/core')
  subdir('translations/nations')
  subdir('translations/ruledit')
endif

python_exe = find_program('python3')
sh_exe = find_program('sh')

common_inc = include_directories(cross_inc_path,
  lua_inc_path, 'dependencies/luasql/src', 'dependencies/tinycthread',
  'dependencies/tolua-5.2/include', 'dependencies/cvercmp',
  'utility', 'common', 'common/networking', 'common/scriptcore',
  'common/aicore')

server_inc = [common_inc, include_directories('server', 'server/advisors',
              'server/scripting', 'server/generator', 'server/savegame',
              'ai', 'ai/classic')]

client_inc = [common_inc, include_directories('client', 'client/include',
              'client/luascript', 'client/agents')]

tool_inc = [server_inc, include_directories('tools/ruleutil', 'tools/shared')]

runwrap = custom_target('runwrap', output: 'run.sh',
              command: [sh_exe, files('bootstrap/generate_meson_run.sh'), '@OUTPUT@'],
              depend_files: files('bootstrap/generate_meson_run.sh'),
              build_by_default: true)

verhdr = custom_target('verhdr', output: 'version_gen.h',
              command: [sh_exe, files('gen_headers/generate_version_header.sh'), '@OUTPUT@'],
              depend_files: files('fc_version'))

ls_core = custom_target('langstat_core', output: 'langstat_core.txt',
              command: [sh_exe, files('bootstrap/generate_langstat.sh'), 'core',
                        meson.project_source_root(), meson.project_build_root()])

specenum = custom_target('specenum_gen.h', output: 'specenum_gen.h',
                         command: [python_exe, files('utility/generate_specenum.py'),
                                   '@OUTPUT@'])

pack_common = custom_target('packets_common', output: ['packets_gen.h',
                                                       'packets_gen.c'],
                            command: [python_exe, files('common/generate_packets.py'),
                                      '@OUTPUT0@', '@OUTPUT1@', '', '', '', ''],
                            depend_files: files('common/networking/packets.def'))

pack_server = custom_target('packets_server', output: ['hand_gen.h', 'hand_gen.c'],
                            command: [python_exe, files('common/generate_packets.py'),
                                      '', '', '', '', '@OUTPUT0@', '@OUTPUT1@'],
                            depend_files: files('common/networking/packets.def'))

pack_client = custom_target('packets_client', output: ['packhand_gen.h',
                                                       'packhand_gen.c'],
                            command: [python_exe, files('common/generate_packets.py'),
                                      '', '', '@OUTPUT0@', '@OUTPUT1@', '', ''],
                            depend_files: files('common/networking/packets.def'))

gitrev = custom_target('gitrev', output: 'fc_gitrev_gen.h',
                       command: [sh_exe, files('bootstrap/generate_gitrev.sh'),
                       meson.project_source_root(), '@OUTPUT@'],
                       build_by_default: get_option('gitrev'))

if host_system == 'windows'
  tinycthr_files = files('dependencies/tinycthread/fc_tinycthread.c')
else
  tinycthr_files = files()
endif

fc_deps = static_library('fc_dependencies',
  'dependencies/cvercmp/cvercmp.c',
  lua_sources,
  'dependencies/tolua-5.2/src/lib/tolua_event.c',
  'dependencies/tolua-5.2/src/lib/tolua_is.c',
  'dependencies/tolua-5.2/src/lib/tolua_map.c',
  'dependencies/tolua-5.2/src/lib/tolua_push.c',
  'dependencies/tolua-5.2/src/lib/tolua_to.c',
  'dependencies/luasql/src/luasql.c',
  'dependencies/luasql/src/ls_sqlite3.c',
  tinycthr_files,
  sources: [verhdr],
  include_directories : common_inc,
  dependencies: lua_dep
  )

if meson.is_cross_build()
  tolua_cmd = find_program('tolua')
else
  tolua_cmd = executable('tolua',
    'dependencies/tolua-5.2/src/bin/tolua.c',
    'dependencies/tolua-5.2/src/bin/toluabind.c',
    include_directories : ['dependencies/lua-5.4/src',
                           'dependencies/tolua-5.2/include'],
    link_with: fc_deps,
    dependencies: m_dep
  )
endif

tolua = generator(tolua_cmd,
                  arguments: ['-n', '@BASENAME@',
                              '-o', '@BUILD_DIR@/@BASENAME@_gen.c',
                              '-H', '@BUILD_DIR@/@BASENAME@_gen.h',
                              '@INPUT@'],
                  output: [ '@BASENAME@_gen.c', '@BASENAME@_gen.h'])

tolua_com_a = custom_target('tolua_custom_a',
                            output: ['tolua_common_a_gen.c',
                                     'tolua_common_a_gen.h'],
                            command: [tolua_cmd, '-n', 'common_a',
                                      '-o', '@OUTPUT0@',
                                      '-H', '@OUTPUT1@',
                                      files('common/scriptcore/tolua_common_a.pkg')])

tolua_com_z = custom_target('tolua_custom_z',
                            output: ['tolua_common_z_gen.c',
                                     'tolua_common_z_gen.h'],
                            command: [tolua_cmd, '-n', 'common_z',
                                      '-o', '@OUTPUT0@',
                                      '-H', '@OUTPUT1@',
                                      files('common/scriptcore/tolua_common_z.pkg')])

tolua_game = custom_target('tolua_game',
                           output: ['tolua_game_gen.c',
                                    'tolua_game_gen.h'],
                           command: [tolua_cmd, '-n', 'game',
                                     '-o', '@OUTPUT0@',
                                     '-H', '@OUTPUT1@',
                                     files('common/scriptcore/tolua_game.pkg')])

tolua_signal = custom_target('tolua_signal',
                             output: ['tolua_signal_gen.c',
                                      'tolua_signal_gen.h'],
                             command: [tolua_cmd, '-n', 'signal',
                                       '-o', '@OUTPUT0@',
                                       '-H', '@OUTPUT1@',
                                       files('common/scriptcore/tolua_signal.pkg')])

if host_system == 'windows'
  windres_cmd = find_program('x86_64-w64-mingw32-windres', 'windres')
  windres = generator(windres_cmd,
                      arguments: ['-I', '@SRC_DIR@/windows',
                                  '-i', '@INPUT@',
                                  '-o', '@OUTPUT@'],
                      output: '@BASENAME@.o')
  clienticon = windres.process('windows/clienticon.rc')
  mpicon = windres.process('windows/mpicon.rc')
  rulediticon = windres.process('windows/rulediticon.rc')
  servericon = windres.process('windows/servericon.rc')
else
  clienticon = []
  mpicon = []
  rulediticon = []
  servericon = []
endif

common_lib = library('freeciv',
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
  'utility/randseed.c',
  'utility/registry.c',
  'utility/registry_ini.c',
  'utility/registry_xml.c',
  'utility/section_file.c',
  'utility/shared.c',
  'utility/string_vector.c',
  'utility/support.c',
  'utility/timing.c',
  'common/aicore/aiactions.c',
  'common/aicore/aisupport.c',
  'common/aicore/caravan.c',
  'common/aicore/citymap.c',
  'common/aicore/cm.c',
  'common/aicore/path_finding.c',
  'common/aicore/pf_tools.c',
  'common/networking/connection.c',
  'common/networking/dataio_json.c',
  'common/networking/dataio_raw.c',
  'common/networking/packets.c',
  'common/networking/packets_json.c',
  'common/scriptcore/api_common_intl.c',
  'common/scriptcore/api_common_utilities.c',
  'common/scriptcore/api_game_effects.c',
  'common/scriptcore/api_game_find.c',
  'common/scriptcore/api_game_methods.c',
  'common/scriptcore/api_game_specenum.c',
  'common/scriptcore/api_signal_base.c',
  'common/scriptcore/luascript.c',
  'common/scriptcore/luascript_func.c',
  'common/scriptcore/luascript_signal.c',
  'common/achievements.c',
  'common/actions.c',
  'common/ai.c',
  'common/base.c',
  'common/borders.c',
  'common/calendar.c',
  'common/capstr.c',
  'common/citizens.c',
  'common/city.c',
  'common/clientutils.c',
  'common/combat.c',
  'common/culture.c',
  'common/diptreaty.c',
  'common/disaster.c',
  'common/effects.c',
  'common/events.c',
  'common/extras.c',
  'common/fc_cmdhelp.c',
  'common/fc_interface.c',
  'common/featured_text.c',
  'common/game.c',
  'common/government.c',
  'common/idex.c',
  'common/improvement.c',
  'common/map.c',
  'common/mapimg.c',
  'common/metaknowledge.c',
  'common/movement.c',
  'common/multipliers.c',
  'common/nation.c',
  'common/player.c',
  'common/reqtext.c',
  'common/requirements.c',
  'common/research.c',
  'common/rgbcolor.c',
  'common/road.c',
  'common/server_settings.c',
  'common/spaceship.c',
  'common/specialist.c',
  'common/style.c',
  'common/team.c',
  'common/tech.c',
  'common/terrain.c',
  'common/tile.c',
  'common/traderoutes.c',
  'common/unit.c',
  'common/unitlist.c',
  'common/unittype.c',
  'common/version.c',
  'common/victory.c',
  'common/vision.c',
  'common/workertask.c',
  'common/worklist.c',
  include_directories : common_inc,
  sources: [specenum, pack_common,
            tolua_com_a, tolua_com_z, tolua_game, tolua_signal],
  link_whole: fc_deps,
  dependencies: [c_compiler.find_library('libicuuc', dirs: cross_lib_path),
                 c_compiler.find_library('z', dirs: cross_lib_path),
                 c_compiler.find_library('libcurl', dirs: cross_lib_path),
                 m_dep, sqlite3_dep,
                 net_dep, jansson_dep, lua_dep, lzma_dep, zstd_dep,
                 bcrypt_lib_dep, iconv_lib_dep,
                 gettext_dep, charset_dep,
                 dependency('threads')],
  install : true
  )

ais = static_library('fc_ai',
  'ai/classic/classicai.c',
  'ai/tex/texai.c',
  'ai/tex/texaicity.c',
  'ai/tex/texaimsg.c',
  'ai/tex/texaiplayer.c',
  'ai/tex/texaiworld.c',
  'ai/default/aiair.c',
  'ai/default/aidata.c',
  'ai/default/aidiplomat.c',
  'ai/default/aiferry.c',
  'ai/default/aiguard.c',
  'ai/default/aihand.c',
  'ai/default/aihunt.c',
  'ai/default/ailog.c',
  'ai/default/aiparatrooper.c',
  'ai/default/aiplayer.c',
  'ai/default/aitech.c',
  'ai/default/aitools.c',
  'ai/default/aiunit.c',
  'ai/default/daiactions.c',
  'ai/default/daicity.c',
  'ai/default/daidiplomacy.c',
  'ai/default/daidomestic.c',
  'ai/default/daieffects.c',
  'ai/default/daimilitary.c',
  'ai/default/daisettler.c',
  sources: [verhdr],
  include_directories: [server_inc, include_directories('ai/default')]
  )

server_lib = static_library('fc_server',
  'ai/aitraits.c',
  'ai/difficulty.c',
  'ai/handicaps.c',
  'server/advisors/advbuilding.c',
  'server/advisors/advchoice.c',
  'server/advisors/advcity.c',
  'server/advisors/advdata.c',
  'server/advisors/advgoto.c',
  'server/advisors/advruleset.c',
  'server/advisors/advspace.c',
  'server/advisors/advtools.c',
  'server/advisors/autoexplorer.c',
  'server/advisors/autosettlers.c',
  'server/advisors/infracache.c',
  'server/generator/fracture_map.c',
  'server/generator/height_map.c',
  'server/generator/mapgen.c',
  'server/generator/mapgen_topology.c',
  'server/generator/mapgen_utils.c',
  'server/generator/startpos.c',
  'server/generator/temperature_map.c',
  'server/savegame/savecompat.c',
  'server/savegame/savegame2.c',
  'server/savegame/savegame3.c',
  'server/savegame/savemain.c',
  'server/scripting/api_fcdb_auth.c',
  'server/scripting/api_fcdb_base.c',
  'server/scripting/api_server_base.c',
  'server/scripting/api_server_edit.c',
  'server/scripting/api_server_game_methods.c',
  'server/scripting/api_server_luadata.c',
  'server/scripting/api_server_notify.c',
  'server/scripting/script_fcdb.c',
  'server/scripting/script_server.c',
  'server/actiontools.c',
  'server/aiiface.c',
  'server/animals.c',
  'server/auth.c',
  'server/barbarian.c',
  'server/citizenshand.c',
  'server/cityhand.c',
  'server/citytools.c',
  'server/cityturn.c',
  'server/commands.c',
  'server/connecthand.c',
  'server/console.c',
  'server/diplhand.c',
  'server/diplomats.c',
  'server/edithand.c',
  'server/fcdb.c',
  'server/gamehand.c',
  'server/handchat.c',
  'server/infrapts.c',
  'server/maphand.c',
  'server/meta.c',
  'server/mood.c',
  'server/notify.c',
  'server/plrhand.c',
  'server/report.c',
  'server/rscompat.c',
  'server/rssanity.c',
  'server/ruleset.c',
  'server/sanitycheck.c',
  'server/score.c',
  'server/sernet.c',
  'server/settings.c',
  'server/spacerace.c',
  'server/srv_log.c',
  'server/srv_main.c',
  'server/stdinhand.c',
  'server/techtools.c',
  'server/unithand.c',
  'server/unittools.c',
  'server/voting.c',
  include_directories: server_inc,
  sources: [ pack_server,
             tolua.process('server/scripting/tolua_fcdb.pkg',
                           'server/scripting/tolua_server.pkg')],
  dependencies: lua_dep
  )

executable(server_binary_name,
  'server/civserver.c',
  servericon,
  include_directories: server_inc,
  link_with: [server_lib, common_lib, ais],
  dependencies: [m_dep, net_dep, readline_dep, gettext_dep],
  install: true
  )

install_data(
  'lua/database.lua',
  install_dir : join_paths(get_option('sysconfdir'), 'freeciv')
  )

install_data(
  'data/ruledit/comments-3.1.txt',
  install_dir : join_paths(get_option('datadir'), 'freeciv/ruledit')
  )

nations = [
  'abkhaz',
  'aborigines',
  'abyssinian',
  'acadian',
  'acehnese',
  'acrean',
  'afghani',
  'african',
  'ainu',
  'akwe',
  'alandalus',
  'alander',
  'albanian',
  'aleut',
  'algerian',
  'alsatian',
  'amazigh',
  'amazonian',
  'american',
  'andorran',
  'angolan',
  'anhaltian',
  'animals',
  'anishinaabe',
  'antarctican',
  'antiguan',
  'antillean',
  'apache',
  'arab',
  'aragonese',
  'aramean',
  'argentine',
  'armenian',
  'ashanti',
  'assamese',
  'assyrian',
  'asturian',
  'atlantean',
  'australian',
  'austrian',
  'avar',
  'aymara',
  'azeri',
  'aztec',
  'babylonian',
  'badian',
  'bahamian',
  'bahraini',
  'bangladeshi',
  'barbadian',
  'barbarian',
  'bashkir',
  'basque',
  'bavarian',
  'belarusian',
  'belgian',
  'belgic',
  'belizean',
  'bengali',
  'beninese',
  'benin',
  'bhutanese',
  'biafran',
  'bissauguinean',
  'boer',
  'boian',
  'bolivian',
  'bosnia',
  'bosporan',
  'botswanan',
  'brandenburgian',
  'brazilian',
  'breton',
  'british',
  'briton',
  'bruneian',
  'bulgarian',
  'burgundian',
  'burgundic',
  'burkinabe',
  'burmese',
  'burundi',
  'buryat',
  'byzantium',
  'californian',
  'cambodian',
  'cameroonian',
  'canadian',
  'canari',
  'cantonese',
  'capeverdean',
  'carantanian',
  'carthaginian',
  'castilian',
  'catalan',
  'celtiberian',
  'celtic',
  'centralafrican',
  'centralamerican',
  'centrallithuanian',
  'chadian',
  'cham',
  'chananean',
  'chechen',
  'cherokee',
  'chiapanec',
  'chickasaw',
  'chilean',
  'chimu',
  'chinese',
  'chinook',
  'choctaw',
  'chola',
  'chrobatian',
  'chumash',
  'chuvash',
  'circassian',
  'colombian',
  'comanche',
  'comorian',
  'confederate',
  'congolesebrazzaville',
  'congolese',
  'cornish',
  'corsican',
  'cossack',
  'costarican',
  'cree',
  'cretan',
  'crimeantatar',
  'croatian',
  'crusader',
  'cuban',
  'curonian',
  'cuyavian',
  'cypriot',
  'cyrenaican',
  'czechoslovak',
  'czech',
  'dacian',
  'dahomean',
  'danish',
  'darfuri',
  'djiboutian',
  'dominicano',
  'dominican',
  'dryad',
  'dutch',
  'eastgerman',
  'easttimorese',
  'ecuadorian',
  'egyptianarab',
  'egyptian',
  'elamite',
  'emirati',
  'english',
  'epirote',
  'equatoguinean',
  'eritrean',
  'esperant',
  'estonian',
  'ethiopian',
  'etruscan',
  'european',
  'evenki',
  'faroese',
  'fijian',
  'filipino',
  'finnish',
  'flemish',
  'florentine',
  'formosan',
  'franconian',
  'frankish',
  'french',
  'frisian',
  'friulian',
  'fulani',
  'gabonese',
  'gaelic',
  'galician',
  'gallic',
  'gambian',
  'genoese',
  'georgian',
  'gepid',
  'germanbelgian',
  'germanic',
  'german',
  'ghanaian',
  'ghana',
  'ghaznavid',
  'gokturk',
  'goldenhorde',
  'gothic',
  'greaterpolish',
  'greek',
  'greenlander',
  'grenadian',
  'guanche',
  'guarani',
  'guatemalan',
  'guinean',
  'gupta',
  'guyanese',
  'hacker',
  'haitian',
  'hanoverian',
  'han',
  'hansa',
  'hasinay',
  'hawaiian',
  'hellenic',
  'helvetian',
  'hephthalite',
  'hessian',
  'himyarite',
  'hittite',
  'holyroman',
  'holysee',
  'honduran',
  'hopi',
  'hungarian',
  'hunnic',
  'iberian',
  'icelandic',
  'illyrian',
  'inca',
  'indian',
  'indoeuropean',
  'indonesian',
  'inuit',
  'iranian',
  'iraqi',
  'irish',
  'iroquois',
  'israeli',
  'israelite',
  'italiangreek',
  'italian',
  'ivoirian',
  'jaffna',
  'jamaican',
  'japanese',
  'jolof',
  'jordanian',
  'kalmyk',
  'kanem-bornu',
  'karabakhi',
  'karelian',
  'karen',
  'kashmiri',
  'kashubian',
  'katangan',
  'kazakh',
  'kenyan',
  'khazar',
  'khmer',
  'khoisan',
  'khwarezmian',
  'kiribati',
  'kittitian',
  'komi',
  'kongo',
  'korean',
  'kosovar',
  'kuna',
  'kurd',
  'kushan',
  'kuwaiti',
  'kyrgyz',
  'langobardic',
  'lankese',
  'laotian',
  'latin',
  'latvian',
  'lebanese',
  'lendian',
  'leonese',
  'lesothoan',
  'liberian',
  'liburnian',
  'libyan',
  'liechtensteiner',
  'ligurian',
  'lipkatatar',
  'lippe',
  'lithuanian',
  'lojbanistani',
  'lorrain',
  'luwian',
  'luxembourgish',
  'lycian',
  'maasai',
  'macedonian',
  'macedon',
  'majapahit',
  'malagasy',
  'malawian',
  'malaysian',
  'maldivian',
  'malian',
  'mali',
  'maltese',
  'mamluk',
  'manchu',
  'manx',
  'maori',
  'mapuche',
  'marathi',
  'marshallese',
  'martian',
  'mauritanian',
  'mauritian',
  'mayan',
  'mazovian',
  'mecklenburgian',
  'median',
  'messapian',
  'metis',
  'mexican',
  'miao',
  'micronesian',
  'mikmaq',
  'milanese',
  'miskito',
  'mitanni',
  'mixtec',
  'moldovan',
  'moluccan',
  'monegasque',
  'mongol',
  'mon',
  'montenegrin',
  'moravian',
  'mordvin',
  'moroccan',
  'mozambican',
  'mughal',
  'muscovite',
  'muskogee',
  'mwiska',
  'namibian',
  'nauruan',
  'navajo',
  'neapolitan',
  'nenets',
  'nepali',
  'nestorian',
  'newfoundland',
  'newzealand',
  'nicaraguan',
  'nigerian',
  'nigerien',
  'nimiipuu',
  'norman',
  'northkorean',
  'northumbrian',
  'norwegian',
  'novgorodian',
  'nubian',
  'numidian',
  'nuu-chah-nulth',
  'occitan',
  'ohlone',
  'oldenburgian',
  'oldprussian',
  'omani',
  'ossetian',
  'ostrogothic',
  'otomi',
  'ottoman',
  'ozite',
  'paeonian',
  'pakistani',
  'palatinate',
  'palestinian',
  'palmyrene',
  'panamanian',
  'papuanewguinean',
  'papuan',
  'paraguayan',
  'parthian',
  'pashtun',
  'pelasgian',
  'persian',
  'peruvian',
  'phoenician',
  'phrygian',
  'pictish',
  'piedmontese',
  'pirate',
  'polish',
  'polynesian',
  'pontic',
  'portuguese',
  'prussian',
  'puertorican',
  'purhepecha',
  'qatari',
  'quebecois',
  'rapanui',
  'raramuri',
  'rhenish',
  'riffian',
  'riograndense',
  'romanian',
  'roman',
  'romansh',
  'russian',
  'rusyn',
  'ruthenian',
  'rwandan',
  'ryukyuan',
  'sabine',
  'sahrawi',
  'saintlucian',
  'saka',
  'salishan',
  'salvadoran',
  'sami',
  'sammarinese',
  'samnite',
  'samoan',
  'samogitian',
  'santomean',
  'sardinian',
  'sarmatian',
  'saudi',
  'savoyard',
  'saxon',
  'scanian',
  'schleswig-holsteinian',
  'scottishgaelic',
  'scottish',
  'scythian',
  'seleucid',
  'seljuk',
  'seminole',
  'senegalese',
  'serbian',
  'seychellois',
  'shan',
  'shawnee',
  'sherpa',
  'siberian',
  'sicilian',
  'sierraleonean',
  'sikh',
  'sikkimese',
  'silesian',
  'singaporean',
  'singlebarbarian',
  'sinhalese',
  'sioux',
  'slavic',
  'slovakian',
  'slovenian',
  'solomonislander',
  'somaliland',
  'somali',
  'songhai',
  'sorbian',
  'sotho',
  'southafrican',
  'southamerican',
  'southkorean',
  'southsudanese',
  'southvietnamese',
  'southyemeni',
  'soviet',
  'spanish',
  'srivijaya',
  'sudanese',
  'suebian',
  'sumerian',
  'surinamese',
  'swahili',
  'swazi',
  'swedish',
  'swiss',
  'syrian',
  'tahitian',
  'taino',
  'tairona',
  'taiwanese',
  'tajik',
  'tanganyikan',
  'tanzanian',
  'tatar',
  'templar',
  'teutonic',
  'texan',
  'thai',
  'thracian',
  'thuringian',
  'tibetan',
  'timurid',
  'tocharian',
  'togolese',
  'toltec',
  'tongan',
  'transnistrian',
  'transylvanian',
  'trinidadian',
  'tswana',
  'tuareg',
  'tunisian',
  'tupi',
  'turkishcypriot',
  'turkmen',
  'turk',
  'tuvan',
  'tyrolian',
  'ugandan',
  'ukrainian',
  'un',
  'urartian',
  'uruguayan',
  'uyghur',
  'uzbek',
  'vampire',
  'vandalic',
  'vanuatuan',
  'vedic',
  'veletian',
  'venda',
  'venetian',
  'venetic',
  'venezuelan',
  'vermont',
  'vietnamese',
  'viking',
  'vincentian',
  'visigothic',
  'vistulan',
  'volapuk',
  'volgabulgar',
  'volgagerman',
  'walloon',
  'welsh',
  'werewolf',
  'westernpomeranian',
  'westernroman',
  'westindian',
  'westphalian',
  'wuerttembergian',
  'xhosa',
  'xiongnu',
  'yakut',
  'yemeni',
  'yucatecan',
  'yugoslav',
  'zambian',
  'zanzibari',
  'zapotec',
  'zhuang',
  'zimbabwean',
  'zulu'
  ]

if get_option('clients') != []

client_common = static_library('fc_client_common',
  'client/agents/agents.c',
  'client/agents/cma_core.c',
  'client/agents/cma_fec.c',
  'client/agents/sha.c',
  'client/luascript/api_client_base.c',
  'client/luascript/script_client.c',
  'client/attribute.c',
  'client/audio.c',
  'client/audio_none.c',
  'client/audio_sdl.c',
  'client/chatline_common.c',
  'client/citydlg_common.c',
  'client/cityrepdata.c',
  'client/client_main.c',
  'client/climap.c',
  'client/climisc.c',
  'client/clinet.c',
  'client/colors_common.c',
  'client/connectdlg_common.c',
  'client/control.c',
  'client/editor.c',
  'client/global_worklist.c',
  'client/goto.c',
  'client/helpdata.c',
  'client/luaconsole_common.c',
  'client/mapctrl_common.c',
  'client/mapview_common.c',
  'client/messagewin_common.c',
  'client/music.c',
  'client/options.c',
  'client/overview_common.c',
  'client/packhand.c',
  'client/plrdlg_common.c',
  'client/repodlgs_common.c',
  'client/reqtree.c',
  'client/servers.c',
  'client/text.c',
  'client/themes_common.c',
  'client/tilespec.c',
  'client/unitselect_common.c',
  'client/update_queue.c',
  'client/voteinfo.c',
  'client/zoom.c',
  sources: [pack_client,
            tolua.process('client/luascript/tolua_client.pkg')],
  include_directories: client_inc,
  link_with: [common_lib],
  dependencies: [m_dep, audio_dep, lua_dep]
  )

install_data('data/helpdata.txt',
             'data/cimpletoon.tilespec',
             'data/isophex.tilespec',
             'data/isotrident.tilespec',
             'data/toonhex.tilespec',
             'data/alio.tilespec',
             'data/trident.tilespec',
             'data/hexemplio.tilespec',
             'data/hex2t.tilespec',
             'data/amplio2.tilespec',
             'data/stdmusic.musicspec',
             'data/stdsounds.soundspec',
             install_dir : join_paths(get_option('datadir'), 'freeciv'))

install_data('data/misc/colors.tilespec',
             'data/misc/intro.png',
             'data/misc/overlays.png',
             'data/misc/overlays.spec',
             'data/misc/citybar.png',
             'data/misc/citybar.spec',
             'data/misc/small.png',
             'data/misc/small.spec',
             'data/misc/governments.png',
             'data/misc/governments.spec',
             'data/misc/specialists.png',
             'data/misc/specialists.spec',
             'data/misc/events.png',
             'data/misc/events.spec',
             'data/misc/buildings.png',
             'data/misc/buildings.spec',
             'data/misc/buildings-large.spec',
             'data/misc/wonders-large.spec',
             'data/misc/flags.spec',
             'data/misc/flags-large.spec',
             'data/misc/shields.spec',
             'data/misc/shields-large.spec',
             'data/misc/cursors.png',
             'data/misc/cursors.spec',
             'data/misc/space.png',
             'data/misc/space.spec',
             'data/misc/techs.png',
             'data/misc/techs.spec',
             'data/misc/treaty.png',
             'data/misc/treaty.spec',
             'data/misc/icons.spec',
             'data/misc/editor.png',
             'data/misc/editor.spec',
             'data/misc/civicon.png',
             'data/misc/cityicon.png',
             install_dir : join_paths(get_option('datadir'), 'freeciv/misc'))

install_data('data/buildings/airport.png',
             'data/buildings/aqueduct.png',
             'data/buildings/bank.png',
             'data/buildings/barracks_iii.png',
             'data/buildings/barracks_ii.png',
             'data/buildings/barracks_i.png',
             'data/buildings/capitalization.png',
             'data/buildings/cathedral.png',
             'data/buildings/city_walls.png',
             'data/buildings/coastal_defense.png',
             'data/buildings/colosseum.png',
             'data/buildings/courthouse.png',
             'data/buildings/ecclesiastical_palace.png',
             'data/buildings/factory.png',
             'data/buildings/granary.png',
             'data/buildings/harbor.png',
             'data/buildings/hydro_plant.png',
             'data/buildings/library.png',
             'data/buildings/marketplace.png',
             'data/buildings/mass_transit.png',
             'data/buildings/mfg_plant.png',
             'data/buildings/nuclear_plant.png',
             'data/buildings/offshore_platform.png',
             'data/buildings/palace.png',
             'data/buildings/police_station.png',
             'data/buildings/port_facility.png',
             'data/buildings/power_plant.png',
             'data/buildings/recycling_center.png',
             'data/buildings/research_lab.png',
             'data/buildings/sam_battery.png',
             'data/buildings/sdi_defense.png',
             'data/buildings/sewer_system.png',
             'data/buildings/solar_plant.png',
             'data/buildings/space_component.png',
             'data/buildings/space_modules.png',
             'data/buildings/space_structural.png',
             'data/buildings/stock_exchange.png',
             'data/buildings/super_highways.png',
             'data/buildings/supermarket.png',
             'data/buildings/temple.png',
             'data/buildings/university.png',
             install_dir : join_paths(get_option('datadir'),
                                      'freeciv/buildings'))

install_data('data/scenarios/british-isles.sav',
             'data/scenarios/earth-large.sav',
             'data/scenarios/earth-small.sav',
             'data/scenarios/europe.sav',
             'data/scenarios/europe_1900_WWI.sav',
             'data/scenarios/france.sav',
             'data/scenarios/hagworld.sav',
             'data/scenarios/iberian-peninsula.sav',
             'data/scenarios/italy.sav',
             'data/scenarios/japan.sav',
             'data/scenarios/north_america.sav',
             'data/scenarios/tutorial.sav',
             install_dir : join_paths(get_option('datadir'), 'freeciv/scenarios'))

install_data('data/wonders/apollo_program.png',
             'data/wonders/asmiths_trading_co.png',
             'data/wonders/colossus.png',
             'data/wonders/copernicus_observatory.png',
             'data/wonders/cure_for_cancer.png',
             'data/wonders/darwins_voyage.png',
             'data/wonders/eiffel_tower.png',
             'data/wonders/great_library.png',
             'data/wonders/great_wall.png',
             'data/wonders/hanging_gardens.png',
             'data/wonders/hoover_dam.png',
             'data/wonders/internet.png',
             'data/wonders/isaac_newtons_college.png',
             'data/wonders/js_bachs_cathedral.png',
             'data/wonders/king_richards_crusade.png',
             'data/wonders/leonardos_workshop.png',
             'data/wonders/lighthouse.png',
             'data/wonders/magellans_expedition.png',
             'data/wonders/manhattan_project.png',
             'data/wonders/marco_polos_embassy.png',
             'data/wonders/mausoleum_of_halicarnassus.png',
             'data/wonders/michelangelos_chapel.png',
             'data/wonders/oracle.png',
             'data/wonders/pyramids.png',
             'data/wonders/seti_program.png',
             'data/wonders/shakespeares_theatre.png',
             'data/wonders/statue_of_liberty.png',
             'data/wonders/statue_of_zeus.png',
             'data/wonders/sun_tzus_war_academy.png',
             'data/wonders/temple_of_artemis.png',
             'data/wonders/united_nations.png',
             'data/wonders/womens_suffrage.png',
             install_dir : join_paths(get_option('datadir'),
                                      'freeciv/wonders'))

flags = [
  'abkhazia',
  'aborigines',
  'acadia',
  'aceh',
  'acre',
  'adygea',
  'afghanistan',
  'africa',
  'ainu',
  'akwe',
  'aland',
  'albania',
  'aleut',
  'algeria',
  'almohad',
  'alsace',
  'amazigh',
  'amazon',
  'andorra',
  'angola',
  'animals',
  'anhalt',
  'anishinaabe',
  'antarctica',
  'antarctica_alt',
  'antigua_and_barbuda',
  'apache',
  'arab',
  'aragon',
  'aram',
  'argentina',
  'armenia',
  'ashanti',
  'assam',
  'assyria',
  'asturias',
  'atlantis',
  'australia',
  'austria',
  'avar',
  'aymara',
  'azerbaijan',
  'aztec',
  'babylon',
  'baden',
  'bahamas',
  'bahrain',
  'bangladesh',
  'barbados',
  'barbarian',
  'bashkortostan',
  'bavarian',
  'belarus',
  'belgic',
  'belgium',
  'belize',
  'bengal',
  'benin',
  'benin_ancient',
  'bhutan',
  'biafra',
  'boer',
  'boii',
  'bolivia',
  'bophuthatswana',
  'bosnia',
  'bosporus',
  'botswana',
  'brandenburg',
  'brasil',
  'britannia',
  'brittany',
  'brunei',
  'bulgaria',
  'burgundic',
  'burgundy',
  'burkina_faso',
  'burundi',
  'buryatia',
  'byzantium',
  'caddo',
  'california',
  'cameroon',
  'canada',
  'canada_old',
  'canar',
  'cantonese',
  'cape_verde',
  'car',
  'carantanian',
  'cartago',
  'castile',
  'catalan',
  'celtiberian',
  'celtic',
  'central_america',
  'central_lithuania',
  'chad',
  'cham',
  'chananea',
  'chechnya',
  'cheyenne',
  'chiapas',
  'chickasaw',
  'chile',
  'chimu',
  'china',
  'choctaw',
  'chola',
  'chrobatian',
  'chumash',
  'chuvashia',
  'clatsop',
  'colombia',
  'comanche',
  'comoros',
  'constantine',
  'cornwall',
  'corsica',
  'cossack',
  'costa_rica',
  'courland',
  'crete',
  'crimean_tatar',
  'croatia',
  'cuba',
  'cuyavia',
  'cyprus',
  'cyrenaica',
  'czech',
  'czechoslovakia',
  'dacian',
  'dahomey',
  'darfur',
  'ddr',
  'denmark',
  'dgb',
  'djibouti',
  'dominica',
  'dominican_republic',
  'dr_congo',
  'dryad',
  'ecuador',
  'east_timor',
  'egypt_ancient',
  'egypt',
  'elam',
  'el_salvador',
  'england',
  'epirus',
  'equatorial_guinea',
  'esperanto',
  'estonia',
  'eritrea',
  'ethiopia',
  'ethiopia_old',
  'etruscan',
  'europe',
  'euskadi',
  'evenkia',
  'faroes',
  'fiji',
  'finland',
  'flanders',
  'florence',
  'formosan',
  'france_old',
  'france',
  'franconia',
  'french_polynesia',
  'frisia',
  'friuli',
  'gabon',
  'gael',
  'galicia',
  'gambia',
  'gaul',
  'georgia',
  'gepid',
  'germanic',
  'germany',
  'gokturk',
  'gothic',
  'ghana',
  'ghana_ancient',
  'ghaznavid',
  'golden_horde',
  'greater_poland',
  'greece_ancient',
  'greece',
  'greenland',
  'grenada',
  'grisons',
  'guanche',
  'guarani',
  'guatemala',
  'guinea',
  'guinea-bissau',
  'gupta',
  'guyana',
  'hacker',
  'hainan',
  'haiti',
  'han',
  'hanover',
  'hansa',
  'hawaii',
  'helvetii',
  'hephthalite',
  'hesse',
  'himyar',
  'hittite',
  'honduras',
  'hopi',
  'hre',
  'hungary',
  'hunnic',
  'iberian',
  'iceland',
  'illyria',
  'inca',
  'india',
  'indoeuropean',
  'indonesia',
  'innu',
  'iran_ancient',
  'iran',
  'iraq_old',
  'iraq',
  'ireland',
  'iroquois',
  'israel',
  'israel_ancient',
  'italian_greek',
  'italy',
  'ivory_coast',
  'jaffna',
  'jamaica',
  'japan',
  'jbonai',
  'jerusalem',
  'jolof',
  'jordan',
  'kalmykia',
  'kampuchea',
  'kanem-bornu',
  'karelia',
  'karen',
  'kashmir',
  'kashubia',
  'katanga',
  'kazakhstan',
  'keetoowah',
  'kenya',
  'khazaria',
  'khmer',
  'khoisan',
  'khwarezm',
  'kiev',
  'kiribati',
  'komi',
  'kongo',
  'korea',
  'korea_ancient',
  'kosovo',
  'kuna_yala',
  'kurd',
  'kushan',
  'kuwait',
  'kyrgyzstan',
  'labarum',
  'lombardy',
  'laos',
  'latin_empire',
  'latvia',
  'lebanon',
  'lendian',
  'leon',
  'lesotho',
  'lesotho_old',
  'liberia',
  'liburnian',
  'libya',
  'libya_old',
  'liechtenstein',
  'liguria',
  'ligurian',
  'lipkatatar',
  'lippe',
  'lithuania',
  'lorraine',
  'luna',
  'lusatia',
  'luwian',
  'luxembourg',
  'lycian',
  'maasai',
  'macedon',
  'macedonia',
  'madagascar',
  'majapahit',
  'malawi',
  'malaysia',
  'maldives',
  'mali',
  'mali_ancient',
  'malta',
  'mamluk',
  'man',
  'manchuria',
  'maori',
  'mapuche',
  'marathi',
  'mars',
  'marshall_islands',
  'mauritania',
  'mauritius',
  'maya',
  'mazovia',
  'mecklenburg',
  'median',
  'messapian',
  'metis',
  'mexico',
  'micronesia',
  'mikmaq',
  'milan',
  'miskito',
  'mitanni',
  'mixtec',
  'moldova',
  'moluccas',
  'mon',
  'monaco_alternative',
  'mongolia',
  'montenegro',
  'moravia',
  'mordovia',
  'morocco',
  'moscow',
  'mozambique',
  'mughal',
  'muskogee',
  'mwiska',
  'myanmar',
  'myanmar_old',
  'nagorno_karabakh',
  'namibia',
  'naples',
  'nato',
  'nauru',
  'navajo',
  'nenetsia',
  'nepal',
  'nestoria',
  'netherlands',
  'netherlands_antilles',
  'newfoundland',
  'newzealand',
  'nez_perce',
  'nicaragua',
  'niger',
  'nigeria',
  'north_korea',
  'northumbria',
  'norway',
  'normandy',
  'novgorod',
  'nubia',
  'numidia',
  'nunavut',
  'nuu-chah-nulth',
  'occitania',
  'ohlone',
  'oldenburg',
  'oman',
  'ossetia',
  'otomi',
  'ottoman',
  'oz',
  'paeonia',
  'pakistan',
  'palatinate',
  'palestine',
  'palmyra',
  'panama',
  'papua_newguinea',
  'paraguay',
  'parthia',
  'pashtun',
  'pelasgian',
  'peru',
  'philippines',
  'phoenicia',
  'phrygian',
  'pict',
  'piedmont',
  'pirate',
  'piratini',
  'poland',
  'polynesian',
  'pomerania',
  'portugal',
  'prusai',
  'prussia',
  'puerto_rico',
  'purhepecha',
  'qatar',
  'qing',
  'quebec',
  'rapa_nui',
  'raramuri',
  'r_congo',
  'rhineland',
  'rif',
  'romania',
  'rome',
  'rvn',
  'russia',
  'rusyn',
  'rwanda',
  'ryukyu',
  'sabinium',
  'sadr',
  'saint_kitts_and_nevis',
  'saint_lucia',
  'saka',
  'sakha',
  'salish',
  'samnium',
  'samoa',
  'samogitia',
  'san_marino',
  'sao_tome_and_principe',
  'sapmi',
  'sardinia',
  'sarmatia',
  'saudi_arabia',
  'savoy',
  'saxony',
  'scania',
  'schleswig-holstein',
  'scotland',
  'scottishgaelic',
  'scythia',
  'seleucid',
  'seljuk',
  'seminole',
  'senegal',
  'serbia',
  'seychelles',
  'shan',
  'shawnee',
  'sherpa',
  'siberia',
  'sicily',
  'sierra_leone',
  'sikh',
  'sikkim',
  'silesia',
  'singapore',
  'sinhalese',
  'slavic',
  'slovakia',
  'slovenia',
  'sokoto',
  'solomon_islands',
  'somalia',
  'somaliland',
  'songhai',
  'south_africa',
  'southern_cross',
  'southern_sudan',
  'south_yemen',
  'soviet',
  'spain',
  'srilanka',
  'srivijaya',
  'stpatrick',
  'sudan',
  'suebian',
  'sumeria',
  'suriname',
  'svg',
  'swahili',
  'swaziland',
  'sweden',
  'switzerland',
  'syria',
  'taino',
  'tairona',
  'taiwan',
  'tajikistan',
  'tanganyika',
  'tannu_tuva',
  'tanzania',
  'tatarstan',
  'templar',
  'teutonic_order',
  'texas',
  'thailand',
  'thrace',
  'thuringia',
  'tibet',
  'timur',
  'tocharian',
  'togo',
  'toltec',
  'tonga',
  'transnistria',
  'transylvania',
  'trebizond',
  'trinidad_and_tobago',
  'trnc',
  'tuareg',
  'tunisia',
  'tupi',
  'turkey',
  'turkmenistan',
  'tyrol',
  'uae',
  'uganda',
  'ukraine',
  'unasur',
  'united_kingdom',
  'united_nations',
  'unknown',
  'urartu',
  'uruguay',
  'usa',
  'uyghur',
  'uzbekistan',
  'valknut',
  'vampire',
  'vandal',
  'vanuatu',
  'vatican',
  'vedic',
  'veletian',
  'venda',
  'venetic',
  'venezuela',
  'venice',
  'vermont',
  'vietnam',
  'viking',
  'visigoth',
  'vistulan',
  'volapuk',
  'volga_bulgar',
  'volga_german',
  'wales',
  'wallonia',
  'west_indies_federation',
  'west_papua',
  'westphalia',
  'wuerttemberg',
  'xhosa',
  'xiongnu',
  'yemen',
  'yucatan',
  'yugoslavia',
  'zambia',
  'zanzibar',
  'zapotec',
  'zhuang',
  'zimbabwe',
  'zulu'
]

foreach flag : flags
  install_data(join_paths('data/flags', flag + '-large.png'),
               join_paths('data/flags', flag + '-shield-large.png'),
               install_dir : join_paths(get_option('datadir'), 'freeciv/flags'))
endforeach

install_data('data/override/flags-large.spec',
             'data/override/flags.spec',
             'data/override/shields-large.spec',
             'data/override/shields.spec',
             install_dir : join_paths(get_option('datadir'), 'freeciv/override'))

install_data('data/hexemplio/activities.png',
             'data/hexemplio/activities.spec',
             'data/hexemplio/bases.png',
             'data/hexemplio/bases.spec',
             'data/hexemplio/cities.png',
             'data/hexemplio/cities.spec',
             'data/hexemplio/embellishments.png',
             'data/hexemplio/embellishments.spec',
             'data/hexemplio/forests.png',
             'data/hexemplio/forests.spec',
             'data/hexemplio/grid.png',
             'data/hexemplio/grid.spec',
             'data/hexemplio/hills.png',
             'data/hexemplio/hills.spec',
             'data/hexemplio/mountains.png',
             'data/hexemplio/mountains.spec',
             'data/hexemplio/rivers.png',
             'data/hexemplio/rivers.spec',
             'data/hexemplio/roads-maglevs.png',
             'data/hexemplio/roads-maglevs.spec',
             'data/hexemplio/roads.png',
             'data/hexemplio/roads.spec',
             'data/hexemplio/roads-rails.png',
             'data/hexemplio/roads-rails.spec',
             'data/hexemplio/select.png',
             'data/hexemplio/select.spec',
             'data/hexemplio/terrain.png',
             'data/hexemplio/terrain.spec',
             'data/hexemplio/tiles.png',
             'data/hexemplio/tiles.spec',
             'data/hexemplio/unitcost.png',
             'data/hexemplio/unitcost.spec',
             'data/hexemplio/unitextras.png',
             'data/hexemplio/unitextras.spec',
             'data/hexemplio/water1.png',
             'data/hexemplio/water1.spec',
             'data/hexemplio/water2.png',
             'data/hexemplio/water2.spec',
             'data/hexemplio/water3.png',
             'data/hexemplio/water3.spec',
             install_dir : join_paths(get_option('datadir'), 'freeciv/hexemplio'))

install_data('data/hex2t/grid.png',
             'data/hex2t/grid.spec',
             'data/hex2t/items.png',
             'data/hex2t/items.spec',
             'data/hex2t/overlays.png',
             'data/hex2t/overlays.spec',
             'data/hex2t/select.png',
             'data/hex2t/select.spec',
             'data/hex2t/tiles.png',
             'data/hex2t/tiles.spec',
             'data/hex2t/unitcost.png',
             'data/hex2t/unitcost.spec',
             install_dir : join_paths(get_option('datadir'), 'freeciv/hex2t'))

install_data('data/isophex/darkness.png',
             'data/isophex/darkness.spec',
             'data/isophex/grid.png',
             'data/isophex/grid.spec',
             'data/isophex/rivers.png',
             'data/isophex/rivers.spec',
             'data/isophex/terrain1.png',
             'data/isophex/terrain1.spec',
             'data/isophex/terrain2.png',
             'data/isophex/terrain2.spec',
             install_dir : join_paths(get_option('datadir'), 'freeciv/isophex'))

install_data('data/trident/cities.png',
             'data/trident/cities.spec',
             'data/trident/earth.png',
             'data/trident/earth.spec',
             'data/trident/explosions.png',
             'data/trident/explosions.spec',
             'data/trident/extra_units.png',
             'data/trident/extra_units.spec',
             'data/trident/fog.png',
             'data/trident/fog.spec',
             'data/trident/auto_ll.spec',
             'data/trident/grid.png',
             'data/trident/grid.spec',
             'data/trident/roads.png',
             'data/trident/roads.spec',
             'data/trident/select.png',
             'data/trident/select.spec',
             'data/trident/tiles.png',
             'data/trident/tiles.spec',
             'data/trident/units.png',
             'data/trident/units.spec',
             install_dir : join_paths(get_option('datadir'), 'freeciv/trident'))

install_data('data/isotrident/cities.png',
             'data/isotrident/cities.spec',
             'data/isotrident/fog.png',
             'data/isotrident/fog.spec',
             'data/isotrident/grid.png',
             'data/isotrident/grid.spec',
             'data/isotrident/morecities.png',
             'data/isotrident/morecities.spec',
             'data/isotrident/nuke.png',
             'data/isotrident/nuke.spec',
             'data/isotrident/select.png',
             'data/isotrident/select.spec',
             'data/isotrident/terrain1.png',
             'data/isotrident/terrain1.spec',
             'data/isotrident/terrain2.png',
             'data/isotrident/terrain2.spec',
             'data/isotrident/tiles.png',
             'data/isotrident/tiles.spec',
             'data/isotrident/unitcost.png',
             'data/isotrident/unitcost.spec',
             'data/isotrident/unitextras.png',
             'data/isotrident/unitextras.spec',
             install_dir : join_paths(get_option('datadir'), 'freeciv/isotrident'))

install_data('data/cimpletoon/orient_units.spec',
             'data/cimpletoon/orient_units.png',
             install_dir : join_paths(get_option('datadir'), 'freeciv/cimpletoon'))

install_data('data/alio/burrowtubes.png',
             'data/alio/burrowtubes.spec',
             'data/alio/fortresses.png',
             'data/alio/fortresses.spec',
             'data/alio/hills.png',
             'data/alio/hills.spec',
             'data/alio/riversbrown.png',
             'data/alio/riversbrown.spec',
             'data/alio/riversgreen.png',
             'data/alio/riversgreen.spec',
             'data/alio/roads.spec',
             'data/alio/roads.png',
             'data/alio/terrain.png',
             'data/alio/terrain.spec',
             'data/alio/tunnels.png',
             'data/alio/tunnels.spec',
             install_dir : join_paths(get_option('datadir'), 'freeciv/alio'))

install_data('data/amplio2/activities.png',
             'data/amplio2/activities.spec',
             'data/amplio2/animals.png',
             'data/amplio2/animals.spec',
             'data/amplio2/bases.png',
             'data/amplio2/bases.spec',
             'data/amplio2/cities.png',
             'data/amplio2/cities.spec',
             'data/amplio2/explosions.png',
             'data/amplio2/explosions.spec',
             'data/amplio2/extra_units.png',
             'data/amplio2/extra_units.spec',
             'data/amplio2/fog.png',
             'data/amplio2/fog.spec',
             'data/amplio2/grid.png',
             'data/amplio2/grid.spec',
             'data/amplio2/hills.png',
             'data/amplio2/hills.spec',
             'data/amplio2/maglev.png',
             'data/amplio2/maglev.spec',
             'data/amplio2/mountains.png',
             'data/amplio2/mountains.spec',
             'data/amplio2/nuke.png',
             'data/amplio2/nuke.spec',
             'data/amplio2/ocean.png',
             'data/amplio2/ocean.spec',
             'data/amplio2/select-alpha.png',
             'data/amplio2/select.spec',
             'data/amplio2/terrain1.png',
             'data/amplio2/terrain1.spec',
             'data/amplio2/terrain2.png',
             'data/amplio2/terrain2.spec',
             'data/amplio2/tiles.png',
             'data/amplio2/tiles.spec',
             'data/amplio2/units.png',
             'data/amplio2/units.spec',
             'data/amplio2/upkeep.png',
             'data/amplio2/upkeep.spec',
             'data/amplio2/veterancy.png',
             'data/amplio2/veterancy.spec',
             'data/amplio2/water.png',
             'data/amplio2/water.spec',
             install_dir : join_paths(get_option('datadir'), 'freeciv/amplio2'))

install_data('data/stdmusic/CullamBruce-Lockhart--Dawning_Fanfare.ogg',
             install_dir : join_paths(get_option('datadir'), 'freeciv/stdmusic'))

install_data('data/stdsounds/foot3.ogg',
             'data/stdsounds/illegal.ogg',
             'data/stdsounds/inh2o.ogg',
             'data/stdsounds/LrgCan.ogg',
             'data/stdsounds/LrgExpl.ogg',
             'data/stdsounds/MedCan.ogg',
             'data/stdsounds/metbrk.ogg',
             'data/stdsounds/MgBar1.ogg',
             'data/stdsounds/MgBar2.ogg',
             'data/stdsounds/MgHeavy.ogg',
             'data/stdsounds/Mortar.ogg',
             'data/stdsounds/SmlExpl.ogg',
             'data/stdsounds/Splash.ogg',
             'data/stdsounds/THover.ogg',
             'data/stdsounds/Tread.ogg',
             'data/stdsounds/wakeup.ogg',
             'data/stdsounds/wall01.ogg',
             'data/stdsounds/woodbrk.ogg',
             install_dir : join_paths(get_option('datadir'), 'freeciv/stdsounds'))

endif

i = ['16x16', '32x32', '48x48', '64x64', '128x128']
foreach iconsize : i
    install_data(join_paths('data/icons/', iconsize, 'freeciv-client.png'),
                 join_paths('data/icons/', iconsize, 'freeciv-server.png'),
                 join_paths('data/icons/', iconsize, 'freeciv-modpack.png'),
                 join_paths('data/icons/', iconsize, 'freeciv-ruledit.png'),
                 install_dir : join_paths(get_option('datadir'), 'icons/hicolor', iconsize, 'apps'))
endforeach

gtk322_dep = dependency('gtk+-3.0', version : '>= 3.22', required : false)
gtk4_dep = dependency('gtk4', version : '>= 4.0.0', required : false)

if get_option('clients').contains('gtk3.22')

if not gtk322_dep.found()
  error('gtk+-3.0 >= 3.22 required for gtk3.22-client, but not found')
endif

executable('freeciv-gtk3.22',
  'client/gui-gtk-3.22/action_dialog.c',
  'client/gui-gtk-3.22/canvas.c',
  'client/gui-gtk-3.22/chatline.c',
  'client/gui-gtk-3.22/choice_dialog.c',
  'client/gui-gtk-3.22/citizensinfo.c',
  'client/gui-gtk-3.22/citydlg.c',
  'client/gui-gtk-3.22/cityrep.c',
  'client/gui-gtk-3.22/cma_fe.c',
  'client/gui-gtk-3.22/colors.c',
  'client/gui-gtk-3.22/connectdlg.c',
  'client/gui-gtk-3.22/dialogs.c',
  'client/gui-gtk-3.22/diplodlg.c',
  'client/gui-gtk-3.22/editgui.c',
  'client/gui-gtk-3.22/editprop.c',
  'client/gui-gtk-3.22/finddlg.c',
  'client/gui-gtk-3.22/gamedlgs.c',
  'client/gui-gtk-3.22/gotodlg.c',
  'client/gui-gtk-3.22/graphics.c',
  'client/gui-gtk-3.22/gui_main.c',
  'client/gui-gtk-3.22/gui_stuff.c',
  'client/gui-gtk-3.22/happiness.c',
  'client/gui-gtk-3.22/helpdlg.c',
  'client/gui-gtk-3.22/infradlg.c',
  'client/gui-gtk-3.22/inputdlg.c',
  'client/gui-gtk-3.22/inteldlg.c',
  'client/gui-gtk-3.22/luaconsole.c',
  'client/gui-gtk-3.22/mapctrl.c',
  'client/gui-gtk-3.22/mapview.c',
  'client/gui-gtk-3.22/menu.c',
  'client/gui-gtk-3.22/messagedlg.c',
  'client/gui-gtk-3.22/messagewin.c',
  'client/gui-gtk-3.22/optiondlg.c',
  'client/gui-gtk-3.22/pages.c',
  'client/gui-gtk-3.22/plrdlg.c',
  'client/gui-gtk-3.22/rallypointdlg.c',
  'client/gui-gtk-3.22/repodlgs.c',
  'client/gui-gtk-3.22/soundset_dlg.c',
  'client/gui-gtk-3.22/spaceshipdlg.c',
  'client/gui-gtk-3.22/sprite.c',
  'client/gui-gtk-3.22/theme_dlg.c',
  'client/gui-gtk-3.22/themes.c',
  'client/gui-gtk-3.22/tileset_dlg.c',
  'client/gui-gtk-3.22/transportdlg.c',
  'client/gui-gtk-3.22/unitselect.c',
  'client/gui-gtk-3.22/unitselextradlg.c',
  'client/gui-gtk-3.22/unitselunitdlg.c',
  'client/gui-gtk-3.22/voteinfo_bar.c',
  'client/gui-gtk-3.22/wldlg.c',
  clienticon,
  c_args: [ '-DGDK_VERSION_MIN_REQUIRED=GDK_VERSION_3_20',
            '-DGDK_VERSION_MAX_ALLOWED=GDK_VERSION_3_22',
            '-DGLIB_VERSION_MIN_REQUIRED=GLIB_VERSION_2_50',
            '-DGLIB_VERSION_MAX_ALLOWED=GLIB_VERSION_2_50'],
  include_directories: client_inc,
  dependencies: [gtk322_dep, net_dep, gettext_dep],
  link_with: client_common,
  install: true
  )

install_data('data/gtk3.22_menus.xml',
             install_dir : join_paths(get_option('datadir'), 'freeciv'))

install_data('data/themes/gtk3.22/Freeciv/gtk-3.0/gtk.css',
             'data/themes/gtk3.22/Freeciv/gtk-3.0/menubar.css',
             'data/themes/gtk3.22/Freeciv/gtk-3.0/bg.png',
             install_dir : join_paths(get_option('datadir'),
                           'freeciv/themes/gtk3.22/Freeciv/gtk-3.0'))

endif

if get_option('qtver') == 'qt6'
  qt_mod = import('qt6')

  qt_dep = dependency('Qt6', modules: ['Core', 'Gui', 'Widgets'], required: false)
else
  qt_mod = import('qt5')

  qt_dep = dependency('Qt5', modules: ['Core', 'Gui', 'Widgets'], required: false)
endif

if get_option('clients').contains('qt')

if not qt_dep.found()
  if get_option('qtver') == 'qt6'
    error('Qt6 >= 6.0 required for qt-client in Qt6 mode, but not found')
  else
    error('Qt5 >= 5.11 required for qt-client in Qt5 mode, but not found')
  endif
endif

mocced_client = qt_mod.preprocess(
  moc_headers: [
   'client/gui-qt/fc_client.h',
   'client/gui-qt/ratesdlg.h',
   'client/gui-qt/mapview.h',
   'client/gui-qt/menu.h',
   'client/gui-qt/repodlgs.h',
   'client/gui-qt/dialogs.h',
   'client/gui-qt/optiondlg.h',
   'client/gui-qt/citydlg.h',
   'client/gui-qt/cityrep.h',
   'client/gui-qt/helpdlg.h',
   'client/gui-qt/plrdlg.h',
   'client/gui-qt/diplodlg.h',
   'client/gui-qt/spaceshipdlg.h',
   'client/gui-qt/messagewin.h',
   'client/gui-qt/chatline.h',
   'client/gui-qt/messagedlg.h',
   'client/gui-qt/sidebar.h',
   'client/gui-qt/shortcuts.h',
   'client/gui-qt/voteinfo_bar.h',
   'client/gui-qt/gotodlg.h',
   'client/gui-qt/hudwidget.h']
  )

executable('freeciv-qt',
  'client/gui-qt/canvas.cpp',
  'client/gui-qt/chatline.cpp',
  'client/gui-qt/citydlg.cpp',
  'client/gui-qt/cityrep.cpp',
  'client/gui-qt/colors.cpp',
  'client/gui-qt/connectdlg.cpp',
  'client/gui-qt/dialogs.cpp',
  'client/gui-qt/diplodlg.cpp',
  'client/gui-qt/fc_client.cpp',
  'client/gui-qt/finddlg.cpp',
  'client/gui-qt/fonts.cpp',
  'client/gui-qt/gotodlg.cpp',
  'client/gui-qt/graphics.cpp',
  'client/gui-qt/gui_main.cpp',
  'client/gui-qt/helpdlg.cpp',
  'client/gui-qt/hudwidget.cpp',
  'client/gui-qt/infradlg.cpp',
  'client/gui-qt/inteldlg.cpp',
  'client/gui-qt/luaconsole.cpp',
  'client/gui-qt/mapctrl.cpp',
  'client/gui-qt/mapview.cpp',
  'client/gui-qt/menu.cpp',
  'client/gui-qt/messagedlg.cpp',
  'client/gui-qt/messagewin.cpp',
  'client/gui-qt/optiondlg.cpp',
  'client/gui-qt/pages.cpp',
  'client/gui-qt/plrdlg.cpp',
  'client/gui-qt/qtg_cxxside.cpp',
  'client/gui-qt/ratesdlg.cpp',
  'client/gui-qt/repodlgs.cpp',
  'client/gui-qt/shortcuts.cpp',
  'client/gui-qt/sidebar.cpp',
  'client/gui-qt/spaceshipdlg.cpp',
  'client/gui-qt/sprite.cpp',
  'client/gui-qt/themes.cpp',
  'client/gui-qt/voteinfo_bar.cpp',
  'client/gui-qt/wldlg.cpp',
  'client/gui_interface.c',
  mocced_client, clienticon,
  include_directories: [client_inc, include_directories('client/gui-qt')],
  dependencies: [qt_dep, net_dep, gettext_dep],
  link_with: client_common,
  install: true)

install_data(
  'data/themes/gui-qt/icons/ai.png',
  'data/themes/gui-qt/icons/automate.png',
  'data/themes/gui-qt/icons/building.png',
  'data/themes/gui-qt/icons/buildroad.png',
  'data/themes/gui-qt/icons/cclose.png',
  'data/themes/gui-qt/icons/chopchop.png',
  'data/themes/gui-qt/icons/cities.png',
  'data/themes/gui-qt/icons/city-close.png',
  'data/themes/gui-qt/icons/city-left.png',
  'data/themes/gui-qt/icons/city-right.png',
  'data/themes/gui-qt/icons/city-switch.png',
  'data/themes/gui-qt/icons/close.png',
  'data/themes/gui-qt/icons/cmax.png',
  'data/themes/gui-qt/icons/cmin.png',
  'data/themes/gui-qt/icons/control.png',
  'data/themes/gui-qt/icons/cunits.png',
  'data/themes/gui-qt/icons/done.png',
  'data/themes/gui-qt/icons/economy.png',
  'data/themes/gui-qt/icons/endturn.png',
  'data/themes/gui-qt/icons/flag.png',
  'data/themes/gui-qt/icons/fortify.png',
  'data/themes/gui-qt/icons/future.png',
  'data/themes/gui-qt/icons/go-down.png',
  'data/themes/gui-qt/icons/goto.png',
  'data/themes/gui-qt/icons/go-up.png',
  'data/themes/gui-qt/icons/help-donate.png',
  'data/themes/gui-qt/icons/home.png',
  'data/themes/gui-qt/icons/human.png',
  'data/themes/gui-qt/icons/irrigation.png',
  'data/themes/gui-qt/icons/list-add.png',
  'data/themes/gui-qt/icons/load.png',
  'data/themes/gui-qt/icons/meeting-observer.png',
  'data/themes/gui-qt/icons/mine.png',
  'data/themes/gui-qt/icons/minus.png',
  'data/themes/gui-qt/icons/move.png',
  'data/themes/gui-qt/icons/nations.png',
  'data/themes/gui-qt/icons/nuke.png',
  'data/themes/gui-qt/icons/paradrop.png',
  'data/themes/gui-qt/icons/plantforest.png',
  'data/themes/gui-qt/icons/plus.png',
  'data/themes/gui-qt/icons/pollution.png',
  'data/themes/gui-qt/icons/preferences-other.png',
  'data/themes/gui-qt/icons/research.png',
  'data/themes/gui-qt/icons/resize.png',
  'data/themes/gui-qt/icons/sentry.png',
  'data/themes/gui-qt/icons/set_homecity.png',
  'data/themes/gui-qt/icons/transform.png',
  'data/themes/gui-qt/icons/units.png',
  'data/themes/gui-qt/icons/unload.png',
  'data/themes/gui-qt/icons/upgrade.png',
  'data/themes/gui-qt/icons/view.png',
  'data/themes/gui-qt/icons/wait.png',
  'data/themes/gui-qt/icons/wonder.png',
  install_dir : join_paths(get_option('datadir'),
                           'freeciv/themes/gui-qt/icons'))

install_data(
  'data/themes/gui-qt/NightStalker/resource.qss',
  'data/themes/gui-qt/NightStalker/ai.png',
  'data/themes/gui-qt/NightStalker/arrow-down.png',
  'data/themes/gui-qt/NightStalker/arrow-left.png',
  'data/themes/gui-qt/NightStalker/arrow-right.png',
  'data/themes/gui-qt/NightStalker/arrow-up.png',
  'data/themes/gui-qt/NightStalker/checkbox-checked.png',
  'data/themes/gui-qt/NightStalker/checkbox-checked-pressed.png',
  'data/themes/gui-qt/NightStalker/checkbox-unchecked.png',
  'data/themes/gui-qt/NightStalker/checkbox-unchecked-pressed.png',
  'data/themes/gui-qt/NightStalker/cities.png',
  'data/themes/gui-qt/NightStalker/city-close.png',
  'data/themes/gui-qt/NightStalker/city-left.png',
  'data/themes/gui-qt/NightStalker/city-right.png',
  'data/themes/gui-qt/NightStalker/city-switch.png',
  'data/themes/gui-qt/NightStalker/combo-arrow.png',
  'data/themes/gui-qt/NightStalker/human.png',
  'data/themes/gui-qt/NightStalker/pattern-game.png',
  'data/themes/gui-qt/NightStalker/pattern.png',
  'data/themes/gui-qt/NightStalker/plantforest.png',
  'data/themes/gui-qt/NightStalker/radio-checked.png',
  'data/themes/gui-qt/NightStalker/radio-unchecked.png',
  'data/themes/gui-qt/NightStalker/research.png',
  'data/themes/gui-qt/NightStalker/splitter-horizontal.png',
  'data/themes/gui-qt/NightStalker/splitter-vertical.png',
  'data/themes/gui-qt/NightStalker/sun.png',
  'data/themes/gui-qt/NightStalker/units.png',
  'data/themes/gui-qt/NightStalker/view.png',
  'data/themes/gui-qt/NightStalker/wheel.png',
  install_dir : join_paths(get_option('datadir'),
                           'freeciv/themes/gui-qt/NightStalker'))

install_data(
  'data/themes/gui-qt/Necrophos/resource.qss',
  'data/themes/gui-qt/Necrophos/arrow-down.png',
  'data/themes/gui-qt/Necrophos/arrow-left.png',
  'data/themes/gui-qt/Necrophos/arrow-right.png',
  'data/themes/gui-qt/Necrophos/arrow-up.png',
  'data/themes/gui-qt/Necrophos/checkbox-checked.png',
  'data/themes/gui-qt/Necrophos/checkbox-unchecked.png',
  'data/themes/gui-qt/Necrophos/cities.png',
  'data/themes/gui-qt/Necrophos/combo-arrow.png',
  'data/themes/gui-qt/Necrophos/economy.png',
  'data/themes/gui-qt/Necrophos/endturn.png',
  'data/themes/gui-qt/Necrophos/nations.png',
  'data/themes/gui-qt/Necrophos/pattern.png',
  'data/themes/gui-qt/Necrophos/radio-checked.png',
  'data/themes/gui-qt/Necrophos/radio-unchecked.png',
  'data/themes/gui-qt/Necrophos/research.png',
  'data/themes/gui-qt/Necrophos/splitter-horizontal.png',
  'data/themes/gui-qt/Necrophos/splitter-vertical.png',
  'data/themes/gui-qt/Necrophos/sun.png',
  'data/themes/gui-qt/Necrophos/units.png',
  'data/themes/gui-qt/Necrophos/view.png',
  'data/themes/gui-qt/Necrophos/wheel.png',
   install_dir : join_paths(get_option('datadir'),
                           'freeciv/themes/gui-qt/Necrophos'))

install_data(
  'data/themes/gui-qt/Classic/resource.qss',
  'data/themes/gui-qt/Classic/arrow-down.png',
  'data/themes/gui-qt/Classic/arrow-left.png',
  'data/themes/gui-qt/Classic/arrow-right.png',
  'data/themes/gui-qt/Classic/arrow-up.png',
  'data/themes/gui-qt/Classic/button-default.png',
  'data/themes/gui-qt/Classic/button-insensitive.png',
  'data/themes/gui-qt/Classic/button-prelight.png',
  'data/themes/gui-qt/Classic/button-pressed.png',
  'data/themes/gui-qt/Classic/check4.png',
  'data/themes/gui-qt/Classic/checkbox-checked.png',
  'data/themes/gui-qt/Classic/checkbox-checked-pressed.png',
  'data/themes/gui-qt/Classic/checkbox-unchecked.png',
  'data/themes/gui-qt/Classic/combo-arrow.png',
  'data/themes/gui-qt/Classic/combo-normal.png',
  'data/themes/gui-qt/Classic/menuline.png',
  'data/themes/gui-qt/Classic/menu-overlay.png',
  'data/themes/gui-qt/Classic/pattern.png',
  'data/themes/gui-qt/Classic/radio-checked.png',
  'data/themes/gui-qt/Classic/radio-unchecked.png',
  'data/themes/gui-qt/Classic/splitter-horizontal.png',
  'data/themes/gui-qt/Classic/splitter-vertical.png',
  'data/themes/gui-qt/Classic/wheel.png',
  install_dir : join_paths(get_option('datadir'),
                           'freeciv/themes/gui-qt/Classic'))

install_data(
  'data/themes/gui-qt/System/resource.qss',
  install_dir : join_paths(get_option('datadir'),
                           'freeciv/themes/gui-qt/System'))

endif

if get_option('clients').contains('sdl2')

executable('freeciv-sdl2',
  'client/gui-sdl2/action_dialog.c',
  'client/gui-sdl2/canvas.c',
  'client/gui-sdl2/chatline.c',
  'client/gui-sdl2/citydlg.c',
  'client/gui-sdl2/cityrep.c',
  'client/gui-sdl2/cma_fe.c',
  'client/gui-sdl2/colors.c',
  'client/gui-sdl2/connectdlg.c',
  'client/gui-sdl2/dialogs.c',
  'client/gui-sdl2/diplodlg.c',
  'client/gui-sdl2/finddlg.c',
  'client/gui-sdl2/gotodlg.c',
  'client/gui-sdl2/graphics.c',
  'client/gui-sdl2/gui_main.c',
  'client/gui-sdl2/gui_mouse.c',
  'client/gui-sdl2/gui_string.c',
  'client/gui-sdl2/gui_tilespec.c',
  'client/gui-sdl2/helpdlg.c',
  'client/gui-sdl2/infradlg.c',
  'client/gui-sdl2/inteldlg.c',
  'client/gui-sdl2/luaconsole.c',
  'client/gui-sdl2/mapctrl.c',
  'client/gui-sdl2/mapview.c',
  'client/gui-sdl2/menu.c',
  'client/gui-sdl2/messagedlg.c',
  'client/gui-sdl2/messagewin.c',
  'client/gui-sdl2/optiondlg.c',
  'client/gui-sdl2/pages.c',
  'client/gui-sdl2/plrdlg.c',
  'client/gui-sdl2/repodlgs.c',
  'client/gui-sdl2/spaceshipdlg.c',
  'client/gui-sdl2/sprite.c',
  'client/gui-sdl2/themebackgrounds.c',
  'client/gui-sdl2/themecolors.c',
  'client/gui-sdl2/themes.c',
  'client/gui-sdl2/themespec.c',
  'client/gui-sdl2/utf8string.c',
  'client/gui-sdl2/voteinfo_bar.c',
  'client/gui-sdl2/widget_button.c',
  'client/gui-sdl2/widget.c',
  'client/gui-sdl2/widget_checkbox.c',
  'client/gui-sdl2/widget_combo.c',
  'client/gui-sdl2/widget_core.c',
  'client/gui-sdl2/widget_edit.c',
  'client/gui-sdl2/widget_icon.c',
  'client/gui-sdl2/widget_label.c',
  'client/gui-sdl2/widget_scrollbar.c',
  'client/gui-sdl2/widget_window.c',
  'client/gui-sdl2/wldlg.c',
  clienticon,
  include_directories: client_inc,
  dependencies: [sdl2main_dep, audio_dep,
                 c_compiler.find_library('libSDL2_image', dirs: cross_lib_path),
                 c_compiler.find_library('libSDL2_gfx', dirs: cross_lib_path),
                 c_compiler.find_library('libSDL2_ttf', dirs: cross_lib_path),
                 net_dep, gettext_dep],
  link_with: client_common,
  install: true
  )

install_data('data/themes/gui-sdl2/human/backgrounds.themespec',
             'data/themes/gui-sdl2/human/bg2.png',
             'data/themes/gui-sdl2/human/bg.png',
             'data/themes/gui-sdl2/human/city_fist.png',
             'data/themes/gui-sdl2/human/city_fist.tspec',
             'data/themes/gui-sdl2/human/city.png',
             'data/themes/gui-sdl2/human/city.tspec',
             'data/themes/gui-sdl2/human/colors.themespec',
             'data/themes/gui-sdl2/human/COPYING.DejaVu',
             'data/themes/gui-sdl2/human/COPYING.fireflysung',
             'data/themes/gui-sdl2/human/COPYING.sazanami',
             'data/themes/gui-sdl2/human/COPYING.UnDotum',
             'data/themes/gui-sdl2/human/DejaVuSans.ttf',
             'data/themes/gui-sdl2/human/dip_icons.png',
             'data/themes/gui-sdl2/human/dip_icons.tspec',
             'data/themes/gui-sdl2/human/fireflysung.ttf',
             'data/themes/gui-sdl2/human/icons.png',
             'data/themes/gui-sdl2/human/icons.tspec',
             'data/themes/gui-sdl2/human/intro.png',
             'data/themes/gui-sdl2/human/options_bg.png',
             'data/themes/gui-sdl2/human/sazanami-gothic.ttf',
             'data/themes/gui-sdl2/human/small_theme_buttons.png',
             'data/themes/gui-sdl2/human/small_theme_buttons.tspec',
             'data/themes/gui-sdl2/human/tech_tree.png',
             'data/themes/gui-sdl2/human/tech_tree.tspec',
             'data/themes/gui-sdl2/human/theme_boxs.png',
             'data/themes/gui-sdl2/human/theme_boxs.tspec',
             'data/themes/gui-sdl2/human/theme_buttons.png',
             'data/themes/gui-sdl2/human/theme_buttons.tspec',
             'data/themes/gui-sdl2/human/theme_orders_buttons.png',
             'data/themes/gui-sdl2/human/theme_orders_buttons.tspec',
             'data/themes/gui-sdl2/human/theme_scrolls.png',
             'data/themes/gui-sdl2/human/theme_scrolls.tspec',
             'data/themes/gui-sdl2/human/theme.themespec',
             'data/themes/gui-sdl2/human/UnDotum.ttf',
             'data/themes/gui-sdl2/human/world.png',
             install_dir : join_paths(get_option('datadir'),
                           'freeciv/themes/gui-sdl2/human'))

endif

if get_option('fcmp') != []

fcmp_common = static_library('fcmp_common',
  'tools/fcmp/download.c',
  'tools/fcmp/modinst.c',
  'tools/fcmp/mpcmdline.c',
  'tools/fcmp/mpdb.c',
  sources: [verhdr],
  include_directories: tool_inc
  )

install_data(
  'data/freeciv-modpack.png',
  install_dir : join_paths(get_option('datadir'), 'freeciv')
  )

endif

if get_option('fcmp').contains('gtk3')

if not gtk322_dep.found()
  error('gtk+-3.0 required for gtk3 modpack installer, but not found')
endif

executable('freeciv-mp-gtk3',
  'tools/fcmp/mpgui_gtk3.c',
  mpicon,
  sources: [verhdr],
  c_args: [ '-DGDK_VERSION_MIN_REQUIRED=GDK_VERSION_3_20',
            '-DGDK_VERSION_MAX_ALLOWED=GDK_VERSION_3_22',
            '-DGLIB_VERSION_MIN_REQUIRED=GLIB_VERSION_2_50',
            '-DGLIB_VERSION_MAX_ALLOWED=GLIB_VERSION_2_50'],
  include_directories: tool_inc,
  dependencies: [gtk322_dep, sqlite3_dep, gettext_dep],
  link_with: [common_lib, fcmp_common],
  install: true
  )

endif


if get_option('fcmp').contains('gtk4')

if not gtk4_dep.found()
  error('gtk-4 required for gtk4 modpack installer, but not found')
endif

executable('freeciv-mp-gtk4',
  'tools/fcmp/mpgui_gtk4.c',
  mpicon,
  c_args: [ '-DGDK_VERSION_MIN_REQUIRED=GDK_VERSION_4_0',
            '-DGDK_VERSION_MAX_ALLOWED=GDK_VERSION_4_0',
            '-DGLIB_VERSION_MIN_REQUIRED=GLIB_VERSION_2_66',
            '-DGLIB_VERSION_MAX_ALLOWED=GLIB_VERSION_2_66'],
  include_directories: tool_inc,
  dependencies: [gtk4_dep, sqlite3_dep, gettext_dep],
  link_with: [common_lib, fcmp_common],
  install: true
  )

endif

if get_option('fcmp').contains('qt')

if not qt_dep.found()
  if get_option('qtver') == 'qt6'
    error('Qt6 >= 6.0 required for qt-modpack-installer in Qt6 mode, but not found')
  else
    error('Qt5 >= 5.11 required for qt-modpack-installer in Qt5 mode, but not found')
  endif
endif

mocced_fcmp = qt_mod.preprocess(
  moc_headers: [
   'tools/fcmp/mpgui_qt.h',
   'tools/fcmp/mpgui_qt_worker.h']
  )

executable('freeciv-mp-qt',
  'tools/fcmp/mpgui_qt.cpp',
  'tools/fcmp/mpgui_qt_worker.cpp',
  mocced_fcmp, mpicon,
  include_directories: tool_inc,
  dependencies: [qt_dep, sqlite3_dep, gettext_dep],
  link_with: [common_lib, fcmp_common],
  install: true
  )

endif

if get_option('fcmp').contains('cli')

executable('freeciv-mp-cli',
  'tools/fcmp/mpcli.c',
  mpicon,
  include_directories: tool_inc,
  dependencies: [sqlite3_dep, gettext_dep],
  link_with: [common_lib, fcmp_common],
  install: true
  )

endif

tool_lib = static_library('fc_toolutil',
  'tools/ruleutil/comments.c',
  'tools/ruleutil/rulesave.c',
  'tools/shared/tools_fc_interface.c',
  sources: [verhdr],
  include_directories: server_inc,
  )

executable('freeciv-ruleup',
  'tools/ruleup.c',
  link_with: [common_lib, server_lib, tool_lib, ais],
  include_directories: tool_inc,
  dependencies: [m_dep, net_dep, readline_dep, gettext_dep],
  install: true
  )

if get_option('ruledit')

if not qt_dep.found()
  if get_option('qtver') == 'qt6'
    error('Qt6 >= 6.0 required for ruledit in Qt6 mode, but not found')
  else
    error('Qt5 >= 5.11 required for ruledit in Qt5 mode, but not found')
  endif
endif

mocced_ruledit = qt_mod.preprocess(
  moc_headers: [
   'tools/ruledit/conversion_log.h',
   'tools/ruledit/edit_impr.h',
   'tools/ruledit/edit_utype.h',
   'tools/ruledit/effect_edit.h',
   'tools/ruledit/req_edit.h',
   'tools/ruledit/req_vec_fix.h',
   'tools/ruledit/requirers_dlg.h',
   'tools/ruledit/ruledit_qt.h',
   'tools/ruledit/tab_enablers.h',
   'tools/ruledit/tab_extras.h',
   'tools/ruledit/tab_misc.h',
   'tools/ruledit/tab_multiplier.h',
   'tools/ruledit/tab_nation.h',
   'tools/ruledit/tab_tech.h',
   'tools/ruledit/tab_terrains.h',
   'tools/ruledit/tab_building.h',
   'tools/ruledit/tab_good.h',
   'tools/ruledit/tab_gov.h',
   'tools/ruledit/tab_unit.h']
  )

executable('freeciv-ruledit',
  'tools/ruledit/conversion_log.cpp',
  'tools/ruledit/edit_impr.cpp',
  'tools/ruledit/edit_utype.cpp',
  'tools/ruledit/effect_edit.cpp',
  'tools/ruledit/req_edit.cpp',
  'tools/ruledit/requirers_dlg.cpp',
  'tools/ruledit/req_vec_fix.cpp',
  'tools/ruledit/ruledit.cpp',
  'tools/ruledit/ruledit_qt.cpp',
  'tools/ruledit/tab_building.cpp',
  'tools/ruledit/tab_enablers.cpp',
  'tools/ruledit/tab_extras.cpp',
  'tools/ruledit/tab_good.cpp',
  'tools/ruledit/tab_gov.cpp',
  'tools/ruledit/tab_misc.cpp',
  'tools/ruledit/tab_multiplier.cpp',
  'tools/ruledit/tab_nation.cpp',
  'tools/ruledit/tab_tech.cpp',
  'tools/ruledit/tab_terrains.cpp',
  'tools/ruledit/tab_unit.cpp',
  'tools/ruledit/univ_value.c',
  'tools/ruledit/validity.c',
  mocced_ruledit, rulediticon,
  include_directories: tool_inc,
  dependencies: [qt_dep, m_dep, net_dep, readline_dep, gettext_dep],
  link_with: [common_lib, server_lib, ais, tool_lib],
  install: true
  )

install_data(
  'data/freeciv-ruledit.png',
  install_dir : join_paths(get_option('datadir'), 'freeciv')
  )

endif

executable('freeciv-manual',
  'tools/civmanual.c',
  'client/helpdata.c',
  link_with: [common_lib, server_lib, tool_lib, ais],
  include_directories: [tool_inc,
                        include_directories('client', 'client/include')],
  dependencies: [m_dep, net_dep, readline_dep, gettext_dep],
  install: true
  )

install_data(
  'data/default/default.lua',
  'data/default/nationlist.ruleset',
  'data/default/ai_effects.ruleset',
  install_dir : join_paths(get_option('datadir'), 'freeciv/default')
  )

foreach nation : nations
  install_data(
    join_paths('data/nation', nation + '.ruleset'),
    install_dir : join_paths(get_option('datadir'), 'freeciv/nation'))
endforeach

install_data(
  'data/override/nation.ruleset',
  install_dir : join_paths(get_option('datadir'), 'freeciv/override'))

rulesets = [
  'civ2civ3',
  'classic',
  'multiplayer',
  'alien',
  'sandbox',
  'civ1',
  'civ2',
  'stub'
  ]

ruleset_files = [
  'buildings.ruleset',
  'cities.ruleset',
  'effects.ruleset',
  'game.ruleset',
  'governments.ruleset',
  'nations.ruleset',
  'parser.lua',
  'script.lua',
  'styles.ruleset',
  'techs.ruleset',
  'terrain.ruleset',
  'units.ruleset',
  ]

install_data(
  'data/civ2civ3.serv',
  'data/classic.serv',
  'data/multiplayer.serv',
  'data/alien.serv',
  'data/sandbox.serv',
  'data/civ1.serv',
  'data/civ2.serv',
  install_dir : join_paths(get_option('datadir'), 'freeciv')
  )

foreach rs : rulesets
  foreach rsf : ruleset_files
    install_data(
      join_paths('data', rs, rsf),
      install_dir : join_paths(get_option('datadir'), 'freeciv', rs)
      )
  endforeach
endforeach

install_data(
  'data/civ2civ3/README.civ2civ3',
  install_dir : join_paths(get_option('datadir'), 'freeciv/civ2civ3')
  )

install_data(
  'data/classic/README.classic',
  install_dir : join_paths(get_option('datadir'), 'freeciv/classic')
  )

install_data(
  'data/multiplayer/README.multiplayer',
  install_dir : join_paths(get_option('datadir'), 'freeciv/multiplayer')
  )

install_data(
  'data/alien/README.alien',
  'data/alien/nation_effects.ruleset',
  install_dir : join_paths(get_option('datadir'), 'freeciv/alien')
  )

install_data(
  'data/alien/nation/adventurers.ruleset',
  'data/alien/nation/galacticsound.ruleset',
  'data/alien/nation/jw.ruleset',
  'data/alien/nation/kindergarden.ruleset',
  'data/alien/nation/lunatics.ruleset',
  'data/alien/nation/madmen.ruleset',
  'data/alien/nation/mathclub.ruleset',
  'data/alien/nation/secretsociety.ruleset',
  'data/alien/nation/teamgladiators.ruleset',
  install_dir : join_paths(get_option('datadir'), 'freeciv/alien/nation')
  )

install_data(
  'data/stub/nations/barbarian.ruleset',
  'data/stub/nations/generic.ruleset',
  install_dir : join_paths(get_option('datadir'), 'freeciv/stub/nations')
  )

install_data(
  'data/sandbox/README.sandbox',
  'data/sandbox/luadata.txt',
  install_dir : join_paths(get_option('datadir'), 'freeciv/sandbox')
  )

install_data(
  'COPYING',
  'NEWS',
  'INSTALL',
  'ChangeLog',
  'ChangeLog-1.0-S3_0',
  'doc/BUGS',
  'doc/CodingStyle',
  'doc/FAQ',
  'doc/HACKING',
  'doc/HOWTOPLAY',
  'doc/INSTALL.Cygwin',
  'doc/INSTALL.meson',
  'doc/README',
  'doc/README.achievements',
  'doc/README.actions',
  'doc/README.agents',
  'doc/README.AI',
  'doc/README.AI_modules',
  'doc/README.attributes',
  'doc/README.delta',
  'doc/README.effects',
  'doc/README.fcdb',
  'doc/README.governor',
  'doc/README.graphics',
  'doc/README.modpack_installer',
  'doc/README.msys2',
  'doc/README.nations',
  'doc/README.packaging',
  'doc/README.rulesets',
  'doc/README.scenarios',
  'doc/README.scorelog',
  'doc/README.sound',
  'doc/README.tilesets',
  'doc/TODO',
  install_dir : join_paths(get_option('datadir'), 'doc/freeciv')
  )
