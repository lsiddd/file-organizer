# meson.build
project('file_organizer', 'cpp', version : '0.1', default_options : ['cpp_std=c++17'])

# Request static linking
static_library_options = ['-static']

cc = meson.get_compiler('cpp')

exe = executable(
  'file_organizer',
  'src/file_organizer.cpp',
  dependencies: [],
  cpp_args: static_library_options,
  link_args: static_library_options,
  install: true
)

# Optionally, if you want to create a target for tests or additional files
# test('file_organizer_test', exe)

