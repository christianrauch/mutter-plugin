project('plugin', 'c')

glib_req = '>= 2.61.1'
gtk3_req = '>= 3.19.8'
json_glib_req = '>= 0.12.0'
gsettings_desktop_schemas_req = '>= 3.33.0'

glib_dep = dependency('glib-2.0', version: glib_req)
gtk3_dep = dependency('gtk+-3.0', version: gtk3_req)
json_glib_dep = dependency('json-glib-1.0', version: json_glib_req)
gsettings_desktop_schemas_dep = dependency('gsettings-desktop-schemas', version: gsettings_desktop_schemas_req)
libmutter_clutter_dep = dependency('mutter-clutter-6')

mutter_dep = dependency('libmutter-6')

default_plugin_c_args = [
  '-fPIC',
  '-DG_LOG_DOMAIN="mutter"',
  '-DGETTEXT_PACKAGE="@0@"'.format(meson.project_name()),
]

default_plugin = shared_module('default',
  sources: ['default.c'],
  c_args: default_plugin_c_args,
  dependencies: [
    glib_dep,
    gtk3_dep,
    json_glib_dep,
    gsettings_desktop_schemas_dep,
    libmutter_clutter_dep,
    mutter_dep
  ],
)
