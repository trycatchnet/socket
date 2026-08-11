#!/usr/bin/env bash
# Written to test the p2p_chat program end-to-end in a single run on a
# single machine (via loopback / 127.0.0.1).
#
# Two modes are available:
#
#   --auto   (default) Fully automatic, no human intervention required
#            "smoke test". It brings up two instances on loopback,
#            verifies that hole-punch succeeds, chat messages actually
#            reach the other side, background ping/pong continues to work
#            during chat, and that /quit shuts down properly (sending
#            and receiving BYE). At the end, it prints a PASS/FAIL report
#            and returns exit code 0 (all passed) / 1 (at least one check failed).
#            Ideal for CI or "quick validation after every change".
#
#   --live   Real, interactive, manual chat test. Requires tmux. It splits
#            the screen into two panes: left pane runs peer A, right pane
#            runs peer B; both connect to each other via 127.0.0.1 with
#            --no-stun. You can type into either pane as in a normal terminal
#            and test the real-time chat with your own eyes. To exit, type
#            /quit in both windows, or press CTRL+C, or press CTRL+B then D
#            to detach from the session (it keeps running in the background;
#            reattach with "tmux attach -t p2p_chat_test").
#
# Usage:
#   ./test_p2p_chat.sh                  # automatic test (default)
#   ./test_p2p_chat.sh --auto
#   ./test_p2p_chat.sh --live
#   ./test_p2p_chat.sh --ports 9101 9102
#   ./test_p2p_chat.sh --auto --verbose
#   ./test_p2p_chat.sh --auto --keep-logs

set -uo pipefail

# ---------------------------------------------------------------------------
# Configurable defaults
# ---------------------------------------------------------------------------
MODE="auto"
PORT_A=9101
PORT_B=9102
VERBOSE=0
KEEP_LOGS=0
SKIP_BUILD=0
SESSION_NAME="p2p_chat_test"
HOLE_PUNCH_TIMEOUT=10   # seconds - normally <1 sec on loopback
CHAT_TIMEOUT=5          # seconds - wait time for a message to reach the other side
PING_ROUNDS_WAIT=3      # seconds - wait for at least 2 background ping rounds

# ---------------------------------------------------------------------------
# Colors (only if writing to a real terminal)
# ---------------------------------------------------------------------------
if [ -t 1 ]; then
    C_RED=$'\033[31m'; C_GREEN=$'\033[32m'; C_YELLOW=$'\033[33m'
    C_BLUE=$'\033[34m'; C_BOLD=$'\033[1m'; C_RESET=$'\033[0m'
else
    C_RED=""; C_GREEN=""; C_YELLOW=""; C_BLUE=""; C_BOLD=""; C_RESET=""
fi

info()  { printf '%s[i]%s %s\n' "$C_BLUE" "$C_RESET" "$*"; }
ok()    { printf '%s[OK]%s %s\n' "$C_GREEN" "$C_RESET" "$*"; }
warn()  { printf '%s[!]%s %s\n' "$C_YELLOW" "$C_RESET" "$*"; }
fail()  { printf '%s[FAIL]%s %s\n' "$C_RED" "$C_RESET" "$*"; }

usage() {
    sed -n '2,33p' "$0" | sed 's/^# \{0,1\}//'
}

# ---------------------------------------------------------------------------
# Parse arguments
# ---------------------------------------------------------------------------
while [ $# -gt 0 ]; do
    case "$1" in
        --auto) MODE="auto"; shift ;;
        --live|--tmux) MODE="live"; shift ;;
        --ports) PORT_A="$2"; PORT_B="$3"; shift 3 ;;
        --verbose) VERBOSE=1; shift ;;
        --keep-logs) KEEP_LOGS=1; shift ;;
        --skip-build) SKIP_BUILD=1; shift ;;
        -h|--help) usage; exit 0 ;;
        *) fail "Unknown parameter: $1"; usage; exit 1 ;;
    esac
done

# ---------------------------------------------------------------------------
# Find project root and check dependencies
# ---------------------------------------------------------------------------
SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" >/dev/null 2>&1 && pwd)"
PROJECT_ROOT="$SCRIPT_DIR"

if [ ! -f "$PROJECT_ROOT/Makefile" ]; then
    fail "Makefile not found: $PROJECT_ROOT"
    fail "Place this script in the project root (same folder as Makefile) and run it from there."
    exit 1
fi

for tool in gcc make grep awk; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        fail "'$tool' not found. Install it first: sudo apt-get update && sudo apt-get install -y build-essential"
        exit 1
    fi
done

BIN="$PROJECT_ROOT/build/p2p_chat"

# ---------------------------------------------------------------------------
# Build
# ---------------------------------------------------------------------------
build_project() {
    if [ "$SKIP_BUILD" -eq 1 ]; then
        if [ ! -x "$BIN" ]; then
            fail "--skip-build given but $BIN does not exist. Run without --skip-build first."
            exit 1
        fi
        return 0
    fi

    info "Compiling project (make)..."
    local build_log
    build_log="$(mktemp)"
    if ! ( cd "$PROJECT_ROOT" && make ) >"$build_log" 2>&1; then
        fail "Build failed:"
        cat "$build_log"
        rm -f "$build_log"
        exit 1
    fi
    if [ "$VERBOSE" -eq 1 ]; then
        cat "$build_log"
    fi
    rm -f "$build_log"

    if [ ! -x "$BIN" ]; then
        fail "Build completed but binary not found: $BIN"
        exit 1
    fi
    ok "Build complete: $BIN"
}

# ---------------------------------------------------------------------------
# Common cleanup (runs on every exit, including CTRL+C)
# ---------------------------------------------------------------------------
PIDS=()
TMP_DIR=""
FD_A_OPENED=0
FD_B_OPENED=0

cleanup() {
    local ec=$?
    trap - EXIT INT TERM

    for pid in "${PIDS[@]:-}"; do
        [ -n "$pid" ] || continue
        if kill -0 "$pid" 2>/dev/null; then
            kill "$pid" 2>/dev/null || true
        fi
    done
    # Give a short while for stubborn processes, then SIGKILL
    sleep 0.2
    for pid in "${PIDS[@]:-}"; do
        [ -n "$pid" ] || continue
        if kill -0 "$pid" 2>/dev/null; then
            kill -9 "$pid" 2>/dev/null || true
        fi
    done

    if [ "$FD_A_OPENED" -eq 1 ]; then exec 3>&- 2>/dev/null || true; fi
    if [ "$FD_B_OPENED" -eq 1 ]; then exec 4>&- 2>/dev/null || true; fi

    if [ -n "$TMP_DIR" ] && [ -d "$TMP_DIR" ]; then
        if [ "$KEEP_LOGS" -eq 1 ]; then
            warn "Logs not deleted: $TMP_DIR"
        else
            rm -rf "$TMP_DIR"
        fi
    fi

    exit "$ec"
}
trap cleanup EXIT INT TERM

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

# wait_for_pattern <file> <search_text> <timeout_seconds>
# Searches for plain text (not regex) in the file, checking every 0.1 sec
# until timeout. Returns 0 if found, 1 otherwise.
wait_for_pattern() {
    local file="$1" pattern="$2" timeout_sec="$3"
    local max_iter=$(( timeout_sec * 10 ))
    local i=0
    while [ "$i" -lt "$max_iter" ]; do
        if [ -f "$file" ] && grep -qF -- "$pattern" "$file" 2>/dev/null; then
            return 0
        fi
        sleep 0.1
        i=$(( i + 1 ))
    done
    grep -qF -- "$pattern" "$file" 2>/dev/null
}

# count_pattern <file> <search_text>
count_pattern() {
    local file="$1" pattern="$2"
    grep -cF -- "$pattern" "$file" 2>/dev/null || true
}

CHECKS_TOTAL=0
CHECKS_PASSED=0
declare -a CHECK_NAMES=()
declare -a CHECK_RESULTS=()

# record_check <name> <0=success|1=failure>
record_check() {
    local name="$1" result="$2"
    CHECKS_TOTAL=$(( CHECKS_TOTAL + 1 ))
    CHECK_NAMES+=("$name")
    if [ "$result" -eq 0 ]; then
        CHECKS_PASSED=$(( CHECKS_PASSED + 1 ))
        CHECK_RESULTS+=("PASS")
        ok "$name"
    else
        CHECK_RESULTS+=("FAIL")
        fail "$name"
    fi
}

dump_logs() {
    echo
    printf '%s----- LOG A (port %s) -----%s\n' "$C_BOLD" "$PORT_A" "$C_RESET"
    cat "$LOG_A" 2>/dev/null
    printf '%s----- LOG B (port %s) -----%s\n' "$C_BOLD" "$PORT_B" "$C_RESET"
    cat "$LOG_B" 2>/dev/null
    echo
}

# ---------------------------------------------------------------------------
# --auto : automatic end-to-end test
# ---------------------------------------------------------------------------
run_auto_test() {
    build_project

    TMP_DIR="$(mktemp -d)"
    LOG_A="$TMP_DIR/log_a.txt"
    LOG_B="$TMP_DIR/log_b.txt"
    FIFO_A="$TMP_DIR/stdin_a.fifo"
    FIFO_B="$TMP_DIR/stdin_b.fifo"

    mkfifo "$FIFO_A" "$FIFO_B"

    # Open FIFOs in read-write mode to avoid deadlock before both sides are ready.
    # This allows us to write more "lines" at will, just like a real keyboard.
    exec 3<>"$FIFO_A"; FD_A_OPENED=1
    exec 4<>"$FIFO_B"; FD_B_OPENED=1

    info "Starting Peer A (local port $PORT_A -> peer 127.0.0.1:$PORT_B)..."
    "$BIN" "$PORT_A" --no-stun 127.0.0.1 "$PORT_B" <&3 >"$LOG_A" 2>&1 &
    PID_A=$!
    PIDS+=("$PID_A")

    info "Starting Peer B (local port $PORT_B -> peer 127.0.0.1:$PORT_A)..."
    "$BIN" "$PORT_B" --no-stun 127.0.0.1 "$PORT_A" <&4 >"$LOG_B" 2>&1 &
    PID_B=$!
    PIDS+=("$PID_B")

    echo
    info "Waiting for hole-punch to complete (max ${HOLE_PUNCH_TIMEOUT}s)..."
    if wait_for_pattern "$LOG_A" "opened hole" "$HOLE_PUNCH_TIMEOUT" \
        && wait_for_pattern "$LOG_B" "opened hole" "$HOLE_PUNCH_TIMEOUT"; then
        record_check "Hole-punch (both peers see each other)" 0
    else
        record_check "Hole-punch (both peers see each other)" 1
        fail "Hole-punch failed, skipping remaining tests."
        dump_logs
        return
    fi

    # ---- A -> B chat message ----
    MSG_A_TO_B="Hello B, this is A - $(date +%s)"
    printf '%s\n' "$MSG_A_TO_B" >&3
    if wait_for_pattern "$LOG_B" "[Peer] $MSG_A_TO_B" "$CHAT_TIMEOUT"; then
        record_check "Chat A -> B delivered" 0
    else
        record_check "Chat A -> B delivered" 1
    fi

    # ---- B -> A chat message ----
    MSG_B_TO_A="Hi A, your message arrived - $(date +%s)"
    printf '%s\n' "$MSG_B_TO_A" >&4
    if wait_for_pattern "$LOG_A" "[Peer] $MSG_B_TO_A" "$CHAT_TIMEOUT"; then
        record_check "Chat B -> A delivered" 0
    else
        record_check "Chat B -> A delivered" 1
    fi

    # ---- Is background ping/pong still working during chat? ----
    info "Verifying background ping/pong continues during chat (waiting ~${PING_ROUNDS_WAIT}s)..."
    sleep "$PING_ROUNDS_WAIT"
    PONG_COUNT_A="$(count_pattern "$LOG_A" '[PONG<-]')"
    PONG_COUNT_B="$(count_pattern "$LOG_B" '[PONG<-]')"
    if [ "${PONG_COUNT_A:-0}" -ge 2 ] && [ "${PONG_COUNT_B:-0}" -ge 2 ]; then
        record_check "Ping/pong continues in background during chat (A:$PONG_COUNT_A B:$PONG_COUNT_B pongs)" 0
    else
        record_check "Ping/pong continues in background during chat (A:$PONG_COUNT_A B:$PONG_COUNT_B pongs)" 1
    fi

    # ---- Proper shutdown with /quit ----
    info "Testing proper shutdown with /quit..."
    printf '/quit\n' >&3
    if wait_for_pattern "$LOG_A" "[INFO] Exiting chat." "$CHAT_TIMEOUT"; then
        record_check "A processed /quit command" 0
    else
        record_check "A processed /quit command" 1
    fi
    if wait_for_pattern "$LOG_B" "Other peer closed connection. (Got BYE)" "$CHAT_TIMEOUT"; then
        record_check "B received A's BYE packet" 0
    else
        record_check "B received A's BYE packet" 1
    fi

    # Also quit B so it cleans up properly
    if kill -0 "$PID_B" 2>/dev/null; then
        printf '/quit\n' >&4 2>/dev/null || true
    fi

    # Wait for both processes to actually terminate
    local waited=0
    while [ "$waited" -lt 30 ]; do
        if ! kill -0 "$PID_A" 2>/dev/null && ! kill -0 "$PID_B" 2>/dev/null; then
            break
        fi
        sleep 0.1
        waited=$(( waited + 1 ))
    done
    if ! kill -0 "$PID_A" 2>/dev/null && ! kill -0 "$PID_B" 2>/dev/null; then
        record_check "Both processes terminated cleanly" 0
    else
        record_check "Both processes terminated cleanly" 1
    fi

    if [ "$VERBOSE" -eq 1 ]; then
        dump_logs
    elif [ "$CHECKS_PASSED" -ne "$CHECKS_TOTAL" ]; then
        warn "At least one check failed; see logs for details:"
        dump_logs
    fi
}

# ---------------------------------------------------------------------------
# --live : real, interactive, manual test with tmux
# ---------------------------------------------------------------------------
run_live_test() {
    if ! command -v tmux >/dev/null 2>&1; then
        fail "tmux not found."
        info "To install: sudo apt-get update && sudo apt-get install -y tmux"
        info "After installation, run again: ./test_p2p_chat.sh --live"
        exit 1
    fi

    build_project

    if tmux has-session -t "$SESSION_NAME" 2>/dev/null; then
        warn "Existing tmux session '$SESSION_NAME' found, killing it..."
        tmux kill-session -t "$SESSION_NAME" 2>/dev/null || true
    fi

    info "Preparing tmux session: two peers in side-by-side panes."
    info "  Left pane  = Peer A (local port $PORT_A)"
    info "  Right pane = Peer B (local port $PORT_B)"
    info "You can type messages into either pane and press ENTER like a normal terminal."
    info "To exit: type /quit in both panes, or press CTRL+C, or press CTRL+B then D (session stays in background)."
    echo

    tmux new-session -d -s "$SESSION_NAME" -n chat \
        "$BIN $PORT_A --no-stun 127.0.0.1 $PORT_B"
    tmux split-window -h -t "${SESSION_NAME}:chat" \
        "$BIN $PORT_B --no-stun 127.0.0.1 $PORT_A"
    tmux select-layout -t "${SESSION_NAME}:chat" even-horizontal

    tmux set-option -t "$SESSION_NAME" -g pane-border-status top
    tmux set-option -t "$SESSION_NAME" -g pane-border-format ' #{pane_title} '
    tmux select-pane -t "${SESSION_NAME}:chat.0" -T "PEER A - local port $PORT_A"
    tmux select-pane -t "${SESSION_NAME}:chat.1" -T "PEER B - local port $PORT_B"

    tmux attach -t "$SESSION_NAME"
}

# ---------------------------------------------------------------------------
# main
# ---------------------------------------------------------------------------
printf '%s=== p2p_chat single-machine test tool ===%s\n' "$C_BOLD" "$C_RESET"
info "Mode: $MODE | Ports: $PORT_A <-> $PORT_B | Project: $PROJECT_ROOT"
echo

case "$MODE" in
    auto)
        run_auto_test
        echo
        printf '%s=== RESULT: %d/%d checks passed ===%s\n' \
            "$C_BOLD" "$CHECKS_PASSED" "$CHECKS_TOTAL" "$C_RESET"
        if [ "$CHECKS_PASSED" -eq "$CHECKS_TOTAL" ] && [ "$CHECKS_TOTAL" -gt 0 ]; then
            ok "All checks passed - chat and background ping work together."
            exit 0
        else
            fail "Some checks failed."
            exit 1
        fi
        ;;
    live)
        run_live_test
        ;;
esac
