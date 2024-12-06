# Define the project
project('face_detection', 'cpp',
  version: '0.1',
  default_options: [
    'cpp_std=c++17',
    'warning_level=2',
    'buildtype=release',          # Set the build type to release
    'optimization=3',             # Set optimization level to 3 (highest)
    'b_staticpic=true',           # Build with position-independent code for static linking
    'static=True',                # Build everything statically
  ])

# Find OpenCV package with static linking
opencv = dependency('opencv4', required: true, static: true)

# Define the executable and its source files
src_files = files('src/main.cpp')

# Define the executable and link it with OpenCV statically
executable('face_detection', src_files, dependencies: opencv)
