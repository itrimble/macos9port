# Sourced by the gate: commands keep their existing set -e/export semantics.
progress_tool="$repo/scripts/build_progress.py"
progress_signal=''
progress_exit() {
    progress_code=$1
    trap - EXIT INT TERM
    if ! python3 "$progress_tool" "$OS999_RECEIPT_DIR" exit --code "$progress_code" --reason "$progress_signal"; then
        if [ "$progress_code" -eq 0 ]; then progress_code=1; fi
    fi
    exit "$progress_code"
}
progress_start() { python3 "$progress_tool" "$OS999_RECEIPT_DIR" start --step "$1"; }
progress_pass() { python3 "$progress_tool" "$OS999_RECEIPT_DIR" pass --step "$1"; }
python3 "$progress_tool" "$OS999_RECEIPT_DIR" init --pid "$$"
trap 'progress_exit $?' EXIT
trap 'progress_signal="Interrupted by SIGINT"; exit 130' INT
trap 'progress_signal="Interrupted by SIGTERM"; exit 143' TERM
