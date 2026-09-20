#!/usr/bin/env bash
#
# natlab.sh - a NAT traversal laboratory built from Linux network namespaces.
#
# Topology:
#
#   p2pl-hostA                p2pl-natA                    p2pl-pub
#   10.1.0.2/24  --vA-host--  10.1.0.1  MASQ  198.51.100.10 --vA-net--+
#                                                                     |
#                                                                  br-pub
#                                                            198.51.100.1
#                                                            198.51.100.2
#                                                                     |
#   p2pl-hostB                p2pl-natB                              |
#   10.2.0.2/24  --vB-host--  10.2.0.1  MASQ  198.51.100.20 --vB-net--+
#
# Both peers sit behind their own NAT. The "public internet" namespace carries
# two addresses so a single socket can ask two different servers where it comes
# from - that is the test that separates a cone NAT from a symmetric one.
#
# Nothing here touches the host network stack: every namespace, veth and
# iptables rule lives under the p2pl- prefix and is removed by "down".

set -euo pipefail

PREFIX="p2pl"
NS_PUB="${PREFIX}-pub"
NS_NATA="${PREFIX}-natA"
NS_HOSTA="${PREFIX}-hostA"
NS_NATB="${PREFIX}-natB"
NS_HOSTB="${PREFIX}-hostB"

PUB_IP1="198.51.100.1"
PUB_IP2="198.51.100.2"
PUB_CIDR="24"
NATA_PUB="198.51.100.10"
NATB_PUB="198.51.100.20"

A_LAN_GW="10.1.0.1"; A_LAN_HOST="10.1.0.2"
B_LAN_GW="10.2.0.1"; B_LAN_HOST="10.2.0.2"

REFLECT_PORT="${REFLECT_PORT:-3478}"
PIDFILE="/run/${PREFIX}-reflector.pid"
LOGFILE="/var/log/${PREFIX}-reflector.log"
STUN_PIDFILE="/run/${PREFIX}-stunserver.pid"
STUN_LOGFILE="/var/log/${PREFIX}-stunserver.log"
CAP_PIDFILE="/run/${PREFIX}-capture.pid"
CAPDIR="${CAPDIR:-$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/captures}"

die() { echo "natlab: $*" >&2; exit 1; }
need_root() { [ "$(id -u)" -eq 0 ] || die "run with sudo"; }

# ---------------------------------------------------------------- lifecycle --

up() {
    local mode_a="${1:-cone}" mode_b="${2:-cone}"
    need_root

    ip netns list | grep -q "^${NS_PUB}" && die "lab already up (run: $0 down)"

    for ns in "$NS_PUB" "$NS_NATA" "$NS_HOSTA" "$NS_NATB" "$NS_HOSTB"; do
        ip netns add "$ns"
        ip -n "$ns" link set lo up
    done

    # public segment: one bridge holding both NAT uplinks
    ip -n "$NS_PUB" link add br-pub type bridge
    ip -n "$NS_PUB" addr add "${PUB_IP1}/${PUB_CIDR}" dev br-pub
    ip -n "$NS_PUB" addr add "${PUB_IP2}/${PUB_CIDR}" dev br-pub
    ip -n "$NS_PUB" link set br-pub up

    side_up A "$NS_NATA" "$NS_HOSTA" "$A_LAN_GW" "$A_LAN_HOST" "$NATA_PUB" "$mode_a"
    side_up B "$NS_NATB" "$NS_HOSTB" "$B_LAN_GW" "$B_LAN_HOST" "$NATB_PUB" "$mode_b"

    echo "lab up   A=${mode_a} (${NATA_PUB})   B=${mode_b} (${NATB_PUB})"
    echo "public servers: ${PUB_IP1} ${PUB_IP2}"
}

# side_up <tag> <nat-ns> <host-ns> <lan-gw> <lan-host> <public-ip> <nat-mode>
side_up() {
    local tag="$1" nat_ns="$2" host_ns="$3" gw="$4" host="$5" pub="$6" mode="$7"
    local lan_net="${gw%.*}.0/24"
    local v_int="v${tag}-int"   v_host="v${tag}-host"
    local v_pub="v${tag}-pub"   v_net="v${tag}-net"

    # private link: NAT router <-> peer
    ip link add "$v_int" type veth peer name "$v_host"
    ip link set "$v_int"  netns "$nat_ns"
    ip link set "$v_host" netns "$host_ns"
    ip -n "$nat_ns"  addr add "${gw}/24"   dev "$v_int"
    ip -n "$host_ns" addr add "${host}/24" dev "$v_host"
    ip -n "$nat_ns"  link set "$v_int"  up
    ip -n "$host_ns" link set "$v_host" up
    ip -n "$host_ns" route add default via "$gw"

    # public link: NAT router <-> internet bridge
    ip link add "$v_pub" type veth peer name "$v_net"
    ip link set "$v_pub" netns "$nat_ns"
    ip link set "$v_net" netns "$NS_PUB"
    ip -n "$nat_ns" addr add "${pub}/${PUB_CIDR}" dev "$v_pub"
    ip -n "$nat_ns" link set "$v_pub" up
    ip -n "$NS_PUB" link set "$v_net" master br-pub
    ip -n "$NS_PUB" link set "$v_net" up

    ip netns exec "$nat_ns" sysctl -qw net.ipv4.ip_forward=1
    apply_nat "$nat_ns" "$v_pub" "$lan_net" "$mode"
}

# apply_nat <nat-ns> <public-iface> <lan-net> <mode>
#
# cone      : Linux keeps the internal source port when it can, so the same
#             internal socket shows up as the same external port no matter who
#             it talks to. Endpoint-independent mapping, address+port restricted
#             filtering - a port-restricted cone NAT.
# symmetric : --random forces a fresh random port for every new destination
#             tuple, so each peer sees a different external port. This is the
#             NAT that defeats plain hole punching and forces a TURN relay.
apply_nat() {
    local ns="$1" iface="$2" lan="$3" mode="$4"
    ip netns exec "$ns" iptables -t nat -F POSTROUTING
    case "$mode" in
        cone)
            ip netns exec "$ns" iptables -t nat -A POSTROUTING \
                -s "$lan" -o "$iface" -j MASQUERADE ;;
        symmetric)
            ip netns exec "$ns" iptables -t nat -A POSTROUTING \
                -s "$lan" -o "$iface" -j MASQUERADE --random ;;
        *) die "unknown NAT mode: $mode (use cone or symmetric)" ;;
    esac
}

down() {
    need_root
    stop_capture || true
    stop_stunserver || true
    stop_reflector || true
    for ns in "$NS_HOSTA" "$NS_HOSTB" "$NS_NATA" "$NS_NATB" "$NS_PUB"; do
        case "$ns" in
            ${PREFIX}-*) ip netns list | grep -q "^${ns}" && ip netns del "$ns" ;;
            *) die "refusing to delete namespace outside ${PREFIX}- prefix: $ns" ;;
        esac
    done
    echo "lab down"
}

# --------------------------------------------------------------- experiment --

# Set how long an idle UDP mapping survives in a NAT. Default Linux is 30s
# unreplied / 120s established; drop it to single digits and a missing
# keepalive becomes a bug you can watch instead of one you read about.
set_timeout() {
    need_root
    local secs="${1:?usage: $0 timeout <seconds>}"
    for ns in "$NS_NATA" "$NS_NATB"; do
        ip netns exec "$ns" sysctl -qw "net.netfilter.nf_conntrack_udp_timeout=${secs}"
        ip netns exec "$ns" sysctl -qw "net.netfilter.nf_conntrack_udp_timeout_stream=${secs}"
    done
    echo "UDP conntrack timeout = ${secs}s on both NATs"
}

# Loss, delay and reordering on a peer's own link.
netem() {
    need_root
    local side="${1:?usage: $0 netem a|b <tc netem args...>}"; shift
    case "$side" in
        a) ip netns exec "$NS_HOSTA" tc qdisc replace dev vA-host root netem "$@" ;;
        b) ip netns exec "$NS_HOSTB" tc qdisc replace dev vB-host root netem "$@" ;;
        *) die "netem side must be a or b" ;;
    esac
    echo "netem on ${side}: $*"
}

# A plain-text stand-in for a STUN server: it answers with the source address
# it observed. Same idea as XOR-MAPPED-ADDRESS, no encoding to debug yet.
#
# One socket bound per public address, never a single 0.0.0.0 socket. A wildcard
# socket picks its reply source address by routing, so an answer to a question
# asked at .2 would leave from .1 - and the NAT drops it, because the conntrack
# entry is keyed on the destination that was asked. RFC 5780 requires a STUN
# server to reply from the address the request arrived on for exactly this
# reason; OTHER-ADDRESS and CHANGE-REQUEST exist to make it explicit.
start_reflector() {
    need_root
    ip netns list | grep -q "^${NS_PUB}" || die "lab is down (run: $0 up)"
    [ -f "$PIDFILE" ] && kill -0 "$(cat "$PIDFILE")" 2>/dev/null && die "reflector already running"
    ip netns exec "$NS_PUB" python3 -u -c '
import socket, select, sys
port = int(sys.argv[1])
socks = []
for ip in sys.argv[2:]:
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.bind((ip, port))
    socks.append(s)
    print("reflector on %s:%d" % (ip, port), flush=True)
while True:
    ready, _, _ = select.select(socks, [], [])
    for s in ready:
        data, addr = s.recvfrom(2048)
        # reply leaves from the same socket, so the source address is the one
        # that was asked
        s.sendto(("SEEN %s:%d" % addr).encode(), addr)
' "$REFLECT_PORT" "$PUB_IP1" "$PUB_IP2" >"$LOGFILE" 2>&1 &
    echo $! > "$PIDFILE"
    sleep 0.3
    kill -0 "$(cat "$PIDFILE")" 2>/dev/null || {
        rm -f "$PIDFILE"
        die "reflector died on startup, see $LOGFILE"
    }
    echo "reflector up on ${PUB_IP1}:${REFLECT_PORT} and ${PUB_IP2}:${REFLECT_PORT}"
}

stop_reflector() {
    [ -f "$PIDFILE" ] || return 0
    kill "$(cat "$PIDFILE")" 2>/dev/null || true
    rm -f "$PIDFILE"
    echo "reflector down"
}

# The core measurement: ONE socket asks TWO servers what it looks like from
# outside. Same external port from both  -> endpoint-independent mapping (cone).
# Different external port from each      -> endpoint-dependent (symmetric).
probe() {
    need_root
    local side="${1:-a}" ns
    case "$side" in
        a) ns="$NS_HOSTA" ;;
        b) ns="$NS_HOSTB" ;;
        *) die "probe side must be a or b" ;;
    esac
    ip netns exec "$ns" python3 -u -c '
import socket, sys
srv1, srv2, port = sys.argv[1], sys.argv[2], int(sys.argv[3])
s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
s.bind(("0.0.0.0", 0))
s.settimeout(2.0)
print("local socket : %s:%d" % s.getsockname())
seen = []
for srv in (srv1, srv2):
    try:
        s.sendto(b"probe", (srv, port))
        reply = s.recv(256).decode().split()[1]
        print("via %-14s -> %s" % (srv, reply))
        seen.append(reply.rsplit(":", 1)[1])
    except socket.timeout:
        print("via %-14s -> TIMEOUT (reflector running?)" % srv)
if len(seen) == 2:
    verdict = "endpoint-independent mapping (cone)" if seen[0] == seen[1] \
              else "endpoint-dependent mapping (SYMMETRIC)"
    print("verdict      : %s" % verdict)
' "$PUB_IP1" "$PUB_IP2" "$REFLECT_PORT"
}

# ------------------------------------------------------------- reference --

# coturn in the public namespace: a production STUN server to measure yours
# against. --stun-only keeps it from offering TURN, -n means no config file,
# and the alt listening IP gives it the second address RFC 5780 tests need.
start_stunserver() {
    need_root
    command -v turnserver >/dev/null || die "turnserver missing (sudo pacman -S coturn)"
    ip netns list | grep -q "^${NS_PUB}" || die "lab is down (run: $0 up)"
    [ -f "$STUN_PIDFILE" ] && kill -0 "$(cat "$STUN_PIDFILE")" 2>/dev/null \
        && die "stun server already running"
    [ -f "$PIDFILE" ] && kill -0 "$(cat "$PIDFILE")" 2>/dev/null \
        && die "python reflector holds port ${REFLECT_PORT} (run: $0 stop-reflector)"

    ip netns exec "$NS_PUB" turnserver \
        --stun-only -n --no-cli --no-tls --no-dtls \
        --listening-ip "$PUB_IP1" --alt-listening-ip "$PUB_IP2" \
        --listening-port "$REFLECT_PORT" \
        --simple-log --log-file stdout -v >"$STUN_LOGFILE" 2>&1 &
    echo $! > "$STUN_PIDFILE"
    sleep 0.5
    kill -0 "$(cat "$STUN_PIDFILE")" 2>/dev/null || {
        rm -f "$STUN_PIDFILE"; die "turnserver died on startup, see $STUN_LOGFILE"
    }
    echo "coturn up on ${PUB_IP1}:${REFLECT_PORT} (alt ${PUB_IP2})"
}

stop_stunserver() {
    [ -f "$STUN_PIDFILE" ] || return 0
    kill "$(cat "$STUN_PIDFILE")" 2>/dev/null || true
    rm -f "$STUN_PIDFILE"
    echo "stun server down"
}

# tcpdump on the public bridge: sees both directions of every STUN exchange
# after NAT, which is exactly the view your code has to reproduce.
start_capture() {
    need_root
    command -v tcpdump >/dev/null || die "tcpdump missing (sudo pacman -S tcpdump)"
    ip netns list | grep -q "^${NS_PUB}" || die "lab is down (run: $0 up)"
    [ -f "$CAP_PIDFILE" ] && kill -0 "$(cat "$CAP_PIDFILE")" 2>/dev/null \
        && die "capture already running"
    mkdir -p "$CAPDIR"
    local out="${1:-${CAPDIR}/$(date +%Y%m%d-%H%M%S).pcap}"
    ip netns exec "$NS_PUB" tcpdump -i br-pub -n -s 0 \
        -w "$out" "udp port ${REFLECT_PORT}" >/dev/null 2>&1 &
    echo $! > "$CAP_PIDFILE"
    echo "$out" > "${CAP_PIDFILE}.path"
    sleep 0.5
    kill -0 "$(cat "$CAP_PIDFILE")" 2>/dev/null || {
        rm -f "$CAP_PIDFILE"; die "tcpdump died on startup"
    }
    echo "capturing to ${out}"
}

stop_capture() {
    [ -f "$CAP_PIDFILE" ] || return 0
    kill "$(cat "$CAP_PIDFILE")" 2>/dev/null || true
    sleep 0.3
    local out=""
    [ -f "${CAP_PIDFILE}.path" ] && out="$(cat "${CAP_PIDFILE}.path")"
    rm -f "$CAP_PIDFILE" "${CAP_PIDFILE}.path"
    [ -n "$out" ] && { chmod a+r "$out" 2>/dev/null || true; echo "capture saved: ${out}"; }
    return 0
}

# coturn's own client, run from behind NAT A. It speaks correct STUN, so the
# bytes it puts on the wire are the answer key for your encoder.
refclient() {
    need_root
    command -v turnutils_stunclient >/dev/null \
        || die "turnutils_stunclient missing (sudo pacman -S coturn)"
    local side="${1:-a}" ns
    case "$side" in
        a) ns="$NS_HOSTA" ;;
        b) ns="$NS_HOSTB" ;;
        *) die "refclient side must be a or b" ;;
    esac
    ip netns exec "$ns" turnutils_stunclient -p "$REFLECT_PORT" "$PUB_IP1"
}

status() {
    need_root
    ip netns list | grep "^${PREFIX}-" || { echo "lab is down"; return; }
    echo
    for ns in "$NS_NATA" "$NS_NATB"; do
        echo "--- ${ns} ---"
        ip netns exec "$ns" sysctl -n net.netfilter.nf_conntrack_udp_timeout \
            | sed 's/^/  udp timeout: /'
        if command -v conntrack >/dev/null; then
            ip netns exec "$ns" conntrack -L -p udp 2>/dev/null | sed 's/^/  /' || true
        else
            echo "  (install conntrack-tools to list live mappings)"
        fi
    done
}

# Run any command inside a namespace: natlab.sh a ./build/p2p_ping ...
in_ns() {
    need_root
    local side="$1"; shift
    case "$side" in
        a)   ip netns exec "$NS_HOSTA" "$@" ;;
        b)   ip netns exec "$NS_HOSTB" "$@" ;;
        net) ip netns exec "$NS_PUB"   "$@" ;;
        nata) ip netns exec "$NS_NATA" "$@" ;;
        natb) ip netns exec "$NS_NATB" "$@" ;;
        *) die "unknown namespace: $side" ;;
    esac
}

usage() {
    cat <<USAGE
natlab.sh - NAT traversal lab

  up [cone|symmetric] [cone|symmetric]   build the lab (NAT A, NAT B; default cone cone)
  down                                   tear everything down
  status                                 namespaces, UDP timeouts, live mappings
  reflector | stop-reflector             the public "where do I come from" server
  stunserver | stop-stunserver           coturn on the public side (real STUN)
  capture [file] | stop-capture          tcpdump the public bridge to a pcap
  refclient [a|b]                        coturn's own client, from behind a NAT
  probe [a|b]                            one socket, two servers -> NAT mapping verdict
  timeout <seconds>                      idle UDP mapping lifetime on both NATs
  netem a|b <tc netem args>              loss/delay/reorder on a peer link
  a|b|net|nata|natb <command...>         run a command inside a namespace

Capture a reference exchange:
  sudo ./natlab.sh up cone cone
  sudo ./natlab.sh stunserver
  sudo ./natlab.sh capture
  sudo ./natlab.sh refclient a
  sudo ./natlab.sh stop-capture
  ./carve.py captures/<file>.pcap ../stun/tests/corpus
USAGE
}

case "${1:-}" in
    up)              shift; up "${1:-cone}" "${2:-cone}" ;;
    down)            down ;;
    status)          status ;;
    reflector)       start_reflector ;;
    stop-reflector)  stop_reflector ;;
    stunserver)      start_stunserver ;;
    stop-stunserver) stop_stunserver ;;
    capture)         shift; start_capture "${1:-}" ;;
    stop-capture)    stop_capture ;;
    refclient)       shift; refclient "${1:-a}" ;;
    probe)           shift; probe "${1:-a}" ;;
    timeout)         shift; set_timeout "${1:-}" ;;
    netem)           shift; netem "$@" ;;
    a|b|net|nata|natb) in_ns "$@" ;;
    ""|-h|--help|help) usage ;;
    *) die "unknown command: $1 (try --help)" ;;
esac
