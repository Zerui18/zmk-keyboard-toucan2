#!/usr/bin/env bash

set -euo pipefail

readonly REPO_DIR=/repo
readonly WORKSPACE_DIR=/workspace
readonly MANIFEST_DIR="${WORKSPACE_DIR}/config"
readonly MANIFEST_FILE="${MANIFEST_DIR}/west.yml"
readonly MANIFEST_STAMP="${WORKSPACE_DIR}/.toucan-west-manifest.sha256"
readonly BUILD_ROOT="${WORKSPACE_DIR}/build"
readonly FIRMWARE_DIR="${REPO_DIR}/firmware"

# Each Make command uses a fresh container, so do not rely on the per-container
# CMake user package registry written by `west zephyr-export`.
export ZEPHYR_BASE="${WORKSPACE_DIR}/zephyr"

log() {
    printf '==> %s\n' "$*"
}

update_west_projects() {
    local attempt

    # Large partial-clone fetches are more reliable through Docker Desktop when
    # Git avoids multiplexed HTTP/2 streams. Keep a retry for transient GitHub
    # disconnects; West reuses every project fetched by earlier attempts.
    git config --global http.version HTTP/1.1

    for attempt in 1 2 3; do
        if west update --fetch-opt=--filter=tree:0; then
            return 0
        fi

        if [[ "$attempt" -lt 3 ]]; then
            log "West update attempt ${attempt} failed; retrying"
            sleep $((attempt * 2))
        fi
    done

    printf 'error: West update failed after 3 attempts\n' >&2
    return 1
}

ensure_workspace() {
    local force_update="${1:-false}"
    local current_hash
    local recorded_hash=""
    local update_required=false

    mkdir -p "$MANIFEST_DIR"
    cp "${REPO_DIR}/config/west.yml" "$MANIFEST_FILE"
    current_hash="$(sha256sum "$MANIFEST_FILE" | awk '{print $1}')"

    if [[ -f "$MANIFEST_STAMP" ]]; then
        recorded_hash="$(<"$MANIFEST_STAMP")"
    fi

    cd "$WORKSPACE_DIR"

    if [[ ! -f "${WORKSPACE_DIR}/.west/config" ]]; then
        log "Initializing West workspace"
        west init -l "$MANIFEST_DIR"
        update_required=true
    fi

    if [[ "$force_update" == true || "$current_hash" != "$recorded_hash" || ! -d "${WORKSPACE_DIR}/zmk" ]]; then
        update_required=true
    fi

    if [[ "$update_required" == true ]]; then
        log "Fetching ZMK and manifest modules"
        update_west_projects
        west zephyr-export
        printf '%s\n' "$current_hash" >"$MANIFEST_STAMP"
    fi
}

build_side() {
    local side="$1"
    local build_dir="${BUILD_ROOT}/${side}"
    local build_signature
    local build_signature_stamp="${BUILD_ROOT}/.${side}-build-command.sha256"
    local previous_build_signature=""
    local artifact="${FIRMWARE_DIR}/toucan_${side}.uf2"
    local -a west_args=(
        build
        -p auto
        -s zmk/app
        -d "$build_dir"
        -b seeeduino_xiao_ble
    )
    local -a cmake_args=(
        -DZephyr_DIR=/workspace/zephyr/share/zephyr-package/cmake
        -DZMK_CONFIG=/repo/config
        -DZMK_EXTRA_MODULES=/repo
    )

    case "$side" in
        left)
            west_args+=(-S studio-rpc-usb-uart)
            cmake_args+=(
                "-DSHIELD=toucan_left rgbled_adapter nice_view_gem"
                -DCONFIG_ZMK_STUDIO=y
            )
            ;;
        right)
            west_args+=(-S studio-rpc-usb-uart)
            cmake_args+=("-DSHIELD=toucan_right rgbled_adapter")
            ;;
        *)
            printf 'error: unsupported side: %s\n' "$side" >&2
            exit 2
            ;;
    esac

    build_signature="$(
        printf '%s\n' "${west_args[@]}" -- "${cmake_args[@]}" | sha256sum | awk '{print $1}'
    )"
    if [[ -f "$build_signature_stamp" ]]; then
        previous_build_signature="$(<"$build_signature_stamp")"
    fi

    if [[ "$build_signature" != "$previous_build_signature" && -d "$build_dir" ]]; then
        log "Build configuration changed; cleaning cached ${side} CMake output"
        cmake -E remove_directory "$build_dir"
    fi

    ensure_workspace false
    mkdir -p "$FIRMWARE_DIR"
    cd "$WORKSPACE_DIR"

    log "Building Toucan2 ${side}"
    west "${west_args[@]}" -- "${cmake_args[@]}"

    [[ -s "${build_dir}/zephyr/zmk.uf2" ]] || {
        printf 'error: build completed without zmk.uf2\n' >&2
        exit 1
    }

    install -m 0644 "${build_dir}/zephyr/zmk.uf2" "$artifact"
    printf '%s\n' "$build_signature" >"$build_signature_stamp"
    log "Firmware ready: ${artifact}"
}

clean_outputs() {
    log "Removing generated build output"
    cmake -E remove_directory "$BUILD_ROOT"
    rm -f \
        "${BUILD_ROOT}/.left-build-command.sha256" \
        "${BUILD_ROOT}/.right-build-command.sha256" \
        "${FIRMWARE_DIR}/toucan_left.uf2" \
        "${FIRMWARE_DIR}/toucan_right.uf2"
}

main() {
    local command="${1:-setup}"

    case "$command" in
        setup)
            ensure_workspace false
            west --version
            cmake --version | head -n 1
            ;;
        update)
            ensure_workspace true
            ;;
        build)
            build_side "${2:-}"
            ;;
        test-memory)
            ensure_workspace false
            mkdir -p "${BUILD_ROOT}/tests"
            cc -std=c11 -Wall -Wextra -Werror \
                -I"${WORKSPACE_DIR}/zmk/app/include" -I"${REPO_DIR}/src" \
                -I"${REPO_DIR}/boards/shields/nice_view_gem/assets" \
                "${REPO_DIR}/tests/memory_case.c" "${REPO_DIR}/src/toucan_key_text.c" \
                -o "${BUILD_ROOT}/tests/memory-case"
            "${BUILD_ROOT}/tests/memory-case"
            cc -std=c11 -Wall -Wextra -Werror \
                -I"${WORKSPACE_DIR}/zmk/app/include" -I"${REPO_DIR}/src" \
                "${REPO_DIR}/tests/memory_sequence.c" \
                "${REPO_DIR}/src/toucan_memory_sequence.c" "${REPO_DIR}/src/toucan_key_text.c" \
                -o "${BUILD_ROOT}/tests/memory-sequence"
            "${BUILD_ROOT}/tests/memory-sequence"
            python3 "${REPO_DIR}/tests/test_memory_font.py"
            west build -s "${WORKSPACE_DIR}/zmk/app" \
                -d "${BUILD_ROOT}/tests/memory-capture" -b native_posix_64 -- \
                -DZephyr_DIR=/workspace/zephyr/share/zephyr-package/cmake \
                "-DZMK_CONFIG=${REPO_DIR}/tests/memory_capture" \
                "-DZMK_EXTRA_MODULES=${REPO_DIR};${REPO_DIR}/tests/memory_capture" \
                -DCONFIG_ASSERT=y
            if ! timeout 30 "${BUILD_ROOT}/tests/memory-capture/zephyr/zmk.exe" \
                >"${BUILD_ROOT}/tests/memory-capture/replay.log" 2>&1; then
                cat "${BUILD_ROOT}/tests/memory-capture/replay.log"
                exit 1
            fi
            if ! grep 'PASS: private TEXT/SEQ capture' \
                "${BUILD_ROOT}/tests/memory-capture/replay.log"; then
                cat "${BUILD_ROOT}/tests/memory-capture/replay.log"
                exit 1
            fi
            ;;
        clean)
            clean_outputs
            ;;
        *)
            printf 'error: unsupported container command: %s\n' "$command" >&2
            exit 2
            ;;
    esac
}

main "$@"
