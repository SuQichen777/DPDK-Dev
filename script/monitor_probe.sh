#!/usr/bin/env bash
# Low-frequency telemetry collector relying solely on core Linux utilities.

set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
CONFIG_PATH="$REPO_ROOT/dpdk-23.11/app/raft/config.json"
DATA_DIR="$SCRIPT_DIR/data_storage"
INTERVAL=10
CPU_DELAY=1
PING_SAMPLES=5
PING_INTERVAL=0.2
IFACE=""
ONCE=0
WRITE_CSV=1
WRITE_JSONL=1
declare -a PEER_IDS=()
declare -a PEER_IPS=()
declare -a LAT_VALUES=()

usage() {
    cat <<USAGE
Usage: $0 [options]
  --config <path>        Raft config JSON (default: $CONFIG_PATH)
  --data-dir <path>      Output directory (default: $DATA_DIR)
  --interval <seconds>   Seconds between samples (>=1, default: $INTERVAL)
  --cpu-delay <seconds>  Delay between /proc/stat snapshots (default: $CPU_DELAY)
  --ping-samples <n>     ICMP samples per peer (default: $PING_SAMPLES)
  --ping-interval <sec>  Interval between ICMP probes (default: $PING_INTERVAL)
  --iface <name>         NIC interface for throughput stats
  --peer <id:ip>         Add peer manually (can be repeated)
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
        --ping-samples)
            require_arg $1 "${2:-}"
            PING_SAMPLES=$2
            shift 2
            ;;
        --ping-interval)
            require_arg $1 "${2:-}"
            PING_INTERVAL=$2
            shift 2
            ;;
        --iface)
            require_arg $1 "${2:-}"
            IFACE=$2
            shift 2
            ;;
        --peer)
            require_arg $1 "${2:-}"
            entry=$2
            shift 2
            peer_id=${entry%%:*}
            peer_ip=${entry#*:}
            if [[ -n "$peer_id" && -n "$peer_ip" ]]; then
                PEER_IDS+=("$peer_id")
                PEER_IPS+=("$peer_ip")
            fi
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
PING_SAMPLES=${PING_SAMPLES:-5}
PING_INTERVAL=$(awk -v v="$PING_INTERVAL" 'BEGIN{ if (v<=0) v=0.2; printf "%.3f", v }')

mkdir -p "$DATA_DIR"

NODE_ID=""
if [[ -f "$CONFIG_PATH" ]]; then
    NODE_ID=$(grep -m1 '"node_id"' "$CONFIG_PATH" | sed 's/[^0-9-]//g')
fi

load_peers_from_config() {
    local cfg=$1
    [[ -f $cfg ]] || return
    local self
    self=$(grep -m1 '"node_id"' "$cfg" | sed 's/[^0-9-]//g')
    awk -v self="$self" '
        /"ip_map"/ {flag=1; next}
        flag && /}/ {flag=0}
        flag && match($0, /"([^"]+)"\s*:\s*"([^"]+)"/, arr) {
            if (arr[1] != self) {
                printf "%s,%s\n", arr[1], arr[2];
            }
        }
    ' "$cfg"
}

if [[ ${#PEER_IDS[@]} -eq 0 ]]; then
    while IFS=',' read -r pid pip; do
        [[ -z $pid ]] && continue
        PEER_IDS+=("$pid")
        PEER_IPS+=("$pip")
    done < <(load_peers_from_config "$CONFIG_PATH")
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

measure_latency() {
    local target=$1
    local count=$2
    local interval=$3
    local tmp
    tmp=$(mktemp)
    ping -n -c "$count" -i "$interval" -W 1 "$target" >"$tmp" 2>/dev/null || true
    local value
    value=$(awk -F'[ =]' '/time=/{print $(NF-1)}' "$tmp" | sort -n | awk '
        { a[++n]=$1 }
        END {
            if (n==0) { exit 1 }
            idx = int((0.99*n)+0.999); if (idx<1) idx=1; if (idx>n) idx=n;
            printf "%.3f", a[idx];
        }
    ')
    rm -f "$tmp"
    if [[ -n $value ]]; then
        printf "%s" "$value"
    else
        printf "NA"
    fi
}

collect_latencies() {
    LAT_VALUES=()
    if [[ ${#PEER_IDS[@]} -eq 0 ]]; then
        return
    fi
    local idx
    for idx in "${!PEER_IDS[@]}"; do
        LAT_VALUES+=("$(measure_latency "${PEER_IPS[$idx]}" "$PING_SAMPLES" "$PING_INTERVAL")")
    done
}

THR_PREV_TS=""
THR_PREV_RX=""
THR_PREV_TX=""
THR_TX_GBPS=""
THR_RX_GBPS=""

sample_throughput() {
    local iface=$1
    local rx_path="/sys/class/net/$iface/statistics/rx_bytes"
    local tx_path="/sys/class/net/$iface/statistics/tx_bytes"
    [[ -r $rx_path && -r $tx_path ]] || return 1
    local rx tx now
    rx=$(<"$rx_path")
    tx=$(<"$tx_path")
    now=$(date +%s%N)
    if [[ -z ${THR_PREV_TS:-} ]]; then
        THR_PREV_TS=$now
        THR_PREV_RX=$rx
        THR_PREV_TX=$tx
        THR_TX_GBPS=""
        THR_RX_GBPS=""
        return 2
    fi
    local dt
    dt=$(awk -v start="$THR_PREV_TS" -v end="$now" 'BEGIN{printf "%.6f", (end-start)/1e9}')
    local rx_diff=$((rx - THR_PREV_RX))
    local tx_diff=$((tx - THR_PREV_TX))
    (( rx_diff < 0 )) && rx_diff=0
    (( tx_diff < 0 )) && tx_diff=0
    THR_PREV_TS=$now
    THR_PREV_RX=$rx
    THR_PREV_TX=$tx
    if awk -v d="$dt" 'BEGIN{exit !(d<=0)}'; then
        THR_TX_GBPS=""
        THR_RX_GBPS=""
        return 2
    fi
    THR_RX_GBPS=$(awk -v bits=$((rx_diff*8)) -v d="$dt" 'BEGIN{ if (d<=0) print ""; else printf "%.4f", bits/d/1e9 }')
    THR_TX_GBPS=$(awk -v bits=$((tx_diff*8)) -v d="$dt" 'BEGIN{ if (d<=0) print ""; else printf "%.4f", bits/d/1e9 }')
    return 0
}

format_number() {
    local value=$1
    if [[ -z $value || $value == "NA" ]]; then
        printf "null"
    else
        printf "%s" "$value"
    fi
}

build_latency_json() {
    local json="{"
    local idx
    for idx in "${!PEER_IDS[@]}"; do
        if [[ $idx -gt 0 ]]; then
            json+=", "
        fi
        local value=${LAT_VALUES[$idx]:-NA}
        if [[ -z $value || $value == "NA" ]]; then
            json+="\"${PEER_IDS[$idx]}\": null"
        else
            json+="\"${PEER_IDS[$idx]}\": ${value}"
        fi
    done
    json+="}"
    printf '%s' "$json"
}

write_outputs() {
    local ts=$1
    local cpu=$2
    local mem=$3
    local latency_json=$4
    local json_file="$DATA_DIR/telemetry.json"
    local cpu_json mem_json tx_json rx_json node_json
    cpu_json=$(format_number "$cpu")
    mem_json=$(format_number "$mem")
    node_json=${NODE_ID:-}
    [[ -n $node_json ]] || node_json="null"
    if [[ -n $IFACE ]]; then
        tx_json=$(format_number "$THR_TX_GBPS")
        rx_json=$(format_number "$THR_RX_GBPS")
        read -r -d '' json_payload <<EOFJSON
{
  "timestamp": "$ts",
  "node_id": $node_json,
  "cpu_percent": $cpu_json,
  "mem_percent": $mem_json,
  "latency_p99_ms": $latency_json,
  "throughput_gbps": { "tx": $tx_json, "rx": $rx_json }
}
EOFJSON
    else
        read -r -d '' json_payload <<EOFJSON
{
  "timestamp": "$ts",
  "node_id": $node_json,
  "cpu_percent": $cpu_json,
  "mem_percent": $mem_json,
  "latency_p99_ms": $latency_json
}
EOFJSON
    fi
    printf '%s' "$json_payload" > "$json_file"
    if [[ $WRITE_JSONL -eq 1 ]]; then
        printf '%s\n' "$json_payload" >> "$DATA_DIR/telemetry.jsonl"
    fi
    if [[ $WRITE_CSV -eq 1 ]]; then
        local csv_file="$DATA_DIR/telemetry.csv"
        local header="timestamp,cpu_percent,mem_percent"
        local idx
        for idx in "${!PEER_IDS[@]}"; do
            header+=",latency_p99_ms_${PEER_IDS[$idx]}"
        done
        if [[ -n $IFACE ]]; then
            header+=",tx_gbps,rx_gbps"
        fi
        if [[ ! -s $csv_file ]]; then
            printf '%s\n' "$header" > "$csv_file"
        fi
        local row="$ts,$cpu,$mem"
        for idx in "${!PEER_IDS[@]}"; do
            local value=${LAT_VALUES[$idx]:-}
            if [[ -z $value || $value == "NA" ]]; then
                row+="," 
            else
                row+=",$value"
            fi
        done
        if [[ -n $IFACE ]]; then
            row+=",$THR_TX_GBPS,$THR_RX_GBPS"
        fi
        printf '%s\n' "$row" >> "$csv_file"
    fi
}

print_summary() {
    local ts=$1 cpu=$2 mem=$3
    local parts=("CPU ${cpu}%" "MEM ${mem}%")
    local idx
    for idx in "${!PEER_IDS[@]}"; do
        local value=${LAT_VALUES[$idx]:-NA}
        parts+=("p99[${PEER_IDS[$idx]}]=${value}ms")
    done
    if [[ -n $IFACE ]]; then
        if [[ -n $THR_TX_GBPS || -n $THR_RX_GBPS ]]; then
            parts+=("TX ${THR_TX_GBPS:-NA}Gbps" "RX ${THR_RX_GBPS:-NA}Gbps")
        else
            parts+=("throughput warming up")
        fi
    fi
    printf '[%s] %s\n' "$ts" "${parts[*]}"
}

LAT_VALUES=()

while true; do
    cycle_start=$(date +%s%N)
    timestamp=$(date -u +%Y-%m-%dT%H:%M:%SZ)
    cpu_usage=$(read_cpu_percent "$CPU_DELAY")
    mem_usage=$(read_mem_percent)
    collect_latencies
    if [[ -n $IFACE ]]; then
        sample_throughput "$IFACE" || true
    fi
    latency_json=$(build_latency_json)
    write_outputs "$timestamp" "$cpu_usage" "$mem_usage" "$latency_json"
    print_summary "$timestamp" "$cpu_usage" "$mem_usage"
    if [[ $ONCE -eq 1 ]]; then
        break
    fi
    cycle_end=$(date +%s%N)
    elapsed=$(awk -v start="$cycle_start" -v end="$cycle_end" 'BEGIN{printf "%.6f", (end-start)/1e9}')
    sleep_duration=$(awk -v interval="$INTERVAL" -v e="$elapsed" 'BEGIN{d=interval-e; if (d<0) d=0; printf "%.3f", d}')
    sleep "$sleep_duration"
done
