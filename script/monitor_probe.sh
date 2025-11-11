#!/usr/bin/env bash
# Low-frequency telemetry collector relying solely on core Linux utilities.

set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
CONFIG_PATH="$REPO_ROOT/dpdk-23.11/app/raft/config.json"
DATA_DIR="$SCRIPT_DIR/data_storage"
INTERVAL=10
CPU_DELAY=1
ONCE=0
WRITE_CSV=1
WRITE_JSONL=1

usage() {
    cat <<USAGE
Usage: $0 [options]
  --config <path>        Raft config JSON (default: $CONFIG_PATH)
  --data-dir <path>      Output directory (default: $DATA_DIR)
  --interval <seconds>   Seconds between samples (>=1, default: $INTERVAL)
  --cpu-delay <seconds>  Delay between /proc/stat snapshots (default: $CPU_DELAY)
  --once                 Run a single collection and exit
  --no-csv               Skip telemetry.csv output
  --no-jsonl             Skip telemetry.jsonl output
  -h, --help             Show this message
USAGE
}

require_arg() {
    local flag=$1
    local value=$2
    if [[ -z ${value:-} ]]; then
        echo "Error: $flag requires a value" >&2
        exit 1
    fi
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --config)
            require_arg $1 "${2:-}"
            CONFIG_PATH=$2
            shift 2
            ;;
        --data-dir)
            require_arg $1 "${2:-}"
            DATA_DIR=$2
            shift 2
            ;;
        --interval)
            require_arg $1 "${2:-}"
            INTERVAL=$2
            shift 2
            ;;
        --cpu-delay)
            require_arg $1 "${2:-}"
            CPU_DELAY=$2
            shift 2
            ;;
        --once)
            ONCE=1
            shift
            ;;
        --no-csv)
            WRITE_CSV=0
            shift
            ;;
        --no-jsonl)
            WRITE_JSONL=0
            shift
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            echo "Unknown option: $1" >&2
            usage >&2
            exit 1
            ;;
    esac
done

INTERVAL=$(awk -v v="$INTERVAL" 'BEGIN{ if (v<1) v=1; printf "%.3f", v }')
CPU_DELAY=$(awk -v v="$CPU_DELAY" 'BEGIN{ if (v<0.05) v=0.05; printf "%.3f", v }')

mkdir -p "$DATA_DIR"

NODE_ID=""
if [[ -f "$CONFIG_PATH" ]]; then
    NODE_ID=$(grep -m1 '"node_id"' "$CONFIG_PATH" | sed 's/[^0-9-]//g')
fi

# Metrics helpers
read_cpu_percent() {
    local delay=$1
    read _ u1 n1 s1 i1 w1 q1 t1 st1 rest < /proc/stat
    local idle1=$((i1 + w1))
    local total1=$((u1 + n1 + s1 + i1 + w1 + q1 + t1 + st1))
    sleep "$delay"
    read _ u2 n2 s2 i2 w2 q2 t2 st2 rest < /proc/stat
    local idle2=$((i2 + w2))
    local total2=$((u2 + n2 + s2 + i2 + w2 + q2 + t2 + st2))
    local didle=$((idle2 - idle1))
    local dtotal=$((total2 - total1))
    if [[ $dtotal -le 0 ]]; then
        printf "0.00"
        return
    fi
    awk -v idle="$didle" -v total="$dtotal" 'BEGIN { printf "%.2f", 100*(1-(idle/total)) }'
}

read_mem_percent() {
    awk '
        /MemTotal:/ {total=$2}
        /MemAvailable:/ {avail=$2}
        END {
            if (total>0 && avail>0) printf "%.2f", 100*(total-avail)/total;
            else print "0.00";
        }
    ' /proc/meminfo
}

format_number() {
    local value=$1
    if [[ -z $value || $value == "NA" ]]; then
        printf "null"
    else
        printf "%s" "$value"
    fi
}

write_outputs() {
    local ts=$1
    local cpu=$2
    local mem=$3
    local json_file="$DATA_DIR/telemetry.json"
    local cpu_json mem_json node_json
    cpu_json=$(format_number "$cpu")
    mem_json=$(format_number "$mem")
    node_json=${NODE_ID:-}
    [[ -n $node_json ]] || node_json="null"
    read -r -d '' json_payload <<EOFJSON
{
  "timestamp": "$ts",
  "node_id": $node_json,
  "cpu_percent": $cpu_json,
  "mem_percent": $mem_json
}
EOFJSON
    printf '%s' "$json_payload" > "$json_file"
    if [[ $WRITE_JSONL -eq 1 ]]; then
        printf '%s\n' "$json_payload" >> "$DATA_DIR/telemetry.jsonl"
    fi
    if [[ $WRITE_CSV -eq 1 ]]; then
        local csv_file="$DATA_DIR/telemetry.csv"
        local header="timestamp,cpu_percent,mem_percent"
        if [[ ! -s $csv_file ]]; then
            printf '%s\n' "$header" > "$csv_file"
        fi
        local row="$ts,$cpu,$mem"
        printf '%s\n' "$row" >> "$csv_file"
    fi
}

print_summary() {
    local ts=$1 cpu=$2 mem=$3
    local parts=("CPU ${cpu}%" "MEM ${mem}%")
    printf '[%s] %s\n' "$ts" "${parts[*]}"
}

while true; do
    cycle_start=$(date +%s%N)
    timestamp=$(date -u +%Y-%m-%dT%H:%M:%SZ)
    cpu_usage=$(read_cpu_percent "$CPU_DELAY")
    mem_usage=$(read_mem_percent)
    write_outputs "$timestamp" "$cpu_usage" "$mem_usage"
    print_summary "$timestamp" "$cpu_usage" "$mem_usage"
    if [[ $ONCE -eq 1 ]]; then
        break
    fi
    cycle_end=$(date +%s%N)
    elapsed=$(awk -v start="$cycle_start" -v end="$cycle_end" 'BEGIN{printf "%.6f", (end-start)/1e9}')
    sleep_duration=$(awk -v interval="$INTERVAL" -v e="$elapsed" 'BEGIN{d=interval-e; if (d<0) d=0; printf "%.3f", d}')
    sleep "$sleep_duration"
done
