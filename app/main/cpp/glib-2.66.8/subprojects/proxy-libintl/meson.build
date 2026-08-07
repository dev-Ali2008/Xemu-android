project('proxy-libintl', 'c',
  version : '1',
  meson_version : '>= 0.27.0',
  default_options : [ 'warning_level=1',
                      'c_std=gnu99',
                      'buildtype=debugoptimized' ])

install_headers('libintl.h')

intl_shr = shared_library('intl',
  'libintl.c',
  c_args: ['-DSTUB_ONLY'],
  install : true)

intl_sta = static_library ('intl',
  objects: intl_shr.extract_all_objects(),
  c_args: ['-DSTUB_ONLY'],
  install : true)

if get_option('default_library') == 'static'
  intl_lib = intl_sta
else
  intl_lib = intl_shr
endif

intl_dep = declare_dependency(link_with : intl_lib,
  include_directories : include_directories('.'))
