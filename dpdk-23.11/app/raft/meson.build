# SPDX-License-Identifier: BSD-3-Clause
# Copyright(c) 2025

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

deps = [
    dependency('libdpdk'),
    dependency('threads'),
]

# Compiler flags
cflags = [
    '-Wno-deprecated-declarations',
    '-DALLOW_EXPERIMENTAL_API',
]

# JSON support
if dpdk_conf.has('RTE_HAS_JANSSON')
    jansson_dep = dependency('jansson')
    deps += jansson_dep
    cflags += '-DUSE_JANSSON'
endif

extra_deps = [
    'ethdev',
    'mempool',
    'net',
    'mbuf',
    'timer',
]

foreach dep : extra_deps
    deps += dependency(dep, required: true)
endforeach

# Build executable
executable(
    name,
    sources,
    c_args: cflags,
    include_directories: includes,
    dependencies: deps,
    install: true,
    install_dir: 'bin'
)