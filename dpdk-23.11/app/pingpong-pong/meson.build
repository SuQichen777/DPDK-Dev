if is_windows
    build = false
    reason = 'not supported on Windows'
    subdir_done()
endif

includes = include_directories('include')

sources = files(
        'main.c',
)

deps += [
        'eal',
        'ethdev',
        'mempool',
        'mbuf',
        'net',
        'timer',
]

ext_deps += dependency('jansson') 
