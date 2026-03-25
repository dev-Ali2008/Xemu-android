if(NOT DEFINED GLIB_SOURCE_DIR)
  message(FATAL_ERROR "GLIB_SOURCE_DIR is not set")
endif()

string(REPLACE "\"" "" GLIB_SOURCE_DIR "${GLIB_SOURCE_DIR}")

set(MESON_FILE "${GLIB_SOURCE_DIR}/glib/meson.build")
if(NOT EXISTS "${MESON_FILE}")
  message(FATAL_ERROR "glib meson.build not found at ${MESON_FILE}")
endif()

file(READ "${MESON_FILE}" MESON_CONTENTS)

string(REPLACE
  "if host_system != 'windows'"
  "if false"
  MESON_CONTENTS
  "${MESON_CONTENTS}"
)

string(REPLACE
  "gdb_install_dir = join_paths(glib_datadir, 'gdb', 'auto-load', './' + glib_libdir)"
  "gdb_install_dir = false"
  MESON_CONTENTS
  "${MESON_CONTENTS}"
)
string(REPLACE
  "subdir('fuzzing')"
  "if false\n  subdir('fuzzing')\nendif"
  MESON_CONTENTS
  "${MESON_CONTENTS}"
)
file(WRITE "${MESON_FILE}" "${MESON_CONTENTS}")

set(GQUARK_FILE "${GLIB_SOURCE_DIR}/glib/gquark.c")
if(EXISTS "${GQUARK_FILE}")
  file(READ "${GQUARK_FILE}" GQUARK_CONTENTS)
  string(REPLACE
    "  g_assert (quark_seq_id == 0);\n"
    "  if (quark_seq_id != 0) {\n    return;\n  }\n"
    GQUARK_CONTENTS
    "${GQUARK_CONTENTS}"
  )
  file(WRITE "${GQUARK_FILE}" "${GQUARK_CONTENTS}")
endif()

set(GDBUS_UTILS_FILE "${GLIB_SOURCE_DIR}/gio/gdbus-2.0/codegen/utils.py")
if(EXISTS "${GDBUS_UTILS_FILE}")
  file(READ "${GDBUS_UTILS_FILE}" GDBUS_UTILS_CONTENTS)
  string(REPLACE
    "import distutils.version\n"
    "try:\n    from distutils.version import LooseVersion as _LooseVersion\nexcept Exception:\n    from setuptools._distutils.version import LooseVersion as _LooseVersion\n"
    GDBUS_UTILS_CONTENTS
    "${GDBUS_UTILS_CONTENTS}"
  )
  string(REPLACE
    "distutils.version.LooseVersion"
    "_LooseVersion"
    GDBUS_UTILS_CONTENTS
    "${GDBUS_UTILS_CONTENTS}"
  )
  file(WRITE "${GDBUS_UTILS_FILE}" "${GDBUS_UTILS_CONTENTS}")
endif()
