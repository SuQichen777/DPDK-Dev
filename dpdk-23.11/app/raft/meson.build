name = 'raft'

deps = [
    get_variable('ethdev_dep'),
    get_variable('mbuf_dep'),
    get_variable('net_dep'),
    get_variable('memzone_dep'),
    get_variable('timer_dep'),
    get_variable('cmdline_dep'),
    get_variable('kvargs_dep'),
    get_variable('hash_dep'),
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