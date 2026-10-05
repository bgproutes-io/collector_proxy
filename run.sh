#!/usr/bin/env bash
set -Eeuo pipefail

readonly config_file="${PROXY_CONFIG:-/etc/bgproutes/proxy.conf}"
readonly certificate_generator="/usr/local/libexec/bgproutes/cert_generator.py"
readonly proxy_binary="/usr/local/bin/collector-proxy"
readonly proxy_pid_file="/run/bgproutes/proxy.pid"

if [[ ! -r "$config_file" ]]; then
    echo "Proxy configuration is not readable: $config_file" >&2
    exit 1
fi

config_value() {
    local key="$1"
    awk -v wanted="$key" '
        /^[[:space:]]*#/ { next }
        {
            separator = index($0, ":")
            if (!separator) next
            name = substr($0, 1, separator - 1)
            value = substr($0, separator + 1)
            gsub(/^[[:space:]]+|[[:space:]]+$/, "", name)
            gsub(/^[[:space:]]+|[[:space:]]+$/, "", value)
            if (name == wanted) result = value
        }
        END { print result }
    ' "$config_file"
}

tls_value="$(config_value use_tls)"
tls_value="${tls_value,,}"

case "$tls_value" in
    1|true|yes|on)
        tls_enabled=true
        ;;
    0|false|no|off)
        tls_enabled=false
        ;;
    *)
        echo "Invalid or missing use_tls value in $config_file" >&2
        exit 1
        ;;
esac

if [[ "$tls_enabled" == false ]]; then
    exec "$proxy_binary" --config "$config_file"
fi

certificate_directory="$(config_value internal_directory)"
if [[ "$certificate_directory" != /* ]]; then
    echo "internal_directory must be an absolute path when TLS is enabled" >&2
    exit 1
fi
mkdir -p "$certificate_directory"
if [[ ! -w "$certificate_directory" ]]; then
    echo "Certificate directory is not writable: $certificate_directory" >&2
    exit 1
fi

# Complete initial enrollment, or validate a certificate set restored from the
# persistent volume, before allowing the proxy to load its TLS context.
python3 "$certificate_generator" --bootstrap-only "$config_file"

cert_pid=""
proxy_pid=""

terminate_children() {
    trap - TERM INT HUP
    rm -f "$proxy_pid_file"
    if [[ -n "$proxy_pid" ]] && kill -0 "$proxy_pid" 2>/dev/null; then
        kill -TERM "$proxy_pid" 2>/dev/null || true
    fi
    if [[ -n "$cert_pid" ]] && kill -0 "$cert_pid" 2>/dev/null; then
        kill -TERM "$cert_pid" 2>/dev/null || true
    fi
    [[ -z "$proxy_pid" ]] || wait "$proxy_pid" 2>/dev/null || true
    [[ -z "$cert_pid" ]] || wait "$cert_pid" 2>/dev/null || true
}

handle_signal() {
    terminate_children
    exit 143
}

trap handle_signal TERM INT HUP

python3 "$certificate_generator" \
    --notify-pid-file "$proxy_pid_file" "$config_file" &
cert_pid=$!

"$proxy_binary" --config "$config_file" &
proxy_pid=$!
printf '%s\n' "$proxy_pid" > "$proxy_pid_file"

set +e
exited_pid=""
wait -n -p exited_pid "$cert_pid" "$proxy_pid"
exit_status=$?
set -e

# The renewal daemon is intended to live for the whole TLS-enabled container.
# A clean daemon exit while the proxy is still alive is therefore a failure.
if [[ "$exited_pid" == "$cert_pid" ]] && kill -0 "$proxy_pid" 2>/dev/null; then
    echo "Certificate renewal daemon exited unexpectedly" >&2
    [[ "$exit_status" -ne 0 ]] || exit_status=1
fi

terminate_children
exit "$exit_status"
