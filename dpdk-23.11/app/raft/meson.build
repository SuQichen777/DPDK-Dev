
sources = files(
  'main.c',
  'election.c',
  'networking.c',
  'timeout.c',
  'rtt_service.c',
  'config.c',
)
includes = include_directories('.', 'include')
jansson_dep = dependency('jansson', required: true)
deps = [
  'eal', 'ethdev', 'mbuf', 'mempool', 'timer',
  jansson_dep
]
executable('dpdk-raft',
  sources,
  include_directories: includes,
  dependencies: deps,
  install: true
)
