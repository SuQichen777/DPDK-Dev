# Application name
name = 'raft_node'

# Source files
sources = files(
    'config.c',
    'election.c',
    'main.c',
    'networking.c',
    'timeout.c'
)

# Include directories
includes = include_directories('include')

# Dependencies
deps = [
    'eal',
    'ethdev',
    'mempool',
    'net',
    'mbuf',
    'timer',
]

# Compiler flags
cflags = [
    '-Wno-deprecated-declarations',
    '-DALLOW_EXPERIMENTAL_API'  # For DPDK 23.11 APIs
]

# JSON support
if dpdk_conf.has('RTE_HAS_JANSSON')
    ext_deps += jansson_dep
    cflags += '-DUSE_JANSSON'
endif

# Build executable
executable(
    name,
    sources,
    c_args: cflags,
    include_directories: includes,
    dependencies: [dpdk] + ext_deps,
    install: true,
    install_dir: 'bin'
)