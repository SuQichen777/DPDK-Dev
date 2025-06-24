name = 'raft'

dpdk_libs = [
    'ethdev',
    'net',
    'mbuf',
    'memzone',
    'timer',
    'cmdline',
    'kvargs',
    'hash'
]

deps = []
foreach lib : dpdk_libs
    dep_name = 'libdpdk_' + lib
    deps += dependency(dep_name)
endforeach

sources = files(
    'main.c',
    'election.c',
    'timeout.c',
    'rtt_service.c',
)

includes = include_directories('include')

executable('dpdk-raft',
    sources,
    include_directories: includes,
    dependencies: deps,
    c_args: [
        '-DALLOW_EXPERIMENTAL_API',
        '-DRAFT_PORT=9999',      
        '-DNUM_NODES=3'          
    ],
    install: true
)