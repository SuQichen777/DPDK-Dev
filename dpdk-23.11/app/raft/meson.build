name = 'raft'

default_lib = get_option('default_library')

deps = [
    get_variable(default_lib + '_rte_ethdev'),
    get_variable(default_lib + '_rte_mbuf'),
    get_variable(default_lib + '_rte_net'),
    get_variable(default_lib + '_rte_memzone'),
    get_variable(default_lib + '_rte_timer'),
    get_variable(default_lib + '_rte_cmdline'),
    get_variable(default_lib + '_rte_kvargs'),
    get_variable(default_lib + '_rte_hash'),
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