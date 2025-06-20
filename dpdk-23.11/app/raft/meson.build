name = 'raft'

deps += ['eal', 'ethdev', 'cmdline', 'net', 'kvargs']
sources = files(
    'main.c',
    'election.c',
    'timeout.c',
    'rtt_service.c',
)

includes = include_directories('include')

link_libs = []
foreach d:deps
    var_name = get_option('default_library') + '_rte_' + d
    if is_variable(var_name)
        link_libs += get_variable(var_name)
    else
        error('Missing DPDK dependency: ' + var_name)
    endif
endforeach

executable('dpdk-raft',
    sources,
    include_directories: includes,
    link_whole: link_libs,
    c_args: ['-DALLOW_EXPERIMENTAL_API'],
    install: true
)
