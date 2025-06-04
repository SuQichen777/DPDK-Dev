#!/bin/bash

# 自动创建日志目录
mkdir -p ~/warp_logs

# 以日期命名日志文件
LOG_FILE=~/warp_logs/session-$(date +%F-%H%M%S).log

# 启动 tmux 会话并用 script 记录所有输出
tmux new-session -s warplog "script -q -c bash $LOG_FILE"
