if is_windows
    build = false
    reason = 'not supported on Windows'
    subdir_done()
endif

includes = include_directories('include')

sources = files(
        'config.c',
        'election.c',
        'main.c',
        'networking.c',
        'timeout.c',
        'metadata.c',
        'latency.c',
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
