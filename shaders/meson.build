spirv_comp = find_program('glslangValidator')

spirv_gen = generator(spirv_comp,
                output  : '@PLAINNAME@.spv.hpp',
                arguments : ['@INPUT@', '-V', '-x', '-o', '@OUTPUT@'])

triangle_shaders = spirv_gen.process('triangle.vert', 'triangle.frag')
