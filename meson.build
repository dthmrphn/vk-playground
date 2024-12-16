project('vk-playground', 'cpp',
  version : '0.1',
  default_options : ['cpp_std=c++17'])

deps = [
  dependency('glfw3'),
  dependency('glm'),
  dependency('fmt'),
  dependency('vulkan'),
  dependency('dl'),
  dependency('threads'),
]

subdir('common')
subdir('shaders')
subdir('examples')
