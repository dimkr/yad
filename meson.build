project('yad', 'c', version: '0.42.81', license: 'GPLv3')

cc = meson.get_compiler('c')
m = cc.find_library('m')

gtk = dependency('gtk+-3.0')
gtk_print = dependency('gtk+-unix-print-3.0')
gtk_layer_shell = dependency('gtk-layer-shell-0', required: false)

subdir('po')
subdir('src')
subdir('data')
