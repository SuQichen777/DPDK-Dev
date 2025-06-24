name = 'raft'

deps = [
    dependency('ethdev'),
    dependency('net'),
    dependency('mbuf'),
    dependency('memzone'),
    dependency('timer'),
    dependency('cmdline'),
    dependency('kvargs'),
    dependency('hash'),
]

sources = files(
    'main.c',
    'election.c',
    'timeout.c',
    'rtt_service.c',
    'networking.c'
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