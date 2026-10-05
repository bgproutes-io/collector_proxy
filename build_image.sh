#!/usr/bin/env bash
set -Eeuo pipefail

readonly repository_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
readonly libbmproutes_context="${LIBBMPROUTES_CONTEXT:-$repository_dir/../pybmproutes/libbmproutes}"
readonly image_name="${IMAGE_NAME:-bgproutes/collector-proxy:latest}"

if [[ ! -f "$libbmproutes_context/configure.ac" ]]; then
    echo "libbmproutes source not found at $libbmproutes_context" >&2
    echo "Set LIBBMPROUTES_CONTEXT to its source directory." >&2
    exit 1
fi

docker buildx build \
    --load \
    --build-context "libbmproutes=$libbmproutes_context" \
    --file "$repository_dir/docker/Dockerfile" \
    --tag "$image_name" \
    "$repository_dir"
