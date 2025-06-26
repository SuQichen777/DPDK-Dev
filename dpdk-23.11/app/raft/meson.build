
sources = files(
  'main.c',
  'election.c',
  'networking.c',
  'timeout.c',
  'rtt_service.c',
  'config.c',
)
includes = include_directories('.', 'include')
deps = [
  dependency('eal'),
  dependency('ethdev'),
  dependency('mbuf'),
  dependency('mempool'),
  dependency('timer'),
  dependency('jansson')
]
executable('dpdk-raft',
  sources,
  include_directories: includes,
  dependencies: deps,
  install: true
)
