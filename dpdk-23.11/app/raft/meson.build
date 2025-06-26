# SPDX-License-Identifier: BSD-3-Clause

sources = files(
        'main.c',
        'election.c',
        'networking.c',
        'timeout.c',
        'config.c',
)
jansson_dep = dependency('jansson', required: true)
includes = include_directories('.', 'include')
deps += ['timer', 'ethdev', 'mbuf', 'mempool', 'eal']
deps += jansson_dep