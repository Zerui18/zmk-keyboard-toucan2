#!/usr/bin/env bash

set -euo pipefail

readonly REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
readonly BUILD_IMAGE="${ZMK_BUILD_IMAGE:-zmkfirmware/zmk-build-arm:stable}"
readonly WORKSPACE_VOLUME="${ZMK_BUILD_VOLUME:-toucan2-zmk-v03}"
readonly BOOTLOADER_PATH="${BOOTLOADER:-/Volumes/XIAO-BOOT}"
readonly FIRMWARE_DIR="${REPO_ROOT}/firmware"

log() {
    printf '==> %s\n' "$*"
}

die() {
    printf 'error: %s\n' "$*" >&2
    exit 1
}

usage() {
    cat <<'EOF'
Usage: scripts/zmk.sh COMMAND [SIDE]

Commands:
  setup                 Initialize the persistent West workspace
  build left|right      Build one firmware image
  install left|right    Build and copy one image to the bootloader
  flash left|right      Copy an existing image to the bootloader
  update                Update all West-managed dependencies
  clean                 Remove generated build output
  doctor                Check the local development environment
  shell                 Open an interactive builder shell
EOF
}

require_docker() {
    command -v docker >/dev/null 2>&1 || die "Docker is not installed"
    docker info >/dev/null 2>&1 || die "Docker Desktop is not running"
}

validate_side() {
    case "${1:-}" in
        left|right) ;;
        *) die "side must be 'left' or 'right'" ;;
    esac
}

artifact_for_side() {
    printf '%s/toucan_%s.uf2\n' "$FIRMWARE_DIR" "$1"
}

docker_args() {
    printf '%s\0' \
        run --rm --pull=missing \
        --mount "type=bind,source=${REPO_ROOT},target=/repo" \
        --mount "type=volume,source=${WORKSPACE_VOLUME},target=/workspace" \
        --workdir /workspace
}

run_container() {
    local -a args=()
    while IFS= read -r -d '' arg; do
        args+=("$arg")
    done < <(docker_args)

    docker "${args[@]}" "$BUILD_IMAGE" bash /repo/scripts/zmk-container.sh "$@"
}

run_shell() {
    local -a args=()
    while IFS= read -r -d '' arg; do
        args+=("$arg")
    done < <(docker_args)

    run_container setup
    docker "${args[@]}" -it "$BUILD_IMAGE" bash
}

build_side() {
    local side="$1"
    validate_side "$side"
    mkdir -p "$FIRMWARE_DIR"
    run_container build "$side"
}

flash_side() {
    local side="$1"
    local artifact
    local destination

    validate_side "$side"
    artifact="$(artifact_for_side "$side")"
    [[ -s "$artifact" ]] || die "missing firmware: ${artifact}; run 'make ${side}' first"
    [[ -d "$BOOTLOADER_PATH" ]] || die \
        "bootloader not found at ${BOOTLOADER_PATH}; double-tap reset, then retry"

    destination="${BOOTLOADER_PATH}/$(basename "$artifact")"
    log "Flashing Toucan2 ${side}: ${artifact} -> ${destination}"
    if [[ "$(uname -s)" == "Darwin" ]]; then
        # The UF2 bootloader reboots and unmounts as soon as it accepts the
        # payload. Prevent macOS cp from subsequently touching extended
        # attributes on a volume that is no longer present.
        COPYFILE_DISABLE=1 cp -X "$artifact" "$destination"
    else
        cp "$artifact" "$destination"
    fi
    log "Copy complete; the XIAO bootloader may now unmount and reboot"
}

doctor() {
    printf 'Repository:       %s\n' "$REPO_ROOT"
    printf 'Builder image:    %s\n' "$BUILD_IMAGE"
    printf 'Workspace volume: %s\n' "$WORKSPACE_VOLUME"
    printf 'Bootloader:       %s' "$BOOTLOADER_PATH"
    if [[ -d "$BOOTLOADER_PATH" ]]; then
        printf ' (mounted)\n'
    else
        printf ' (not mounted)\n'
    fi

    if command -v docker >/dev/null 2>&1 && docker info >/dev/null 2>&1; then
        printf 'Docker:           ready\n'
    else
        printf 'Docker:           unavailable\n'
        return 1
    fi

    if docker image inspect "$BUILD_IMAGE" >/dev/null 2>&1; then
        printf 'Builder cache:    image present\n'
    else
        printf 'Builder cache:    image will be pulled on first build\n'
    fi

    if docker volume inspect "$WORKSPACE_VOLUME" >/dev/null 2>&1; then
        printf 'West workspace:   initialized or reserved\n'
    else
        printf 'West workspace:   will be created on first build\n'
    fi

    printf 'origin:            %s\n' "$(git -C "$REPO_ROOT" remote get-url origin 2>/dev/null || printf 'missing')"
    printf 'upstream:          %s\n' "$(git -C "$REPO_ROOT" remote get-url upstream 2>/dev/null || printf 'missing')"
}

main() {
    local command="${1:-help}"
    local side="${2:-}"

    case "$command" in
        help|-h|--help)
            usage
            ;;
        doctor)
            doctor
            ;;
        setup)
            require_docker
            mkdir -p "$FIRMWARE_DIR"
            run_container setup
            ;;
        update)
            require_docker
            run_container update
            ;;
        build)
            require_docker
            build_side "$side"
            ;;
        install)
            require_docker
            build_side "$side"
            flash_side "$side"
            ;;
        flash)
            flash_side "$side"
            ;;
        clean)
            require_docker
            run_container clean
            ;;
        shell)
            require_docker
            run_shell
            ;;
        *)
            usage >&2
            die "unknown command: ${command}"
            ;;
    esac
}

main "$@"
