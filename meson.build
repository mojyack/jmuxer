project('jmuxer', 'cpp', version : '0.0', default_options : ['warning_level=3', 'cpp_std=c++20'])
add_global_arguments('-Wfatal-errors', language: 'cpp')

gstreamer_dep = dependency('gstreamer-1.0', version : ['>=1.20'])

src = files(
  'src/main.cpp',
  'src/pad-name-parser.cpp',
  'src/audio-mixer.cpp',
  'src/gstutil/pipeline-helper.cpp',
  'src/compositor-layouter/compositor-layouter.cpp',
)

executable('jmuxer', src,
        dependencies: [gstreamer_dep]
)
