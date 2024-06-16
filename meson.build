
project('freeciv', ['c'], meson_version: '>= 0.60.0')

c_compiler = meson.get_compiler('c')

b_root = meson.project_build_root()
s_root = meson.project_source_root()

if get_option('tools').contains('ruledit') or \
   get_option('clients').contains('qt') or \
   get_option('fcmp').contains('qt')
  add_languages('cpp', native: false)
  cxx_build = true
  cxx_compiler = meson.get_compiler('cpp')
else
  cxx_build = false
endif

c_args = [
  '-Wnested-externs'
  ]

c_cpp_args = [
  '-Wall',
  '-Wmissing-declarations',
  '-Wpointer-arith',
  '-Wcast-align',
  '-Wmissing-prototypes',
  '-Wformat',
  '-Wformat-security',
  '-Wshadow',
  '-Wold-style-declaration',
  '-Wold-style-definition',
  '-Wtype-limits',
  '-Wimplicit-fallthrough'
  ]

cpp_args = [
  '-Wno-deprecated-declarations',
  '-fPIC'
  ]

foreach arg : c_args
  if c_compiler.has_argument(arg)
    add_global_arguments(arg, language : 'c')
  endif
endforeach

foreach arg : c_cpp_args
  if c_compiler.has_argument(arg)
    add_global_arguments(arg, language : 'c')
  endif

  if cxx_build
    if cxx_compiler.has_argument(arg)
      add_global_arguments(arg, language : 'cpp')
    endif
  endif
endforeach

if cxx_build
  foreach arg: cpp_args
    if cxx_compiler.has_argument(arg)
      add_global_arguments(arg, language : 'cpp')
    endif
  endforeach
endif

host_system = host_machine.system()

meta_url = 'https://meta.freeciv.org/metaserver.php'
mp_list_url = ''
storage_dir = ''
default_port = ''
testmatic = ''
dev_save_compat = ''
homepage_url = '"https://www.freeciv.org/"'

proj_def = get_option('project-definition')
if proj_def != ''
  fs = import('fs')
  if fs.is_file(proj_def)
    proj = fs.read(proj_def).strip().split('\n')
    foreach item : proj
      if item.startswith('META_URL')
        meta_url = item.substring(9)
      elif item.startswith('MODPACK_LIST_URL')
        mp_list_url = item.substring(17)
      elif item.startswith('FREECIV_STORAGE_DIR')
        storage_dir = item.substring(20)
      elif item.startswith('FREECIV_DEFAULT_PORT')
        default_port = item.substring(21)
      elif item.startswith('FREECIV_TESTMATIC')
        testmatic = item.substring(18)
      elif item.startswith('FREECIV_DEV_SAVE_COMPAT')
        dev_save_compat = item.substring(24)
      elif item.startswith('FREECIV_HOMEPAGE')
        homepage_url = '"' + item.substring(17) + '"'
      elif not item.startswith('#') and item != ''
        error('Unknown parameter ' + item + ' in project definition')
      endif
    endforeach
  else
    error('Project definition file ' + proj_def + ' not found!')
  endif
endif

priv_conf_data = configuration_data()
pub_conf_data = configuration_data()
liblua_conf_data = configuration_data()

pub_conf_data.set('FREECIV_META_URL', '"' + meta_url + '"')

if mp_list_url != ''
  pub_conf_data.set('MODPACK_LIST_URL', '"' + mp_list_url + '"')
endif

if default_port == ''
  default_port = 5556
endif
priv_conf_data.set('DEFAULT_SOCK_PORT', default_port)

priv_conf_data.set('HOMEPAGE_URL', homepage_url)

if testmatic == 'yes'
  pub_conf_data.set('FREECIV_TESTMATIC', 1)
elif testmatic != '' and testmatic != 'no'
  error('Unknown testmatic value ' + testmatic)
endif

if dev_save_compat == 'yes'
  priv_conf_data.set('FREECIV_DEV_SAVE_COMPAT', 1)
elif dev_save_compat != '' and dev_save_compat != 'no'
  error('Unknown dev_save_compat value ' + dev_save_compat)
endif

priv_conf_data.set('BINDIR',
                   join_paths(get_option('prefix'), get_option('bindir')))

priv_conf_data.set('DATADIR',
                   join_paths(get_option('prefix'), get_option('datadir')))
priv_conf_data.set('SYSCONFDIR',
                   join_paths(get_option('prefix'), get_option('sysconfdir')))

# Release cycle phases
# See fc_version about proper values for these.
datasubdir = 'dev'
priv_conf_data.set('DATASUBDIR', datasubdir)
priv_conf_data.set('IS_DEVEL_VERSION', 1)
priv_conf_data.set('IS_FREEZE_VERSION', 0)
priv_conf_data.set('IS_BETA_VERSION', 0)
priv_conf_data.set('FREECIV_RELEASE_MONTH', 0)
priv_conf_data.set('NEXT_STABLE_VERSION', '"3.4.0"')

if host_machine.endian() == 'big'
  priv_conf_data.set('WORDS_BIGENDIAN', 1)
endif

priv_conf_data.set('SIZEOF_INT', c_compiler.sizeof('int'))

if get_option('debug')
  priv_conf_data.set('FREECIV_DEBUG', 1)
  pub_conf_data.set('FREECIV_DEBUG', 1)
  add_global_arguments('-Wshadow', language: ['c'])
  if host_system == 'windows'
    # Qt headers have problems with dllimport attributes
    add_global_arguments('-Wno-error=attributes', language : 'cpp')
  else
    add_global_arguments('-Wshadow', language: ['cpp'])
  endif
else
  add_global_arguments('-DQT_NO_DEBUG', language : 'cpp')
endif

if host_system == 'windows'
  def_storage_dir = '~\\\\.freeciv'
else
  if host_system == 'haiku'
    def_storage_dir = '~/config/settings/freeciv'
  else
    def_storage_dir = '~/.freeciv'
  endif
endif

if storage_dir == ''
  storage_dir = def_storage_dir
endif

# pub conf defines a macro for FREECIV_STORAGE_DIR
# priv conf does substitution in data path macros
# They need this in different formats.
pub_conf_data.set('FREECIV_STORAGE_DIR', '"' + storage_dir + '"')
priv_conf_data.set('FREECIV_STORAGE_DIR', storage_dir)

if host_system == 'windows'
  default_localeprefix = '.'

  priv_conf_data.set('DEFAULT_DATA_PATH',
    '".;data;@FREECIV_STORAGE_DIR@/@DATASUBDIR@;@DATADIR@/freeciv"')
  priv_conf_data.set('DEFAULT_SAVE_PATH',
    '".;@FREECIV_STORAGE_DIR@/saves"')
  priv_conf_data.set('DEFAULT_SCENARIO_PATH',
    '".;data/scenarios;@FREECIV_STORAGE_DIR@/@DATASUBDIR@/scenarios;@FREECIV_STORAGE_DIR@/scenarios;@DATADIR@/freeciv/scenarios"')
else
  default_localeprefix = get_option('prefix')

  priv_conf_data.set('DEFAULT_DATA_PATH',
    '".:data:@FREECIV_STORAGE_DIR@/@DATASUBDIR@:@DATADIR@/freeciv"')
  priv_conf_data.set('DEFAULT_SAVE_PATH',
    '".:@FREECIV_STORAGE_DIR@/saves"')
  priv_conf_data.set('DEFAULT_SCENARIO_PATH',
    '".:data/scenarios:@FREECIV_STORAGE_DIR@/@DATASUBDIR@/scenarios:@FREECIV_STORAGE_DIR@/scenarios:@DATADIR@/freeciv/scenarios"')
endif

localeprefix = get_option('localeprefix')

if localeprefix == ''
  priv_conf_data.set('LOCALEDIR', join_paths(default_localeprefix, get_option('localedir')))
else
  priv_conf_data.set('LOCALEDIR', join_paths(localeprefix, get_option('localedir')))
endif

server_type = get_option('server')

if server_type == 'freeciv-web'
  pub_conf_data.set('FREECIV_WEB', 1)
  server_binary_name = 'freeciv-web'
else
  pub_conf_data.set('FREECIV_DELTA_PROTOCOL', 1)
  server_binary_name = 'freeciv-server'
endif

if server_type == 'disabled'
  pub_conf_data.set('FREECIV_AI_MOD_LAST', 1)

  priv_conf_data.set('AI_MOD_STATIC_STUB', 1)
else
  pub_conf_data.set('FREECIV_AI_MOD_LAST', 2)

  priv_conf_data.set('AI_MOD_STATIC_CLASSIC', 1)
  priv_conf_data.set('AI_MOD_STATIC_TEX', 1)
endif

if meson.is_cross_build()
  cross_inc_str = meson.get_external_property('cross_inc_path', '')
  cross_inc_path = [cross_inc_str]
  cross_lib_path = [meson.get_external_property('cross_lib_path', '')]
  crosser = meson.get_external_property('crosser', false)

  # emscripten build is always cross-build, so check it only here
  if c_compiler.compiles(
'''#ifndef __EMSCRIPTEN__
  error fail
#endif''',
  name: 'emscripten')
    emscripten = true
  else
    emscripten = false
  endif
else
  cross_inc_str = ''
  cross_inc_path = []
  cross_lib_path = []
  crosser = false
  emscripten = false
endif

if get_option('appimage')
  pub_conf_data.set('FREECIV_APPIMAGE', 1)
endif

qtver = get_option('qtver')

if c_compiler.compiles('''
#include <threads.h>
int main(void) { return thrd_create((void *)0, (void *)0, (void *)0); }''',
  name: 'C11 threads')
  pub_conf_data.set('FREECIV_HAVE_C11_THREADS', 1)
  threads='c11'
else
  threads=''
endif

# From this, at least the _WIN32_WINNT must be set before
# trying to find the functions.
if host_system == 'windows'
  pub_conf_data.set('FREECIV_MSWINDOWS', 1)
  pub_conf_data.set('FREECIV_HAVE_WINSOCK', 1)
  pub_conf_data.set('FREECIV_SOCKET_ZERO_NOT_STDIN', 1)
  priv_conf_data.set('ALWAYS_ROOT', 1)

  if threads == ''
    pub_conf_data.set('FREECIV_HAVE_TINYCTHR', 1)
    threads='tinycthread'
  endif

  min_win_ver = get_option('min-win-ver')

  if min_win_ver != ''
    add_global_arguments('-D_WIN32_WINNT=' + min_win_ver, language: ['c', 'cpp'])
  endif

  if cxx_build
    if get_option('debug')
      # Qt flags have malformed macro definition triggering this error
      # Also C compiler affected as the macro is on its commandline too
      if c_compiler.has_argument('-Wno-c99-extensions')
        add_global_arguments('-Wno-c99-extensions', language : 'c')
      endif
      if cxx_compiler.has_argument('-Wno-c99-extensions')
        add_global_arguments('-Wno-c99-extensions', language : 'cpp')
      endif
    endif
    if min_win_ver == ''
      if qtver == 'qt6' or qtver == 'qt6x'
        add_global_arguments('-D_WIN32_WINNT=0x0A00', language : ['c', 'cpp'])
      else
        add_global_arguments('-D_WIN32_WINNT=0x0603', language : ['c', 'cpp'])
      endif
    endif
  elif min_win_ver == ''
    add_global_arguments('-D_WIN32_WINNT=0x0603', language : ['c', 'cpp'])
  endif

  net_dep = c_compiler.find_library('ws2_32')
else
  if get_option('min-win-ver') != ''
    error('Option min-win-ver supported on Windows only!')
  endif

  # Assume that vsnprintf() is a working one, if it's found
  # (that's checked separately)
  priv_conf_data.set('HAVE_WORKING_VSNPRINTF', 1)

  if threads == ''
    pub_conf_data.set('FREECIV_HAVE_PTHREAD', 1)
    threads='pthread'
  endif

  if host_system == 'haiku'
    net_dep = c_compiler.find_library('network')
    priv_conf_data.set('ALWAYS_ROOT', 1)
  else
    net_dep = []
  endif
endif

pub_headers = [
  'locale.h',
  'inttypes.h',
  'stdint.h',
  'sys/types.h',
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
  'direct.h',
  'dlfcn.h',
  'execinfo.h',
  'fcntl.h',
  'time.h',
  'libgen.h',
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

if host_system == 'windows'
  # We don't want Cygwin to find these, so checking only under "real" Windows layer

  win_priv_headers= [
    'bcrypt.h'
  ]

  if c_compiler.has_header('ws2tcpip.h', args: header_arg)
    net_incl = '''#include <ws2tcpip.h>'''
    pub_conf_data.set('FREECIV_HAVE_WS2TCPIP_H', 1)
    priv_conf_data.set('HAVE_WS2TCPIP_H', 1)
  else
    net_incl = ''
  endif

  foreach hdr : win_priv_headers
    if c_compiler.has_header(hdr, args: header_arg)
      priv_conf_data.set('HAVE_' + hdr.underscorify().to_upper(), 1)
    endif
  endforeach
else
  net_incl = ''
endif

priv_functions = [
  'fork',
  'vfork',
  'backtrace',
  'bind',
  'clock_gettime',
  'connect',
  'fdopen',
  'fopen_s',
  'fileno',
  'flock',
  'getentropy',
  'gettimeofday',
  'ftime',
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
                             dependencies: net_dep,
                             args: ['-O'])
    priv_conf_data.set('HAVE_' + func.underscorify().to_upper(), 1)
  endif
endforeach

priv_liblua_functions = [
  'localtime_r'
  ]

foreach func : priv_liblua_functions
  if c_compiler.has_function(func,
                             dependencies: net_dep,
                             args: ['-O'])
    priv_conf_data.set('HAVE_' + func.underscorify().to_upper(), 1)
    liblua_conf_data.set('HAVE_' + func.underscorify().to_upper(), 1)
  endif
endforeach

liblua_functions = [
  'mkstemp',
  'popen',
  'pclose',
  '_longjmp',
  '_setjmp',
  'gmtime_r',
  'fseeko'
  ]

foreach func : liblua_functions
  if c_compiler.has_function(func,
                             dependencies: net_dep,
                             args: ['-O'])
    liblua_conf_data.set('HAVE_' + func.underscorify().to_upper(), 1)
  endif
endforeach

if c_compiler.has_header('unistd.h')
  pub_conf_data.set('FREECIV_HAVE_UNISTD_H', 1)
  priv_conf_data.set('HAVE_UNISTD_H', 1)
  liblua_conf_data.set('FREECIV_HAVE_UNISTD_H', 1)
endif

if c_compiler.has_function('BCryptGenRandom', args: ['-lbcrypt', '-O'])
  bcrypt_lib_dep = c_compiler.find_library('bcrypt')
  priv_conf_data.set('HAVE_BCRYPTGENRANDOM', 1)
else
  bcrypt_lib_dep = []
endif

if c_compiler.has_function('getaddrinfo',
                           dependencies: net_dep,
                           args: ['-O'])
  priv_conf_data.set('HAVE_GETADDRINFO', 1)
  pub_conf_data.set('FREECIV_IPV6_SUPPORT', 1)
else
  # Maybe it exist as a macro instead?
  if c_compiler.compiles(net_incl + '''
int main(void) { getaddrinfo(NULL, NULL, NULL, NULL); }''',
  include_directories: include_directories(cross_inc_path),
  name: 'getaddrinfo() as a macro')
    priv_conf_data.set('HAVE_GETADDRINFO', 1)
    pub_conf_data.set('FREECIV_IPV6_SUPPORT', 1)
  else
    warning('IPv6 support not enabled')
  endif
endif

if c_compiler.has_header('libcharset.h', args: header_arg)
  if c_compiler.has_function('locale_charset', args: ['-O'])
    priv_conf_data.set('HAVE_LIBCHARSET', 1)
    charset_dep = []
  else
    charset_dep = c_compiler.find_library('charset', dirs: cross_lib_path,
                                          required: false)
    if charset_dep.found() and c_compiler.has_function('locale_charset',
                                                       dependencies: charset_dep,
                                                       args: ['-O'])
      priv_conf_data.set('HAVE_LIBCHARSET', 1)
    else
      charset_dep = []
    endif
  endif
else
  charset_dep = []
endif

rl_req = get_option('readline')
if rl_req != 'false'
  readline_dep = c_compiler.find_library('readline', dirs: cross_lib_path,
                                         required:false)

  if readline_dep.found() and c_compiler.has_function('rl_completion_suppress_append',
                                                      dependencies: readline_dep,
                                                      args: ['-O'])
    pub_conf_data.set('FREECIV_HAVE_LIBREADLINE', 1)
  elif rl_req == 'true'
    error('Readline support requested but not found.')
  endif
else
  readline_dep = []
endif

if c_compiler.has_header('bzlib.h', args: header_arg)
  priv_conf_data.set('HAVE_BZLIB_H', 1)

  bz2_dep = c_compiler.find_library('bz2', dirs: cross_lib_path,
                                    required:false)

  if bz2_dep.found()
    pub_conf_data.set('FREECIV_HAVE_LIBBZ2', 1)
  endif
else
  bz2_dep = []
endif

if c_compiler.has_header('lzma.h', args: header_arg)
  priv_conf_data.set('HAVE_LZMA_H', 1)

  lzma_dep = c_compiler.find_library('lzma', dirs: cross_lib_path,
                                     required:false)

  if lzma_dep.found()
    pub_conf_data.set('FREECIV_HAVE_LIBLZMA', 1)
  endif
else
  lzma_dep = []
endif

if c_compiler.has_header('zstd.h', args: header_arg)
  priv_conf_data.set('HAVE_ZSTD_H', 1)

  zstd_dep = c_compiler.find_library('zstd', dirs: cross_lib_path,
                                     required:false)

  if zstd_dep.found()
    pub_conf_data.set('FREECIV_HAVE_LIBZSTD', 1)
  endif
else
  zstd_dep = []
endif

if c_compiler.compiles('''
#include <assert.h>
int main(void) { static_assert(1, "1 is not true"); }''',
  name: 'c23 Static Assert')
  pub_conf_data.set('FREECIV_C23_STATIC_ASSERT', 1)
endif

if c_compiler.compiles('''
#include <assert.h>
int main(void) { _Static_assert(1, "1 is not true"); }''',
  name: 'c11 Static Assert')
  pub_conf_data.set('FREECIV_C11_STATIC_ASSERT', 1)
endif

if cxx_build and cxx_compiler.compiles('''
#include <assert.h>
int main(void) { static_assert(1, "1 is not true"); }''',
  name: 'c++11 Static Assert')
  pub_conf_data.set('FREECIV_CXX11_STATIC_ASSERT', 1)
endif

if c_compiler.compiles('''
#include <assert.h>
#include <string.h>
static const char str[] = "12345";
int main(void) { _Static_assert(5 == strlen(str), "Wrong length"); }''',
  name: 'strlen() in static assert')
  pub_conf_data.set('FREECIV_STATIC_STRLEN', 1)
endif

# Check for c23 nullptr that can be passed as sentinel
if c_compiler.compiles('''
#include <stddef.h>
void sentinental(...) __attribute__((__sentinel__(0)));
void caller(void) { sentinental(nullptr); }''',
  name: 'c23 nullptr',
  include_directories: include_directories(cross_inc_path),
  args: ['-Wformat'])
  pub_conf_data.set('FREECIV_HAVE_C23_NULLPTR', 1)
endif

if cxx_build and cxx_compiler.compiles('''
#include <cstddef>
int main(void) { int *var = nullptr; return 0; }''',
  name: 'cxx nullptr',
  include_directories: include_directories(cross_inc_path))
  pub_conf_data.set('FREECIV_HAVE_CXX_NULLPTR', 1)
endif

if emscripten
  icu_dep = []
  zlib_dep = []
  syslua = 'false'

  emscripten_use_args = [
     '-s', 'USE_ICU=1',
     '-s', 'USE_ZLIB=1',
     '-s', 'USE_PTHREADS=1'
    ]

  add_global_arguments(
     emscripten_use_args,
     language : [ 'c', 'cpp'])
  add_global_link_arguments(
     emscripten_use_args,
     '-s', 'TOTAL_MEMORY=64MB',
     language : [ 'c', 'cpp'])
else
  zlib_dep = c_compiler.find_library('z', dirs: cross_lib_path)
  if not c_compiler.has_header('zlib.h', args: header_arg)
    error('Mandatory header zlib.h not found!')
  endif
  icu_dep = dependency('icu-uc')
  syslua = get_option('syslua')
  if syslua != 'false'
    lua_dep_tmp = dependency('lua-5.4', 'lua-54', 'lua54', 'lua5.4', required:false)
  endif
endif

# Set unconditionally, as it was checked as hard requirement
pub_conf_data.set('FREECIV_HAVE_LIBZ', 1)

mw_req = get_option('mwand')
mw_extra_dep = []
if mw_req != 'false'
  mw_dep = dependency('MagickWand', version : '>= 7.0', required : false)
  if mw_dep.found()
    pub_conf_data.set('FREECIV_MWAND7', '1')
    mwand_incl = '#include <MagickWand/MagickWand.h>'
  else
    mw_dep = dependency('MagickWand', version : '>= 6.0', required : false)
    if not mw_dep.found()
      mw_dep = dependency('MagickWand-6.Q16HDRI', required: false)
    endif
    if mw_dep.found()
      mwand_incl = '#include <wand/MagickWand.h>'
      mwand_incl = '#include <wand/MagickWand.h>'
    endif
  endif
  if mw_dep.found()
    if not c_compiler.links(mwand_incl + '''
int main(void) { MagickWandGenesis(); }''',
    name: 'mwand links',
    dependencies: mw_dep)
      mw_extra_dep = [c_compiler.find_library('urlmon', dirs: cross_lib_path,
                                              required: false),
                      c_compiler.find_library('gdi32', dirs: cross_lib_path,
                                              required: false)]
      if c_compiler.links(mwand_incl + '''
int main(void) { MagickWandGenesis(); }''',
      name: 'mwand links',
      dependencies: [mw_dep, mw_extra_dep, zlib_dep, lzma_dep, net_dep])
        priv_conf_data.set('HAVE_MAPIMG_MAGICKWAND', '1')
      elif mw_req == 'true'
        error('MagickWand support requested but not found.')
      else
        mw_dep = []
        mw_extra_dep = []
      endif
    else
      priv_conf_data.set('HAVE_MAPIMG_MAGICKWAND', '1')
    endif
  elif mw_req == 'true'
    error('MagickWand support requested but not found.')
  endif
else
  mw_dep = []
endif

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
int main(void) { struct ip_mreqn req; req.imr_ifindex = 0; return 0; }''',
  name: 'ip_mreqn',
  include_directories: include_directories(cross_inc_path))
  priv_conf_data.set('HAVE_IP_MREQN', 1)
endif

if c_compiler.has_function('iconv', args: [header_arg, '-O'])
  priv_conf_data.set('HAVE_ICONV', 1)
  iconv_lib_dep = []
elif c_compiler.has_header_symbol('iconv.h', 'iconv', args: header_arg)
  iconv_lib_dep = c_compiler.find_library('iconv', dirs: cross_lib_path,
                                          required: false)
  if iconv_lib_dep.found()
    priv_conf_data.set('HAVE_ICONV', 1)
  endif
else
  iconv_lib_dep = []
endif

if c_compiler.compiles('''#include <stddef.h>
#include <iconv.h>
int main(void) { iconv_t cd; const char **c; iconv(cd, c, NULL, NULL); return 0; }''',
  name: 'iconv const',
  include_directories: include_directories(cross_inc_path))
  priv_conf_data.set('ICONV_CONST', 'const')
else
  priv_conf_data.set('ICONV_CONST', '')
endif

if get_option('cacert-path') != ''
  priv_conf_data.set('CUSTOM_CACERT_PATH',
                     '"' + get_option('cacert-path') + '"')
endif

if get_option('followtag') != ''
  pub_conf_data.set('FOLLOWTAG',
                    '"' + get_option('followtag') + '"')
endif

potential_size_t_formats = [ '%zu', '%ld', '%lld', '%I64d', '%I32d' ]

size_t_f = ''
foreach format : potential_size_t_formats
  if c_compiler.compiles('''#include <stdio.h>
#if defined(__GNUC__)
void fr(const char *form, ...)
  __attribute__((__format__(__printf__, 1, 2)));
#else
#define fr(_a_,_b_) printf(_a_,_b_)
#endif
int main(void) { size_t var = 0; fr("''' + format + '''", var); return 0; }''',
  name: 'size_t printf as ' + format,
  include_directories: include_directories(cross_inc_path),
  args: ['-Werror', '-Wall', '-O'])
    priv_conf_data.set('SIZE_T_PRINTF', '"' + format + '"')
    size_t_f = format
    break
  endif
endforeach

if size_t_f == ''
  error('Cannot find correct printf format specifier for size_t')
endif

if c_compiler.compiles('''int main(void) { __builtin_unreachable(); return 0; }''',
  include_directories: include_directories(cross_inc_path),
  name: '__builtin_unreachable()',
  args: ['-Werror', '-Wall'])
    pub_conf_data.set('FREECIV_HAVE_UNREACHABLE', 1)
endif

if get_option('audio') == 'sdl2' or get_option('clients').contains('sdl2')
  if host_system == 'windows'
    sdl2main_dep = [c_compiler.find_library('mingw32', dirs: cross_lib_path),
                    c_compiler.find_library('SDL2main', dirs: cross_lib_path),
                    c_compiler.find_library('SDL2', dirs: cross_lib_path)]
  else
    if emscripten
      emscripten_sdl2_args = [
        '-s', 'USE_SDL=2'
      ]

      if get_option('audio') == 'sdl2'
        emscripten_sdl2_args += [ '-s', 'USE_SDL_MIXER=2' ]
      endif

      if get_option('clients').contains('sdl2')
        emscripten_sdl2_args += [
          '-s', 'USE_SDL_IMAGE=2',
          '-s', 'USE_SDL_TTF=2',
          '-s', 'USE_SDL_GFX=2'
        ]
      endif

      add_global_arguments(emscripten_sdl2_args, language: ['c', 'cpp'])
      add_global_link_arguments(emscripten_sdl2_args, language: ['c', 'cpp'])
      sdl2main_dep = []
    else
      sdl2main_dep = [c_compiler.find_library('SDL2', dirs: cross_lib_path)]
    endif
  endif
endif

if get_option('audio') == 'sdl3' or get_option('clients').contains('sdl3')
  if host_system == 'windows'
    sdl3main_dep = [c_compiler.find_library('mingw32', dirs: cross_lib_path),
                    c_compiler.find_library('SDL3main', dirs: cross_lib_path),
                    c_compiler.find_library('SDL3', dirs: cross_lib_path)]
  else
    if emscripten
      emscripten_sdl3_args = [
        '-s', 'USE_SDL=3'
      ]

      if get_option('audio') == 'sdl3'
        emscripten_sdl3_args += [ '-s', 'USE_SDL_MIXER=3' ]
      endif

      if get_option('clients').contains('sdl3')
        emscripten_sdl3_args += [
          '-s', 'USE_SDL_IMAGE=3',
          '-s', 'USE_SDL_TTF=3',
          '-s', 'USE_SDL_GFX=3'
        ]
      endif

      add_global_arguments(emscripten_sdl3_args, language: ['c', 'cpp'])
      add_global_link_arguments(emscripten_sdl3_args, language: ['c', 'cpp'])
      sdl3main_dep = []
    else
      sdl3main_dep = [c_compiler.find_library('SDL3', dirs: cross_lib_path)]
    endif
  endif
endif

if qtver == 'qt5'
  qt_minver='0x050f00'
  add_global_arguments('-DQT_DISABLE_DEPRECATED_BEFORE=' + qt_minver, language : 'cpp')
  priv_conf_data.set('FC_QT5_MODE', 1)
  qt_opts = []
  qt_cppflags = '-fPIC'
elif qtver == 'qt6x'
  qt_minver='0x060700'
  add_global_arguments('-DQT_DISABLE_DEPRECATED_BEFORE=' + qt_minver, language : 'cpp')
  priv_conf_data.set('FC_QT6X_MODE', 1)
  qt_opts = 'cpp_std=c++17'
  qt_cppflags = '-std=c++17'
else
  qt_minver='0x060000'
  add_global_arguments('-DQT_DISABLE_DEPRECATED_BEFORE=' + qt_minver, language : 'cpp')
  qt_opts = 'cpp_std=c++17'
  qt_cppflags = '-std=c++17'
endif

if get_option('audio') != 'none'
  priv_conf_data.set('AUDIO_SDL', 1)

  if emscripten
    audio_dep = []
  elif get_option('audio') == 'sdl3'
    audio_sdl3_dep = c_compiler.find_library('SDL3_mixer',
                                             dirs: cross_lib_path)
    audio_dep = [sdl3main_dep, audio_sdl3_dep]
    priv_conf_data.set('AUDIO_SDL3', 1)
  else
    audio_sdl2_dep = c_compiler.find_library('SDL2_mixer',
                                             dirs: cross_lib_path)
    audio_dep = [sdl2main_dep, audio_sdl2_dep]
  endif

  audio_sdl_src = 'client/audio_sdl.c'
else
  audio_dep = []
  audio_sdl_src = []
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

nls_req = get_option('nls')

if c_compiler.has_header('libintl.h', args: header_arg)
  pub_conf_data.set('FREECIV_HAVE_LIBINTL_H', 1)
  priv_conf_data.set('HAVE_LIBINTL_H', 1)
elif nls_req
  error('nls support requested, but libintl.h header not found')
endif

if nls_req
  pub_conf_data.set('FREECIV_ENABLE_NLS', 1)
  priv_conf_data.set('ENABLE_NLS', 1)
  gettext_dep = c_compiler.find_library('intl', dirs: cross_lib_path,
                                        required: false)
else
  gettext_dep = []
endif

if emscripten
  curl_dep = []
  m_dep = []
else
  curl_dep = c_compiler.find_library('curl', dirs: cross_lib_path)
  m_dep = c_compiler.find_library('m', dirs: cross_lib_path)
endif

if not emscripten
  if c_compiler.compiles(net_incl + '''
#include <stddef.h>
#include <curl/curl.h>
int main(void) { curl_mime *mime = curl_mime_init(NULL); }''',
  name: 'curl mime API',
  include_directories: include_directories(cross_inc_path)
  )
    priv_conf_data.set('HAVE_CURL_MIME_API', 1)
  else
    error('Too old libcurl version - does not support mime API')
  endif
endif

if not emscripten
  sqlite3_dep = c_compiler.find_library('sqlite3', dirs: cross_lib_path)
else
  sqlite3_dep = []
endif

fcdb = false
fcdb_src = []
fcdb_dep = []

if get_option('fcdb').contains('sqlite3')
  fcdb = true
  priv_conf_data.set('HAVE_FCDB_SQLITE3', 1)
  fcdb_src = [ fcdb_src,
    'dependencies/luasql/src/ls_sqlite3.c',
  ]
  fcdb_dep = [ fcdb_dep, sqlite3_dep ]
endif

if get_option('fcdb').contains('mariadb')
  fcdb = true
  priv_conf_data.set('HAVE_FCDB_MYSQL', 1)
  fcdb_src = [ fcdb_src,
    'dependencies/luasql/src/ls_mysql.c',
  ]
  mdb_dep = dependency('mariadb', required: false)
  if not mdb_dep.found()
    mdb_dep = dependency('libmariadb')
  endif
  fcdb_dep = [ fcdb_dep, mdb_dep ]
endif

if get_option('fcdb').contains('odbc')
  fcdb = true
  priv_conf_data.set('HAVE_FCDB_ODBC', 1)
  fcdb_src = [ fcdb_src,
    'dependencies/luasql/src/ls_odbc.c',
  ]
  fcdb_dep = [ fcdb_dep, dependency('odbc') ]
endif

if fcdb
  if emscripten
    error('FCDB not supported on emscripten builds.')
  endif
  priv_conf_data.set('HAVE_FCDB', 1)
  fcdb_src = [ 'dependencies/luasql/src/luasql.c', fcdb_src ]
endif

if get_option('gitrev')
  priv_conf_data.set('GITREV', 1)
endif

if crosser
  pub_conf_data.set('FREECIV_CROSSER', 1)
endif

svgflags = get_option('svgflags')
if svgflags
  pub_conf_data.set('FREECIV_SVG_FLAGS', 1)
endif

if cxx_build and cxx_compiler.compiles('''
class me {
void top(); };
void me::top() { (void) [=, this]() { this->top(); }; };
''',
     name: 'C++20 capture this',
     args: ['-Werror', '-Wall'])
  pub_conf_data.set('FREECIV_HAVE_CXX20_CAPTURE_THIS', 1)
endif

if not meson.is_cross_build()
  zbm = c_compiler.run('''
#include <stdlib.h>
int main()
{
  void *allocation = malloc(0);

  if (allocation != NULL) {
    free(allocation);
  }

  return (allocation == NULL);
}''',
       name: 'zero-byte malloc()',
       args: ['-Werror', '-Wall'])

  if zbm.returncode() == 0
    priv_conf_data.set('MALLOC_ZERO_OK', 1)
  endif
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

man_pages = [
  'freeciv',
  'freeciv-client',
  'freeciv-manual',
  'freeciv-modpack',
  'freeciv-ruledit',
  'freeciv-ruleup',
  'freeciv-server'
  ]

man_links = [
  'freeciv-gtk3.22',
  'freeciv-gtk4',
  'freeciv-sdl2',
  'freeciv-sdl3',
  'freeciv-qt',
  'freeciv-mp-gtk3',
  'freeciv-mp-gtk4',
  'freeciv-mp-qt',
  'freeciv-mp-cli'
  ]

foreach manp : man_pages
  man_conf_data = configuration_data()
  man_conf_data.set('HOMEPAGE_URL', '"' + homepage_url + '"')
  man_conf_data.set('BUG_URL', '"https://redmine.freeciv.org/projects/freeciv"')
  man_conf_data.set('MAIL_ADDRESS', '"freeciv-dev at freelists.org"')
  man_conf_data.set('DATASUBDIR', '"' + datasubdir + '"')
  configure_file(input : 'doc/man/' + manp + '.6.in',
                 output : manp + '.6',
                 configuration : man_conf_data)
endforeach

add_global_arguments('-DHAVE_CONFIG_H',
                     language: ['c', 'cpp'])

if get_option('nls')
  subdir('translations/core')
  subdir('translations/nations')
  subdir('translations/ruledit')
endif

gen_packets_args = get_option('gen-packets-args')

python_exe = find_program(get_option('python'))
sh_exe = find_program('sh')
gzip_exe = find_program('gzip')

common_inc = include_directories(cross_inc_path,
  lua_inc_path, 'dependencies/luasql/src', 'dependencies/tinycthread',
  'dependencies/tolua-5.2/include', 'dependencies/cvercmp',
  'utility', 'common', 'common/networking', 'common/scriptcore',
  'common/aicore')

server_inc = [common_inc, include_directories('server', 'server/advisors',
              'server/scripting', 'server/generator', 'server/ruleset',
              'server/savegame', 'ai', 'ai/classic')]

client_inc = [common_inc, include_directories('client', 'client/include',
              'client/luascript', 'client/agents')]

tool_inc = [server_inc, include_directories('tools/ruleutil', 'tools/shared')]

runwrap = custom_target('runwrap', output: 'run.sh',
              command: [sh_exe, files('bootstrap/generate_meson_run.sh'),
                        '@OUTPUT@', storage_dir, datasubdir],
              depend_files: files('bootstrap/generate_meson_run.sh'),
              build_by_default: true)

verhdr = custom_target('verhdr', output: 'version_gen.h',
              command: [sh_exe, files('gen_headers/generate_version_header.sh'), '@OUTPUT@'],
              depend_files: files('fc_version'))

ls_core = custom_target('langstat_core', output: 'langstat_core.txt',
              command: [sh_exe, files('bootstrap/generate_langstat.sh'), 'core',
                        s_root, b_root])

ls_nations = custom_target('langstat_nations', output: 'langstat_nations.txt',
               command: [sh_exe, files('bootstrap/generate_langstat.sh'), 'nations',
                         s_root, b_root])

ls_ruledit = custom_target('langstat_ruledit', output: 'langstat_ruledit.txt',
                command: [sh_exe, files('bootstrap/generate_langstat.sh'), 'ruledit',
                          s_root, b_root])

specenum = custom_target('specenum_gen.h', output: 'specenum_gen.h',
                         command: [python_exe, files('utility/generate_specenum.py'),
                                   '@OUTPUT@'])

enum_defs = {
  'common': {
    'actions_enums': ['actions_enums.def', 'actions_enums_gen.h'],
    'city_enums': ['city_enums.def', 'city_enums_gen.h'],
    'effects_enums': ['effects_enums.def', 'effects_enums_gen.h'],
    'fc_types_enums': ['fc_types_enums.def', 'fc_types_enums_gen.h'],
    'terrain_enums': ['terrain_enums.def', 'terrain_enums_gen.h'],
  },
  'manual': {
    'manual_enums': ['manual_enums.def', 'manual_enums_gen.h'],
  }
}

enum_targets = {}
foreach group, defs : enum_defs
  tgts = []
  foreach name, paths : defs
    tgts += custom_target(name,
                          input: join_paths('gen_headers/enums', paths[0]),
                          output: paths[1],
                          command: [python_exe, files('gen_headers/generate_enums.py'),
                                    '@OUTPUT@', '@INPUT@'],
                          depend_files: files('gen_headers/generate_enums.py'))
  endforeach
  enum_targets += {group: tgts}
endforeach

pack_common = custom_target('packets_common',
                            input: files('common/networking/packets.def'),
                            output: ['packets_gen.h', 'packets_gen.c'],
                            command: [python_exe, files('common/generate_packets.py'),
                                      '@INPUT@',
                                      '--common-h', '@OUTPUT0@',
                                      '--common-c', '@OUTPUT1@'] + gen_packets_args,
                            depend_files: files('common/generate_packets.py'))

pack_server = custom_target('packets_server',
                            input: files('common/networking/packets.def'),
                            output: ['hand_gen.h', 'hand_gen.c'],
                            command: [python_exe, files('common/generate_packets.py'),
                                      '@INPUT@',
                                      '--server-h', '@OUTPUT0@',
                                      '--server-c', '@OUTPUT1@'] + gen_packets_args,
                            depend_files: files('common/generate_packets.py'))

pack_client = custom_target('packets_client',
                            input: files('common/networking/packets.def'),
                            output: ['packhand_gen.h', 'packhand_gen.c'],
                            command: [python_exe, files('common/generate_packets.py'),
                                      '@INPUT@',
                                      '--client-h', '@OUTPUT0@',
                                      '--client-c', '@OUTPUT1@'] + gen_packets_args,
                            depend_files: files('common/generate_packets.py'))

gitrev = custom_target('gitrev', output: 'fc_gitrev_gen.h',
                       command: [sh_exe, files('bootstrap/generate_gitrev.sh'),
                                 s_root, '@OUTPUT@'],
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
  fcdb_src, tinycthr_files,
  sources: [verhdr],
  include_directories : common_inc,
  dependencies: [ lua_dep, fcdb_dep ]
  )

if meson.is_cross_build() or get_option('sys-tolua-cmd')
  tolua_cmd = find_program('tolua')
else
  tolua_cmd = executable('tolua',
    'dependencies/tolua-5.2/src/bin/tolua.c',
    'dependencies/tolua-5.2/src/bin/toluabind.c',
    include_directories : ['dependencies/lua-5.4/src',
                           'dependencies/tolua-5.2/include'],
    link_with: fc_deps,
    dependencies: m_dep,
    win_subsystem: 'console'
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
  clienticon = windres.process('platforms/windows/icons/clienticon.rc')
  mpicon = windres.process('platforms/windows/icons/mpicon.rc')
  rulediticon = windres.process('platforms/windows/icons/rulediticon.rc')
  servericon = windres.process('platforms/windows/icons/servericon.rc')
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
  'utility/fc_cmdhelp.c',
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
  'common/scriptcore/api_specenum.c',
  'common/scriptcore/luascript.c',
  'common/scriptcore/luascript_func.c',
  'common/scriptcore/luascript_signal.c',
  'common/achievements.c',
  'common/actions.c',
  'common/actres.c',
  'common/ai.c',
  'common/base.c',
  'common/borders.c',
  'common/calendar.c',
  'common/capstr.c',
  'common/citizens.c',
  'common/city.c',
  'common/clientutils.c',
  'common/combat.c',
  'common/counters.c',
  'common/culture.c',
  'common/diptreaty.c',
  'common/disaster.c',
  'common/effects.c',
  'common/events.c',
  'common/extras.c',
  'common/fc_interface.c',
  'common/featured_text.c',
  'common/game.c',
  'common/government.c',
  'common/idex.c',
  'common/improvement.c',
  'common/map.c',
  'common/mapimg.c',
  'common/metaknowledge.c',
  'common/modpack.c',
  'common/movement.c',
  'common/multipliers.c',
  'common/nation.c',
  'common/oblig_reqs.c',
  'common/player.c',
  'common/reqtext.c',
  'common/requirements.c',
  'common/research.c',
  'common/rgbcolor.c',
  'common/road.c',
  'common/server_settings.c',
  'common/sex.c',
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
  sources: [verhdr, gitrev, specenum, enum_targets['common'], pack_common,
            tolua_com_a, tolua_com_z, tolua_game, tolua_signal],
  link_whole: fc_deps,
  dependencies: [zlib_dep,
                 curl_dep, m_dep, icu_dep,
                 net_dep, jansson_dep, lua_dep, bz2_dep, lzma_dep, zstd_dep,
                 bcrypt_lib_dep, iconv_lib_dep,
                 gettext_dep, charset_dep, mw_dep,
                 dependency('threads')],
  install : true
  )

if server_type != 'disabled' or get_option('tools').length() > 0

if server_type == 'disabled'

ais = static_library('fc_ai',
  'ai/stub/stubai.c',
  sources: [verhdr],
  include_directories: [server_inc, include_directories('ai/default')]
  )

else

ais = static_library('fc_ai',
  'ai/classic/classicai.c',
  'ai/tex/texai.c',
  'ai/tex/texaicity.c',
  'ai/tex/texaimsg.c',
  'ai/tex/texaiplayer.c',
  'ai/tex/texaiworld.c',
  'ai/default/aidiplomat.c',
  'ai/default/aiferry.c',
  'ai/default/aiguard.c',
  'ai/default/aihand.c',
  'ai/default/daiair.c',
  'ai/default/daiactions.c',
  'ai/default/daicity.c',
  'ai/default/daidata.c',
  'ai/default/daidiplomacy.c',
  'ai/default/daidomestic.c',
  'ai/default/daieffects.c',
  'ai/default/daihunter.c',
  'ai/default/dailog.c',
  'ai/default/daimilitary.c',
  'ai/default/daiparadrop.c',
  'ai/default/daiplayer.c',
  'ai/default/daisettler.c',
  'ai/default/daitech.c',
  'ai/default/daitools.c',
  'ai/default/daiunit.c',
  sources: [verhdr],
  include_directories: [server_inc, include_directories('ai/default')]
  )

endif

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
  'server/advisors/autoworkers.c',
  'server/advisors/infracache.c',
  'server/generator/fracture_map.c',
  'server/generator/height_map.c',
  'server/generator/mapgen.c',
  'server/generator/mapgen_topology.c',
  'server/generator/mapgen_utils.c',
  'server/generator/startpos.c',
  'server/generator/temperature_map.c',
  'server/ruleset/rscompat.c',
  'server/ruleset/rssanity.c',
  'server/ruleset/ruleload.c',
  'server/savegame/savecompat.c',
  'server/savegame/savegame2.c',
  'server/savegame/savegame3.c',
  'server/savegame/savemain.c',
  'server/scripting/api_fcdb_auth.c',
  'server/scripting/api_fcdb_base.c',
  'server/scripting/api_fcdb_specenum.c',
  'server/scripting/api_server_counters.c',
  'server/scripting/api_server_counters.h',
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
  'server/sanitycheck.c',
  'server/score.c',
  'server/sernet.c',
  'server/setcompat.c',
  'server/settings.c',
  'server/spacerace.c',
  'server/srv_log.c',
  'server/srv_main.c',
  'server/srv_signal.c',
  'server/stdinhand.c',
  'server/techtools.c',
  'server/unithand.c',
  'server/unittools.c',
  'server/voting.c',
  include_directories: server_inc,
  sources: [ verhdr, pack_server,
             tolua.process('server/scripting/tolua_fcdb.pkg',
                           'server/scripting/tolua_server.pkg')],
  dependencies: lua_dep
  )

endif

if server_type != 'disabled'
  executable(server_binary_name,
    'server/srv_entrypoint.c',
    servericon,
    include_directories: server_inc,
    link_with: [server_lib, common_lib, ais],
    dependencies: [m_dep, net_dep, readline_dep, gettext_dep, fcdb_dep,
                   mw_extra_dep],
    install: true,
    win_subsystem: 'console'
    )

  install_data(
    'lua/database.lua',
    install_dir : join_paths(get_option('sysconfdir'), 'freeciv')
    )

  install_data(
    'bootstrap/org.freeciv.server.desktop',
    install_dir : join_paths(get_option('prefix'), 'share/applications')
    )

  custom_target('mi_server',
                input: 'bootstrap/org.freeciv.server.metainfo.xml.in',
                output: '@BASENAME@',
                command: [sh_exe, files('bootstrap/generate_metainfo.sh'),
                          '@OUTPUT@', b_root],
                depend_files: files('fc_version'),
                install: true,
                install_dir: join_paths(get_option('datadir'), 'metainfo'))

endif

install_data(
  'data/ruledit/comments-3.4.txt',
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
  'alaskan',
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
  'conch',
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
  'dagestan',
  'dahomean',
  'danish',
  'darfuri',
  'djiboutian',
  'dominicano',
  'dominican',
  'donetsk',
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
  'florida',
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
  'liege',
  'ligurian',
  'lipkatatar',
  'lippe',
  'lithuanian',
  'lojbanistani',
  'lorrain',
  'louisianian',
  'luhansk',
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
  'minnesotan',
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
  'northirish',
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
  'palauan',
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
  'poyaisian',
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
  'tokiponan',
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
  'tuvaluan',
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
  'client/chatline_common.c',
  'client/citydlg_common.c',
  'client/cityrepdata.c',
  'client/client_main.c',
  'client/climap.c',
  'client/climisc.c',
  'client/clinet.c',
  'client/clitreaty.c',
  'client/colors_common.c',
  'client/connectdlg_common.c',
  'client/control.c',
  'client/editor.c',
  'client/global_worklist.c',
  'client/goto.c',
  'client/gui_properties.c',
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
  'client/svgflag.c',
  'client/text.c',
  'client/themes_common.c',
  'client/tilespec.c',
  'client/unitselect_common.c',
  'client/update_queue.c',
  'client/voteinfo.c',
  'client/zoom.c',
  sources: [verhdr, audio_sdl_src, pack_client,
            tolua.process('client/luascript/tolua_client.pkg')],
  include_directories: client_inc,
  link_with: [common_lib],
  dependencies: [m_dep, audio_dep, lua_dep]
  )

install_data('data/helpdata.txt',
             'data/cimpletoon.modpack',
             'data/cimpletoon.tilespec',
             'data/isophex.modpack',
             'data/isophex.tilespec',
             'data/isotrident.modpack',
             'data/isotrident.tilespec',
             'data/toonhex.modpack',
             'data/toonhex.tilespec',
             'data/alio.modpack',
             'data/alio.tilespec',
             'data/trident.modpack',
             'data/trident.tilespec',
             'data/hexemplio.modpack',
             'data/hexemplio.tilespec',
             'data/hex2t.modpack',
             'data/hex2t.tilespec',
             'data/amplio2.modpack',
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
             'data/misc/extra_buildings.png',
             'data/misc/extra_buildings.spec',
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

scenarios = [
  'british-isles.sav',
  'earth-large.sav',
  'earth-small.sav',
  'europe.sav',
  'europe_1900_WWI.sav',
  'france.sav',
  'hagworld.sav',
  'iberian-peninsula.sav',
  'italy.sav',
  'japan.sav',
  'north_america.sav',
  'tutorial.sav'
]

if host_system != 'windows' or crosser
  foreach scen : scenarios
    scenzip = custom_target('gzip_' + scen,
                            command: [gzip_exe, '--best', '-n', '-c', '@INPUT@' ],
                            output: '@PLAINNAME@.gz',
                            capture: true,
                            input: join_paths('data/scenarios', scen),
                            install: true,
                            install_dir: join_paths(get_option('datadir'), 'freeciv/scenarios'))
  endforeach
else
  # Windows msys2 - cannot compress at the moment (msys2 gzip on CI failing)
  foreach scen : scenarios
    install_data(join_paths('data/scenarios', scen),
                 install_dir: join_paths(get_option('datadir'), 'freeciv/scenarios'))
  endforeach
endif

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
  'alaska',
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
  'conch',
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
  'dagestan',
  'dahomey',
  'darfur',
  'ddr',
  'denmark',
  'dgb',
  'djibouti',
  'dominica',
  'dominican_republic',
  'donetsk',
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
  'florida',
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
  'louisiana',
  'luhansk',
  'luik',
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
  'minnesota',
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
  'northernireland',
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
  'palau',
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
  'poyais',
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
  'tokipona',
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
  'tuvalu',
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
  install_data(join_paths('data/flags', flag + '.png'),
               join_paths('data/flags', flag + '-shield.png'),
               join_paths('data/flags', flag + '-large.png'),
               join_paths('data/flags', flag + '-shield-large.png'),
               install_dir : join_paths(get_option('datadir'), 'freeciv/flags'))
  if svgflags
    install_data(join_paths('data/flags', flag + '.svg'),
                 install_dir : join_paths(get_option('datadir'), 'freeciv/flags'))
  endif
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
             'data/hexemplio/savannah.png',
             'data/hexemplio/savannah.spec',
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
             'data/hex2t/highways.png',
             'data/hex2t/highways.spec',
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
             'data/trident/highways.png',
             'data/trident/highways.spec',
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

ct_extra_units = [
  'abrams',
  'assaultgun',
  'biplane',
  'container_ship',
  'flak88',
  'icebreaker',
  'light_howitzer',
  'locomotive',
  'm4sherman',
  'mig15',
  'missile_sub',
  'panther',
  'panzer_iii',
  'pt-boat',
  'ROMB',
  'stationary_sam',
  'steamer',
  'storm',
  'technical',
  'tiger',
  'truck',
  'zeppelin',
  ]

ct_extra_per_unit_files = [
  '0001.png',
  '0002.png',
  '0003.png',
  '0004.png',
  '0005.png',
  '0006.png',
  '0007.png',
  '0008.png',
  'xxx.spec',
  ]

foreach cteu : ct_extra_units
  foreach cteuf : ct_extra_per_unit_files
    install_data(
      join_paths('data/cimpletoon/units', cteu, cteuf),
      install_dir : join_paths(get_option('datadir'), 'freeciv/cimpletoon/units', cteu)
      )
  endforeach
endforeach

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
             'data/amplio2/volcano.png',
             'data/amplio2/volcano.spec',
             'data/amplio2/water.png',
             'data/amplio2/water.spec',
             install_dir : join_paths(get_option('datadir'), 'freeciv/amplio2'))

install_data('data/stdmusic/CullamBruce-Lockhart--Dawning_Fanfare.ogg',
             install_dir : join_paths(get_option('datadir'), 'freeciv/stdmusic'))

install_data('data/stdsounds/complete.ogg',
             'data/stdsounds/fanfare.ogg',
             'data/stdsounds/fanfarehappy.ogg',
             'data/stdsounds/foot3.ogg',
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

foreach manp : man_pages
  install_data(
    join_paths(b_root, manp + '.6'),
    install_dir : join_paths(get_option('mandir'), 'man6')
    )
endforeach

foreach manp : man_links
  install_data(
    join_paths('doc/man/', manp + '.6'),
    install_dir : join_paths(get_option('mandir'), 'man6')
    )
endforeach

gtk322_dep = dependency('gtk+-3.0', version : '>= 3.22', required : false)
gtk4_dep = dependency('gtk4', version : '>= 4.0.0', required : false)
gtk5_dep = dependency('gtk4', version : '>= 4.14.0', required : false)

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
  dependencies: [gtk322_dep, net_dep, gettext_dep, mw_extra_dep],
  link_with: client_common,
  install: true,
  win_subsystem: 'windows'
  )

install_data('data/gtk3.22_menus.xml',
             install_dir : join_paths(get_option('datadir'), 'freeciv'))

install_data('data/themes/gtk3.22/Freeciv/gtk-3.0/gtk.css',
             'data/themes/gtk3.22/Freeciv/gtk-3.0/menubar.css',
             'data/themes/gtk3.22/Freeciv/gtk-3.0/bg.png',
             install_dir : join_paths(get_option('datadir'),
                           'freeciv/themes/gtk3.22/Freeciv/gtk-3.0'))

install_data(
  'bootstrap/org.freeciv.gtk322.desktop',
  install_dir : join_paths(get_option('prefix'), 'share/applications')
  )

custom_target('mi_gtk322',
              input: 'bootstrap/org.freeciv.gtk322.metainfo.xml.in',
              output: '@BASENAME@',
              command: [sh_exe, files('bootstrap/generate_metainfo.sh'),
                        '@OUTPUT@', b_root],
              depend_files: files('fc_version'),
              install: true,
              install_dir: join_paths(get_option('datadir'), 'metainfo'))

endif

gtk4_theme = false
if get_option('clients').contains('gtk4')

if not gtk4_dep.found()
  error('gtk-4.0 >= 4.0 required for gtk4-client, but not found')
endif

executable('freeciv-gtk4',
  'client/gui-gtk-4.0/action_dialog.c',
  'client/gui-gtk-4.0/canvas.c',
  'client/gui-gtk-4.0/chatline.c',
  'client/gui-gtk-4.0/choice_dialog.c',
  'client/gui-gtk-4.0/citizensinfo.c',
  'client/gui-gtk-4.0/citydlg.c',
  'client/gui-gtk-4.0/cityrep.c',
  'client/gui-gtk-4.0/cma_fe.c',
  'client/gui-gtk-4.0/colors.c',
  'client/gui-gtk-4.0/connectdlg.c',
  'client/gui-gtk-4.0/dialogs.c',
  'client/gui-gtk-4.0/diplodlg.c',
  'client/gui-gtk-4.0/editgui.c',
  'client/gui-gtk-4.0/editprop.c',
  'client/gui-gtk-4.0/finddlg.c',
  'client/gui-gtk-4.0/gamedlgs.c',
  'client/gui-gtk-4.0/gotodlg.c',
  'client/gui-gtk-4.0/graphics.c',
  'client/gui-gtk-4.0/gui_main.c',
  'client/gui-gtk-4.0/gui_stuff.c',
  'client/gui-gtk-4.0/happiness.c',
  'client/gui-gtk-4.0/helpdlg.c',
  'client/gui-gtk-4.0/infradlg.c',
  'client/gui-gtk-4.0/inputdlg.c',
  'client/gui-gtk-4.0/inteldlg.c',
  'client/gui-gtk-4.0/luaconsole.c',
  'client/gui-gtk-4.0/mapctrl.c',
  'client/gui-gtk-4.0/mapview.c',
  'client/gui-gtk-4.0/menu.c',
  'client/gui-gtk-4.0/messagedlg.c',
  'client/gui-gtk-4.0/messagewin.c',
  'client/gui-gtk-4.0/optiondlg.c',
  'client/gui-gtk-4.0/pages.c',
  'client/gui-gtk-4.0/plrdlg.c',
  'client/gui-gtk-4.0/rallypointdlg.c',
  'client/gui-gtk-4.0/repodlgs.c',
  'client/gui-gtk-4.0/soundset_dlg.c',
  'client/gui-gtk-4.0/spaceshipdlg.c',
  'client/gui-gtk-4.0/sprite.c',
  'client/gui-gtk-4.0/theme_dlg.c',
  'client/gui-gtk-4.0/themes.c',
  'client/gui-gtk-4.0/tileset_dlg.c',
  'client/gui-gtk-4.0/transportdlg.c',
  'client/gui-gtk-4.0/unitselect.c',
  'client/gui-gtk-4.0/unitselextradlg.c',
  'client/gui-gtk-4.0/unitselunitdlg.c',
  'client/gui-gtk-4.0/voteinfo_bar.c',
  'client/gui-gtk-4.0/wldlg.c',
  clienticon,
  c_args: [ '-DGDK_VERSION_MIN_REQUIRED=GDK_VERSION_4_0',
            '-DGDK_VERSION_MAX_ALLOWED=GDK_VERSION_4_0',
            '-DGLIB_VERSION_MIN_REQUIRED=GLIB_VERSION_2_66',
            '-DGLIB_VERSION_MAX_ALLOWED=GLIB_VERSION_2_66'],
  include_directories: client_inc,
  dependencies: [gtk4_dep, net_dep, gettext_dep, mw_extra_dep],
  link_with: client_common,
  install: true,
  win_subsystem: 'windows'
  )

  gtk4_theme = true

install_data(
  'bootstrap/org.freeciv.gtk4.desktop',
  install_dir : join_paths(get_option('prefix'), 'share/applications')
  )

custom_target('mi_gtk4',
              input: 'bootstrap/org.freeciv.gtk4.metainfo.xml.in',
              output: '@BASENAME@',
              command: [sh_exe, files('bootstrap/generate_metainfo.sh'),
                        '@OUTPUT@', b_root],
              depend_files: files('fc_version'),
              install: true,
              install_dir: join_paths(get_option('datadir'), 'metainfo'))

endif

if get_option('clients').contains('gtk4x')

if not gtk5_dep.found()
  error('gtk-4.0 >= 4.14 required for gtk4x-client, but not found')
endif

executable('freeciv-gtk4x',
  'client/gui-gtk-5.0/action_dialog.c',
  'client/gui-gtk-5.0/canvas.c',
  'client/gui-gtk-5.0/chatline.c',
  'client/gui-gtk-5.0/choice_dialog.c',
  'client/gui-gtk-5.0/citizensinfo.c',
  'client/gui-gtk-5.0/citydlg.c',
  'client/gui-gtk-5.0/cityrep.c',
  'client/gui-gtk-5.0/cma_fe.c',
  'client/gui-gtk-5.0/colors.c',
  'client/gui-gtk-5.0/connectdlg.c',
  'client/gui-gtk-5.0/dialogs.c',
  'client/gui-gtk-5.0/diplodlg.c',
  'client/gui-gtk-5.0/editgui.c',
  'client/gui-gtk-5.0/editprop.c',
  'client/gui-gtk-5.0/finddlg.c',
  'client/gui-gtk-5.0/gamedlgs.c',
  'client/gui-gtk-5.0/gotodlg.c',
  'client/gui-gtk-5.0/graphics.c',
  'client/gui-gtk-5.0/gui_main.c',
  'client/gui-gtk-5.0/gui_stuff.c',
  'client/gui-gtk-5.0/happiness.c',
  'client/gui-gtk-5.0/helpdlg.c',
  'client/gui-gtk-5.0/infradlg.c',
  'client/gui-gtk-5.0/inputdlg.c',
  'client/gui-gtk-5.0/inteldlg.c',
  'client/gui-gtk-5.0/luaconsole.c',
  'client/gui-gtk-5.0/mapctrl.c',
  'client/gui-gtk-5.0/mapview.c',
  'client/gui-gtk-5.0/menu.c',
  'client/gui-gtk-5.0/messagedlg.c',
  'client/gui-gtk-5.0/messagewin.c',
  'client/gui-gtk-5.0/optiondlg.c',
  'client/gui-gtk-5.0/pages.c',
  'client/gui-gtk-5.0/plrdlg.c',
  'client/gui-gtk-5.0/rallypointdlg.c',
  'client/gui-gtk-5.0/repodlgs.c',
  'client/gui-gtk-5.0/soundset_dlg.c',
  'client/gui-gtk-5.0/spaceshipdlg.c',
  'client/gui-gtk-5.0/sprite.c',
  'client/gui-gtk-5.0/theme_dlg.c',
  'client/gui-gtk-5.0/themes.c',
  'client/gui-gtk-5.0/tileset_dlg.c',
  'client/gui-gtk-5.0/transportdlg.c',
  'client/gui-gtk-5.0/unitselect.c',
  'client/gui-gtk-5.0/unitselextradlg.c',
  'client/gui-gtk-5.0/unitselunitdlg.c',
  'client/gui-gtk-5.0/voteinfo_bar.c',
  'client/gui-gtk-5.0/wldlg.c',
  clienticon,
  c_args: [ '-DGDK_VERSION_MIN_REQUIRED=GDK_VERSION_4_8',
            '-DGDK_VERSION_MAX_ALLOWED=GDK_VERSION_4_14',
            '-DGLIB_VERSION_MIN_REQUIRED=GLIB_VERSION_2_76',
            '-DGLIB_VERSION_MAX_ALLOWED=GLIB_VERSION_2_76'],
  include_directories: client_inc,
  dependencies: [gtk5_dep, net_dep, gettext_dep, mw_extra_dep],
  link_with: client_common,
  install: true,
  win_subsystem: 'windows'
  )

  gtk4_theme = true

install_data(
  'bootstrap/org.freeciv.gtk4x.desktop',
  install_dir : join_paths(get_option('prefix'), 'share/applications')
  )

custom_target('mi_gtk4x',
              input: 'bootstrap/org.freeciv.gtk4x.metainfo.xml.in',
              output: '@BASENAME@',
              command: [sh_exe, files('bootstrap/generate_metainfo.sh'),
                        '@OUTPUT@', b_root],
              depend_files: files('fc_version'),
              install: true,
              install_dir: join_paths(get_option('datadir'), 'metainfo'))

endif

if gtk4_theme
  install_data('data/themes/gtk4/Freeciv/gtk-4.0/gtk.css',
               'data/themes/gtk4/Freeciv/gtk-4.0/menubar.css',
               'data/themes/gtk4/Freeciv/gtk-4.0/bg.png',
               install_dir : join_paths(get_option('datadir'),
                             'freeciv/themes/gtk4/Freeciv/gtk-4.0'))
endif

if qtver == 'qt6' or qtver == 'qt6x'
  qt_mod = import('qt6')

  qt_dep = dependency('Qt6', modules: ['Core', 'Gui', 'Widgets'], required: false)
else
  qt_mod = import('qt5')

  qt_dep = dependency('Qt5', modules: ['Core', 'Gui', 'Widgets'], required: false)
endif

if qt_dep.found() and cxx_build
  if cxx_compiler.compiles(
'''#include <QtCore>
int main()
{
#if QT_VERSION < ''' + qt_minver + '''
  fail
#endif
  return 0;
}
''',
    name: 'qt minver',
    args: qt_cppflags,
    dependencies: qt_dep)
    qtfine = true
  else
    qtfine = false
  endif
else
  qtfine = false
endif

if get_option('clients').contains('qt')

if not qtfine
  if qtver == 'qt6'
    error('Qt6 >= 6.0 required for qt-client in Qt6 mode, but not found')
  elif qtver == 'qt6x'
    error('Qt6 >= 6.7 required for qt-client in Qt6x mode, but not found')
  else
    error('Qt5 >= 5.15 required for qt-client in Qt5 mode, but not found')
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
  'client/gui_interface.c',
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
  mocced_client, clienticon,
  include_directories: [client_inc, include_directories('client/gui-qt')],
  dependencies: [qt_dep, net_dep, gettext_dep, mw_extra_dep],
  override_options: qt_opts,
  link_with: client_common,
  install: true,
  win_subsystem: 'windows'
  )

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

install_data(
  'bootstrap/org.freeciv.qt.desktop',
  install_dir : join_paths(get_option('prefix'), 'share/applications')
  )

custom_target('mi_qt',
              input: 'bootstrap/org.freeciv.qt.metainfo.xml.in',
              output: '@BASENAME@',
              command: [sh_exe, files('bootstrap/generate_metainfo.sh'),
                        '@OUTPUT@', b_root],
              depend_files: files('fc_version'),
              install: true,
              install_dir: join_paths(get_option('datadir'), 'metainfo'))

endif

if get_option('clients').contains('sdl2')

sdl2_gfx_dep = []
sdl2_gfx_src = []
sdl2_gfx_inc = []
sdl2_gfx_cargs = []
sdl2_image_dep = []
sdl2_ttf_dep = []

if not emscripten
  sdl2_gfx_dep = c_compiler.find_library('SDL2_gfx', dirs: cross_lib_path,
                                         required: false)

  if not sdl2_gfx_dep.found()
    sdl2_gfx_src = 'dependencies/SDL2_gfx/SDL2_rotozoom.c'
    sdl2_gfx_inc = include_directories('dependencies')
    sdl2_gfx_cargs = '-DSDL2_GFX_FROM_TREE'
  endif

  sdl2_image_dep = c_compiler.find_library('SDL2_image', dirs: cross_lib_path,
                                           required: false)
  sdl2_ttf_dep = c_compiler.find_library('SDL2_ttf', dirs: cross_lib_path,
                                         required: false)
endif

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
  sdl2_gfx_src,
  clienticon,
  c_args: sdl2_gfx_cargs,
  include_directories: [client_inc, sdl2_gfx_inc ],
  dependencies: [sdl2main_dep, audio_dep,
                 sdl2_image_dep, sdl2_gfx_dep, sdl2_ttf_dep,
                 net_dep, gettext_dep, mw_extra_dep],
  link_with: client_common,
  install: true,
  win_subsystem: 'windows'
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

install_data(
  'bootstrap/org.freeciv.sdl2.desktop',
  install_dir : join_paths(get_option('prefix'), 'share/applications')
  )

custom_target('mi_sdl2',
              input: 'bootstrap/org.freeciv.sdl2.metainfo.xml.in',
              output: '@BASENAME@',
              command: [sh_exe, files('bootstrap/generate_metainfo.sh'),
                        '@OUTPUT@', b_root],
              depend_files: files('fc_version'),
              install: true,
              install_dir: join_paths(get_option('datadir'), 'metainfo'))

endif

if get_option('clients').contains('sdl3')

sdl3_gfx_dep = []
sdl3_gfx_inc = []
sdl3_gfx_cargs = []
sdl3_image_dep = []
sdl3_ttf_dep = []

if not emscripten
  sdl3_gfx_inc = include_directories('dependencies')

  sdl3_image_dep = c_compiler.find_library('SDL3_image', dirs: cross_lib_path,
                                           required: false)
  sdl3_ttf_dep = c_compiler.find_library('SDL3_ttf', dirs: cross_lib_path,
                                         required: false)
endif

executable('freeciv-sdl3',
  'client/gui-sdl3/action_dialog.c',
  'client/gui-sdl3/canvas.c',
  'client/gui-sdl3/chatline.c',
  'client/gui-sdl3/citydlg.c',
  'client/gui-sdl3/cityrep.c',
  'client/gui-sdl3/cma_fe.c',
  'client/gui-sdl3/colors.c',
  'client/gui-sdl3/connectdlg.c',
  'client/gui-sdl3/dialogs.c',
  'client/gui-sdl3/diplodlg.c',
  'client/gui-sdl3/finddlg.c',
  'client/gui-sdl3/gotodlg.c',
  'client/gui-sdl3/graphics.c',
  'client/gui-sdl3/gui_main.c',
  'client/gui-sdl3/gui_mouse.c',
  'client/gui-sdl3/gui_string.c',
  'client/gui-sdl3/gui_tilespec.c',
  'client/gui-sdl3/helpdlg.c',
  'client/gui-sdl3/infradlg.c',
  'client/gui-sdl3/inteldlg.c',
  'client/gui-sdl3/luaconsole.c',
  'client/gui-sdl3/mapctrl.c',
  'client/gui-sdl3/mapview.c',
  'client/gui-sdl3/menu.c',
  'client/gui-sdl3/messagewin.c',
  'client/gui-sdl3/optiondlg.c',
  'client/gui-sdl3/pages.c',
  'client/gui-sdl3/plrdlg.c',
  'client/gui-sdl3/repodlgs.c',
  'client/gui-sdl3/spaceshipdlg.c',
  'client/gui-sdl3/sprite.c',
  'client/gui-sdl3/themebackgrounds.c',
  'client/gui-sdl3/themecolors.c',
  'client/gui-sdl3/themes.c',
  'client/gui-sdl3/themespec.c',
  'client/gui-sdl3/utf8string.c',
  'client/gui-sdl3/voteinfo_bar.c',
  'client/gui-sdl3/widget_button.c',
  'client/gui-sdl3/widget.c',
  'client/gui-sdl3/widget_checkbox.c',
  'client/gui-sdl3/widget_combo.c',
  'client/gui-sdl3/widget_core.c',
  'client/gui-sdl3/widget_edit.c',
  'client/gui-sdl3/widget_icon.c',
  'client/gui-sdl3/widget_label.c',
  'client/gui-sdl3/widget_scrollbar.c',
  'client/gui-sdl3/widget_window.c',
  'client/gui-sdl3/wldlg.c',
  'client/gui-sdl3/SDL3_gfx/SDL3_rotozoom.c',
  clienticon,
  include_directories: [client_inc, sdl3_gfx_inc ],
  dependencies: [sdl3main_dep, audio_dep,
                 sdl3_image_dep, sdl3_gfx_dep, sdl3_ttf_dep,
                 net_dep, gettext_dep, mw_extra_dep],
  link_with: client_common,
  install: true,
  win_subsystem: 'windows'
  )

install_data('data/themes/gui-sdl3/human/backgrounds.themespec',
             'data/themes/gui-sdl3/human/bg2.png',
             'data/themes/gui-sdl3/human/bg.png',
             'data/themes/gui-sdl3/human/city_fist.png',
             'data/themes/gui-sdl3/human/city_fist.tspec',
             'data/themes/gui-sdl3/human/city.png',
             'data/themes/gui-sdl3/human/city.tspec',
             'data/themes/gui-sdl3/human/colors.themespec',
             'data/themes/gui-sdl3/human/COPYING.DejaVu',
             'data/themes/gui-sdl3/human/COPYING.fireflysung',
             'data/themes/gui-sdl3/human/COPYING.sazanami',
             'data/themes/gui-sdl3/human/COPYING.UnDotum',
             'data/themes/gui-sdl3/human/DejaVuSans.ttf',
             'data/themes/gui-sdl3/human/dip_icons.png',
             'data/themes/gui-sdl3/human/dip_icons.tspec',
             'data/themes/gui-sdl3/human/fireflysung.ttf',
             'data/themes/gui-sdl3/human/icons.png',
             'data/themes/gui-sdl3/human/icons.tspec',
             'data/themes/gui-sdl3/human/intro.png',
             'data/themes/gui-sdl3/human/options_bg.png',
             'data/themes/gui-sdl3/human/sazanami-gothic.ttf',
             'data/themes/gui-sdl3/human/small_theme_buttons.png',
             'data/themes/gui-sdl3/human/small_theme_buttons.tspec',
             'data/themes/gui-sdl3/human/tech_tree.png',
             'data/themes/gui-sdl3/human/tech_tree.tspec',
             'data/themes/gui-sdl3/human/theme_boxs.png',
             'data/themes/gui-sdl3/human/theme_boxs.tspec',
             'data/themes/gui-sdl3/human/theme_buttons.png',
             'data/themes/gui-sdl3/human/theme_buttons.tspec',
             'data/themes/gui-sdl3/human/theme_orders_buttons.png',
             'data/themes/gui-sdl3/human/theme_orders_buttons.tspec',
             'data/themes/gui-sdl3/human/theme_scrolls.png',
             'data/themes/gui-sdl3/human/theme_scrolls.tspec',
             'data/themes/gui-sdl3/human/theme.themespec',
             'data/themes/gui-sdl3/human/UnDotum.ttf',
             'data/themes/gui-sdl3/human/world.png',
             install_dir : join_paths(get_option('datadir'),
                           'freeciv/themes/gui-sdl3/human'))

install_data(
  'bootstrap/org.freeciv.sdl3.desktop',
  install_dir : join_paths(get_option('prefix'), 'share/applications')
  )

custom_target('mi_sdl3',
              input: 'bootstrap/org.freeciv.sdl3.metainfo.xml.in',
              output: '@BASENAME@',
              command: [sh_exe, files('bootstrap/generate_metainfo.sh'),
                        '@OUTPUT@', b_root],
              depend_files: files('fc_version'),
              install: true,
              install_dir: join_paths(get_option('datadir'), 'metainfo'))

endif

if get_option('clients').contains('stub')

executable('freeciv-stub',
  'client/gui_cbsetter.c',
  'client/gui_interface.c',
  'client/gui-stub/canvas.c',
  'client/gui-stub/chatline.c',
  'client/gui-stub/citydlg.c',
  'client/gui-stub/cityrep.c',
  'client/gui-stub/colors.c',
  'client/gui-stub/connectdlg.c',
  'client/gui-stub/dialogs.c',
  'client/gui-stub/diplodlg.c',
  'client/gui-stub/finddlg.c',
  'client/gui-stub/gotodlg.c',
  'client/gui-stub/graphics.c',
  'client/gui-stub/gui_main.c',
  'client/gui-stub/helpdlg.c',
  'client/gui-stub/infradlg.c',
  'client/gui-stub/inteldlg.c',
  'client/gui-stub/luaconsole.c',
  'client/gui-stub/mapctrl.c',
  'client/gui-stub/mapview.c',
  'client/gui-stub/menu.c',
  'client/gui-stub/messagedlg.c',
  'client/gui-stub/messagewin.c',
  'client/gui-stub/optiondlg.c',
  'client/gui-stub/pages.c',
  'client/gui-stub/plrdlg.c',
  'client/gui-stub/ratesdlg.c',
  'client/gui-stub/repodlgs.c',
  'client/gui-stub/spaceshipdlg.c',
  'client/gui-stub/sprite.c',
  'client/gui-stub/themes.c',
  'client/gui-stub/voteinfo_bar.c',
  'client/gui-stub/wldlg.c',
  clienticon,
  include_directories: client_inc,
  dependencies: [audio_dep, net_dep, gettext_dep, mw_extra_dep],
  link_with: client_common,
  install: true,
  win_subsystem: 'console'
  )

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
  c_args: [ '-DGDK_VERSION_MIN_REQUIRED=GDK_VERSION_3_20',
            '-DGDK_VERSION_MAX_ALLOWED=GDK_VERSION_3_22',
            '-DGLIB_VERSION_MIN_REQUIRED=GLIB_VERSION_2_50',
            '-DGLIB_VERSION_MAX_ALLOWED=GLIB_VERSION_2_50'],
  include_directories: tool_inc,
  sources: [verhdr],
  dependencies: [gtk322_dep, sqlite3_dep, gettext_dep],
  link_with: [common_lib, fcmp_common],
  install: true,
  win_subsystem: 'windows'
  )

install_data(
  'bootstrap/org.freeciv.gtk3.mp.desktop',
  install_dir : join_paths(get_option('prefix'), 'share/applications')
  )

custom_target('mi_mp_gtk3',
              input: 'bootstrap/org.freeciv.gtk3.mp.metainfo.xml.in',
              output: '@BASENAME@',
              command: [sh_exe, files('bootstrap/generate_metainfo.sh'),
                        '@OUTPUT@', b_root],
              depend_files: files('fc_version'),
              install: true,
              install_dir: join_paths(get_option('datadir'), 'metainfo'))

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
  sources: [verhdr],
  dependencies: [gtk4_dep, sqlite3_dep, gettext_dep],
  link_with: [common_lib, fcmp_common],
  install: true,
  win_subsystem: 'windows'
  )

install_data(
  'bootstrap/org.freeciv.gtk4.mp.desktop',
  install_dir : join_paths(get_option('prefix'), 'share/applications')
  )

custom_target('mi_mp_gtk4',
              input: 'bootstrap/org.freeciv.gtk4.mp.metainfo.xml.in',
              output: '@BASENAME@',
              command: [sh_exe, files('bootstrap/generate_metainfo.sh'),
                        '@OUTPUT@', b_root],
              depend_files: files('fc_version'),
              install: true,
              install_dir: join_paths(get_option('datadir'), 'metainfo'))

endif

if get_option('fcmp').contains('gtk4x')

if not gtk5_dep.found()
  error('gtk-4.0 >= 4.14 required for gtk4x modpack installer, but not found')
endif

executable('freeciv-mp-gtk4x',
  'tools/fcmp/mpgui_gtk5.c',
  mpicon,
  c_args: [ '-DGDK_VERSION_MIN_REQUIRED=GDK_VERSION_4_14',
            '-DGDK_VERSION_MAX_ALLOWED=GDK_VERSION_4_14',
            '-DGLIB_VERSION_MIN_REQUIRED=GLIB_VERSION_2_76',
            '-DGLIB_VERSION_MAX_ALLOWED=GLIB_VERSION_2_76'],
  include_directories: tool_inc,
  sources: [verhdr],
  dependencies: [gtk5_dep, sqlite3_dep, gettext_dep],
  link_with: [common_lib, fcmp_common],
  install: true,
  win_subsystem: 'windows'
  )

install_data(
  'bootstrap/org.freeciv.gtk4x.mp.desktop',
  install_dir : join_paths(get_option('prefix'), 'share/applications')
  )

custom_target('mi_mp_gtk4x',
              input: 'bootstrap/org.freeciv.gtk4x.mp.metainfo.xml.in',
              output: '@BASENAME@',
              command: [sh_exe, files('bootstrap/generate_metainfo.sh'),
                        '@OUTPUT@', b_root],
              depend_files: files('fc_version'),
              install: true,
              install_dir: join_paths(get_option('datadir'), 'metainfo'))

endif

if get_option('fcmp').contains('qt')

if not qtfine
  if qtver == 'qt6'
    error('Qt6 >= 6.0 required for qt-modpack-installer in Qt6 mode, but not found')
  elif qtver == 'qt6x'
    error('Qt6 >= 6.7 required for qt-modpack-installer in Qt6x mode, but not found')
  else
    error('Qt5 >= 5.15 required for qt-modpack-installer in Qt5 mode, but not found')
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
  sources: [verhdr],
  dependencies: [qt_dep, sqlite3_dep, gettext_dep],
  link_with: [common_lib, fcmp_common],
  override_options: qt_opts,
  install: true,
  win_subsystem: 'windows'
  )

install_data(
  'bootstrap/org.freeciv.qt.mp.desktop',
  install_dir : join_paths(get_option('prefix'), 'share/applications')
  )

custom_target('mi_mp_qt',
              input: 'bootstrap/org.freeciv.qt.mp.metainfo.xml.in',
              output: '@BASENAME@',
              command: [sh_exe, files('bootstrap/generate_metainfo.sh'),
                        '@OUTPUT@', b_root],
              depend_files: files('fc_version'),
              install: true,
              install_dir: join_paths(get_option('datadir'), 'metainfo'))

endif

if get_option('fcmp').contains('cli')

executable('freeciv-mp-cli',
  'tools/fcmp/mpcli.c',
  mpicon,
  include_directories: tool_inc,
  sources: [verhdr],
  dependencies: [sqlite3_dep, gettext_dep],
  link_with: [common_lib, fcmp_common],
  install: true,
  win_subsystem: 'console'
  )

endif

if get_option('tools').length() > 0

tool_lib = static_library('fc_toolutil',
  'tools/ruleutil/comments.c',
  'tools/ruleutil/rulesave.c',
  'tools/shared/tools_fc_interface.c',
  sources: [verhdr],
  include_directories: server_inc,
  )

else
  tool_lib = []
endif

if get_option('tools').contains('ruleup')

executable('freeciv-ruleup',
  'tools/ruleup.c',
  link_with: [common_lib, server_lib, tool_lib, ais],
  include_directories: tool_inc,
  dependencies: [m_dep, net_dep, readline_dep, gettext_dep, fcdb_dep,
                 mw_extra_dep],
  install: true,
  win_subsystem: 'console'
  )

endif

if get_option('tools').contains('ruledit')

if not qtfine
  if qtver == 'qt6'
    error('Qt6 >= 6.0 required for ruledit in Qt6 mode, but not found')
  elif qtver == 'qt6x'
    error('Qt6 >= 6.7 required for ruledit in Qt6x mode, but not found')
  else
    error('Qt5 >= 5.15 required for ruledit in Qt5 mode, but not found')
  endif
endif

mocced_ruledit = qt_mod.preprocess(
  moc_headers: [
   'tools/ruledit/conversion_log.h',
   'tools/ruledit/edit_extra.h',
   'tools/ruledit/edit_gov.h',
   'tools/ruledit/edit_impr.h',
   'tools/ruledit/edit_tech.h',
   'tools/ruledit/edit_terrain.h',
   'tools/ruledit/edit_utype.h',
   'tools/ruledit/effect_edit.h',
   'tools/ruledit/helpeditor.h',
   'tools/ruledit/req_edit.h',
   'tools/ruledit/req_vec_fix.h',
   'tools/ruledit/requirers_dlg.h',
   'tools/ruledit/ruledit_qt.h',
   'tools/ruledit/tab_achievement.h',
   'tools/ruledit/tab_counters.h',
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
   'tools/ruledit/tab_unit.h',
   'tools/ruledit/values_dlg.h']
  )

executable('freeciv-ruledit',
  'tools/ruledit/conversion_log.cpp',
  'tools/ruledit/edit_extra.cpp',
  'tools/ruledit/edit_gov.cpp',
  'tools/ruledit/edit_impr.cpp',
  'tools/ruledit/edit_tech.cpp',
  'tools/ruledit/edit_terrain.cpp',
  'tools/ruledit/edit_utype.cpp',
  'tools/ruledit/effect_edit.cpp',
  'tools/ruledit/helpeditor.cpp',
  'tools/ruledit/req_edit.cpp',
  'tools/ruledit/requirers_dlg.cpp',
  'tools/ruledit/req_vec_fix.cpp',
  'tools/ruledit/ruledit.cpp',
  'tools/ruledit/ruledit_qt.cpp',
  'tools/ruledit/tab_achievement.cpp',
  'tools/ruledit/tab_building.cpp',
  'tools/ruledit/tab_counters.cpp',
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
  'tools/ruledit/values_dlg.cpp',
  mocced_ruledit, rulediticon,
  include_directories: tool_inc,
  dependencies: [qt_dep, m_dep, net_dep, readline_dep, gettext_dep,
                 fcdb_dep, mw_extra_dep],
  link_with: [common_lib, server_lib, ais, tool_lib],
  override_options: qt_opts,
  install: true,
  win_subsystem: 'windows'
  )

install_data(
  'data/freeciv-ruledit.png',
  install_dir : join_paths(get_option('datadir'), 'freeciv')
  )

install_data(
  'bootstrap/org.freeciv.ruledit.desktop',
  install_dir : join_paths(get_option('prefix'), 'share/applications')
  )

custom_target('mi_ruledit',
              input: 'bootstrap/org.freeciv.ruledit.metainfo.xml.in',
              output: '@BASENAME@',
              command: [sh_exe, files('bootstrap/generate_metainfo.sh'),
                        '@OUTPUT@', b_root],
              depend_files: files('fc_version'),
              install: true,
              install_dir: join_paths(get_option('datadir'), 'metainfo'))

endif

if get_option('tools').contains('manual')

executable('freeciv-manual',
  'tools/manual/fc_manual.c',
  'tools/manual/manual_buildings.c',
  'tools/manual/manual_commands.c',
  'tools/manual/manual_extras.c',
  'tools/manual/manual_governments.c',
  'tools/manual/manual_settings.c',
  'tools/manual/manual_techs.c',
  'tools/manual/manual_terrain.c',
  'tools/manual/manual_uclass.c',
  'tools/manual/manual_units.c',
  'client/helpdata.c',
  sources: [enum_targets['manual']],
  link_with: [common_lib, server_lib, tool_lib, ais],
  include_directories: [tool_inc,
                        include_directories('client', 'client/include')],
  dependencies: [m_dep, net_dep, readline_dep, gettext_dep, fcdb_dep,
                 mw_extra_dep],
  install: true,
  win_subsystem: 'console'
  )

endif

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
  'granularity',
  'stub'
  ]

ruleset_files = [
  'actions.ruleset',
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
  'data/default.modpack',
  'data/civ2civ3.modpack',
  'data/classic.modpack',
  'data/multiplayer.modpack',
  'data/alien.modpack',
  'data/sandbox.modpack',
  'data/civ1.modpack',
  'data/civ2.modpack',
  'data/granularity.modpack',
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
  'data/granularity/README.granularity',
  install_dir : join_paths(get_option('datadir'), 'freeciv/granularity')
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
  'doc/README.crosser',
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

custom_target('gzip_ChangeLog-1.0-S3_0',
              command: [gzip_exe, '--best', '-n', '-c', '@INPUT@' ],
              output: '@PLAINNAME@.gz',
              capture: true,
              input: 'ChangeLog-1.0-S3_0',
              install: true,
              install_dir: join_paths(get_option('datadir'), 'doc/freeciv'))
