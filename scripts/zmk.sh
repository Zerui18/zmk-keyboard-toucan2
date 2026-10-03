#!/usr/bin/env bash

set -euo pipefail

readonly REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
readonly BUILD_IMAGE="${ZMK_BUILD_IMAGE:-zmkfirmware/zmk-build-arm:stable}"
readonly WORKSPACE_VOLUME="${ZMK_BUILD_VOLUME:-toucan2-zmk-v03}"
readonly BOOTLOADER_PATH="${BOOTLOADER:-/Volumes/XIAO-BOOT}"
readonly FIRMWARE_DIR="${REPO_ROOT}/firmware"
readonly HOST_PYTHON="${PYTHON:-python3}"
readonly USB_TOOL="${REPO_ROOT}/scripts/toucan-usb.py"
readonly FIRMWARE_GIT_SHA="$(git -C "$REPO_ROOT" rev-parse --short=7 HEAD 2>/dev/null || true)"

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
  install left|right    Build, reboot, and flash one half
  flash left|right      Reboot and flash one half with an existing image
  install-both          Build, reboot, and flash both USB-connected halves
  flash-both            Reboot and flash both halves with existing images
  usb-check             Verify both runtime USB ports are available
  test-memory           Test memory case, SEQ names, font, and capture/playback
  test-platform         Test per-host modes, persistence, and shortcuts
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
        --env "TOUCAN_GIT_SHA=${FIRMWARE_GIT_SHA}" \
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

run_usb_tool() {
    command -v "$HOST_PYTHON" >/dev/null 2>&1 || die "Python is not installed: ${HOST_PYTHON}"
    [[ -f "$USB_TOOL" ]] || die "missing USB helper: ${USB_TOOL}"
    "$HOST_PYTHON" "$USB_TOOL" "$@"
}

wait_for_bootloader_mount() {
    local deadline=$((SECONDS + 10))
    local ready_checks=0

    while (( SECONDS < deadline )); do
        # Disk Arbitration creates the root-owned mount-point directory shortly
        # before the FAT filesystem is attached. Seeing only the directory can
        # race cp and produce a misleading EACCES. INFO_UF2.TXT proves that the
        # actual XIAO bootloader filesystem is mounted; require it twice so the
        # mount has a brief settling interval as well.
        if [[ -r "${BOOTLOADER_PATH}/INFO_UF2.TXT" ]]; then
            ((ready_checks += 1))
            if (( ready_checks >= 2 )); then
                return 0
            fi
        else
            ready_checks=0
        fi
        sleep 0.1
    done

    die "bootloader did not become ready at ${BOOTLOADER_PATH}"
}

wait_for_bootloader_unmount() {
    local deadline=$((SECONDS + 10))

    while (( SECONDS < deadline )); do
        if [[ ! -d "$BOOTLOADER_PATH" ]]; then
            return 0
        fi
        sleep 0.1
    done

    die "bootloader remained mounted at ${BOOTLOADER_PATH} after flashing"
}

flash_side() {
    local side="$1"
    local artifact
    local destination

    validate_side "$side"
    artifact="$(artifact_for_side "$side")"
    [[ -s "$artifact" ]] || die "missing firmware: ${artifact}; run 'make ${side}' first"
    [[ -r "${BOOTLOADER_PATH}/INFO_UF2.TXT" ]] || die \
        "bootloader not ready at ${BOOTLOADER_PATH}; double-tap reset, then retry"

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

auto_flash_side() {
    local side="$1"
    local artifact

    validate_side "$side"
    artifact="$(artifact_for_side "$side")"
    [[ -s "$artifact" ]] || die "missing firmware: ${artifact}; run 'make ${side}' first"

    # Accept a half that was put into UF2 manually as a bootstrap/fallback.
    # Otherwise use the side-specific runtime USB identity and guarded serial
    # handshake so the requested half is the one that enters the bootloader.
    if [[ -r "${BOOTLOADER_PATH}/INFO_UF2.TXT" ]]; then
        log "Bootloader already ready at ${BOOTLOADER_PATH}; flashing ${side}"
    else
        [[ ! -d "$BOOTLOADER_PATH" ]] || die \
            "bootloader mount point exists but is not ready at ${BOOTLOADER_PATH}; eject it, then retry"
        run_usb_tool touch "$side"
        wait_for_bootloader_mount
    fi

    flash_side "$side"
    wait_for_bootloader_unmount
    run_usb_tool wait "$side" --timeout 15
}

auto_flash_both() {
    local side
    local artifact

    for side in left right; do
        artifact="$(artifact_for_side "$side")"
        [[ -s "$artifact" ]] || die \
            "missing firmware: ${artifact}; run 'make all' or 'make install-both' first"
    done

    run_usb_tool check
    auto_flash_side left
    auto_flash_side right
    run_usb_tool check
    log "Both Toucan2 halves flashed successfully"
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
            auto_flash_side "$side"
            ;;
        install-both)
            run_usb_tool check
            require_docker
            build_side left
            build_side right
            auto_flash_both
            ;;
        flash)
            auto_flash_side "$side"
            ;;
        flash-both)
            auto_flash_both
            ;;
        usb-check)
            run_usb_tool check
            ;;
        test-memory|test-platform)
            require_docker
            run_container "$command"
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
