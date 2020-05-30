project(
    'libhelix',
    'c', 'cpp',
    default_options : ['cpp_std=c++17', 'c_std=c11', 'buildtype=debugoptimized'],
    license : 'MIT',
    version : '0.0.5'
)

srcs = [
    'src/Helix.cpp',
    'src/util.cpp'
]

incdir = include_directories('include')

lua_dep = dependency('lua')

libmlactions_proj = subproject('libmlactions')
libmlactions_dep = libmlactions_proj.get_variable('mlactions_dep')

libalphafile_proj = subproject('libalphafile')
libalphafile_dep = libalphafile_proj.get_variable('libalphafile_dep')
deps = [lua_dep, libmlactions_dep, libalphafile_dep]

libhelix = shared_library('helix',
    srcs,
    include_directories : incdir,
    dependencies : deps,
    install : true
)
libhelix_dep = declare_dependency(
    include_directories : incdir,
    link_with : libhelix,
    dependencies : deps
)

#executable('helix', sources : srcs, include_directories : incdir, dependencies : deps)
