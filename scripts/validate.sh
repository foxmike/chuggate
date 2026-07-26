#!/usr/bin/env bash
# scripts/validate.sh — ChugGate release validation entry point.
#
# Implements the release-time checks required by CHUGGATE_SPEC.md
# §30 (Testing) and §31 (Release Criteria). It is the single script the
# release process (Phase K) invokes to assert the plugin is shippable.
#
# Scope: build, test, RT-safety scan, latency verification, bypass/state/
# multiband hooks. It must exit non-zero as soon as any enforced step fails.
#
# This script intentionally reuses the project's existing CMake presets
# (scripts/build_all.sh / CMakePresets.json) and the ctest test runner
# registered by CMakeLists.txt (`add_test(NAME ChugGateTests ...)`).
# It does NOT reimplement build or test logic and does not modify any
# Source/**, CMakeLists.txt, or tests/** files.

set -euo pipefail

# ---------------------------------------------------------------------------
# Configuration
# ---------------------------------------------------------------------------

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
cd "${ROOT_DIR}"

PRESET="${PRESET:-linux-release}"
BUILD_DIR="${BUILD_DIR:-build/${PRESET}}"
CONFIG="${CONFIG:-Release}"

# Lookahead values (ms) covered by CHUGGATE_SPEC.md §17 / §29.
# Default is "off" (zero reported latency). Legal lookahead settings:
# 0.25, 0.5, 1, 2, 5, 10 ms.
LOOKAHEAD_MS_VALUES=(0.25 0.5 1 2 5 10)

# Source translation units that contribute to processBlock and therefore
# MUST be allocation/lock/IO-free on the audio thread after prepare().
# Paths are relative to ROOT_DIR.
RT_SAFETY_PATHS=(
    "Source/DSP"
    "Source/AlignmentAnalyzer.cpp"
    "Source/AlignmentAnalyzer.h"
    "Source/AnomalyGuard.cpp"
    "Source/AnomalyGuard.h"
    "Source/AttackGuard.cpp"
    "Source/AttackGuard.h"
    "Source/CadenceTracker.cpp"
    "Source/CadenceTracker.h"
    "Source/DetectorEngine.cpp"
    "Source/DetectorEngine.h"
    "Source/FeedbackDetector.cpp"
    "Source/FeedbackDetector.h"
    "Source/FundamentalEstimator.cpp"
    "Source/FundamentalEstimator.h"
    "Source/GainComputer.cpp"
    "Source/GainComputer.h"
    "Source/GainSmoother.cpp"
    "Source/GainSmoother.h"
    "Source/HissCurtain.cpp"
    "Source/HissCurtain.h"
    "Source/KeyConditioner.cpp"
    "Source/KeyConditioner.h"
    "Source/KeySourceRouter.cpp"
    "Source/KeySourceRouter.h"
    "Source/NoiseFloorEstimator.cpp"
    "Source/NoiseFloorEstimator.h"
    "Source/PerformanceStateMachine.cpp"
    "Source/PerformanceStateMachine.h"
    "Source/PowerlineShield.cpp"
    "Source/PowerlineShield.h"
    "Source/RejectedSignalRouter.cpp"
    "Source/RejectedSignalRouter.h"
    "Source/StereoLinker.cpp"
    "Source/StereoLinker.h"
    "Source/SustainAnalyzer.cpp"
    "Source/SustainAnalyzer.h"
)

# Patterns that are forbidden inside processBlock-related TUs after prepare().
# NB: this is a coarse static guard. It is intentionally permissive about
# comments and prepared/preallocated buffers; it flags any remaining raw
# allocation/lock/file-IO tokens that would endanger RT safety.
RT_FORBIDDEN_PATTERNS=(
    '\bnew[[:space:]]+[A-Z]'
    '\bdelete[[:space:]]+'
    '\bmalloc[[:space:]]*\('
    '\bcalloc[[:space:]]*\('
    '\brealloc[[:space:]]*\('
    '\bfree[[:space:]]*\('
    '\bstd::lock_guard\b'
    '\bstd::unique_lock\b'
    '\bstd::scoped_lock\b'
    '\bCriticalSection\b'
    '\bScopedLock\b'
    '\bScopedWriteLock\b'
    '\bScopedReadLock\b'
    '\bfopen[[:space:]]*\('
    '\bfclose[[:space:]]*\('
    '\bstd::ofstream\b'
    '\bstd::ifstream\b'
    '\bstd::fstream\b'
)

# Counters for final summary.
PASS_COUNT=0
FAIL_COUNT=0
FAILED_STEPS=()

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

color() { :; }  # no-op unless terminal supports color
if [[ -t 1 ]] && command -v tput >/dev/null 2>&1; then
    GREEN="$(tput setaf 2 2>/dev/null || true)"
    RED="$(tput setaf 1 2>/dev/null || true)"
    YELLOW="$(tput setaf 3 2>/dev/null || true)"
    BOLD="$(tput bold 2>/dev/null || true)"
    RESET="$(tput sgr0 2>/dev/null || true)"
else
    GREEN=""; RED=""; YELLOW=""; BOLD=""; RESET=""
fi

section() {
    printf '\n%s==== %s ====%s\n' "${BOLD}" "$1" "${RESET}"
}

pass() {
    printf '%s[PASS]%s %s\n' "${GREEN}" "${RESET}" "$1"
    PASS_COUNT=$((PASS_COUNT + 1))
}

fail() {
    printf '%s[FAIL]%s %s\n' "${RED}" "${RESET}" "$1" >&2
    FAIL_COUNT=$((FAIL_COUNT + 1))
    FAILED_STEPS+=("$1")
}

# Run a command, recording pass/fail by name. Does NOT abort on failure —
# we want to run every enforced step and report a complete summary.
run_step() {
    local name="$1"; shift
    printf '%s[RUN ]%s %s\n' "${YELLOW}" "${RESET}" "$name"
    if "$@"; then
        pass "$name"
        return 0
    else
        fail "$name"
        return 1
    fi
}

# ---------------------------------------------------------------------------
# Step 1 — Build Release (VST3 + Standalone)
# ---------------------------------------------------------------------------

build_release() {
    section "Step 1: Build Release (VST3 + Standalone)"

    # Configure using the project preset (see CMakePresets.json).
    cmake --preset "${PRESET}"

    # Build all targets, including the VST3 and Standalone plugin formats
    # declared by `juce_add_plugin(ChugGate ... FORMATS VST3 Standalone)`
    # and the ChugGateTests executable used by ctest.
    cmake --build "${BUILD_DIR}" --config "${CONFIG}" --parallel

    # Verify the expected plugin artifacts were produced. The exact paths
    # depend on JUCE's artefact layout; we assert the build succeeded by
    # checking the plugin target binary exists.
    local found_vst3=0
    local found_standalone=0
    while IFS= read -r -d '' f; do
        case "$f" in
            *.vst3) found_vst3=1 ;;
            ChugGate|ChugGate_Standalone) found_standalone=1 ;;
        esac
    done < <(find "${BUILD_DIR}" -type f \( -name '*.vst3' -o -name 'ChugGate' -o -name 'ChugGate_Standalone' \) -print0 2>/dev/null)

    # VST3 may be a bundle directory rather than a single file; check dirs too.
    if [[ ${found_vst3} -eq 0 ]]; then
        if find "${BUILD_DIR}" -type d -name 'ChugGate.vst3' -print -quit 2>/dev/null | grep -q .; then
            found_vst3=1
        fi
    fi

    if [[ ${found_vst3} -eq 0 ]]; then
        fail "Build: VST3 artifact not found under ${BUILD_DIR}"
        return 1
    fi
    if [[ ${found_standalone} -eq 0 ]]; then
        # Standalone target name may be `ChugGate_Standalone` (JUCE default).
        if find "${BUILD_DIR}" -type f -name 'ChugGate_Standalone' -print -quit 2>/dev/null | grep -q .; then
            found_standalone=1
        fi
    fi
    if [[ ${found_standalone} -eq 0 ]]; then
        fail "Build: Standalone artifact not found under ${BUILD_DIR}"
        return 1
    fi

    pass "Build: VST3 + Standalone artifacts produced"
    return 0
}

# ---------------------------------------------------------------------------
# Step 2 — Run ctest
# ---------------------------------------------------------------------------

run_ctest() {
    section "Step 2: Run ctest (ChugGateTests)"

    # CMakeLists.txt registers: add_test(NAME ChugGateTests COMMAND ChugGateTests)
    # The executable is a JUCE UnitTest runner covering Detector, StateMachine,
    # Attack, Sustain, Cadence, Lookahead, Multiband, Stereo, Alignment,
    # Analysis and State round-trip suites.
    ctest --test-dir "${BUILD_DIR}" --build-config "${CONFIG}" \
          --output-on-failure --parallel
}

# ---------------------------------------------------------------------------
# Step 3 — RT-safety static scan
# ---------------------------------------------------------------------------

rt_safety_scan() {
    section "Step 3: RT-safety static scan"

    local violations=0
    local tmp
    tmp="$(mktemp)"
    trap 'rm -f "${tmp}"' RETURN

    printf 'Scanning processBlock-related translation units for forbidden RT patterns...\n'

    local path pat
    for path in "${RT_SAFETY_PATHS[@]}"; do
        local full="${ROOT_DIR}/${path}"
        if [[ ! -e "${full}" ]]; then
            # Skip paths that don't exist in this checkout (some TUs are
            # generated or optional). Not a failure.
            continue
        fi

        # Build a grep of source files under the path.
        local files=()
        if [[ -d "${full}" ]]; then
            while IFS= read -r -d '' f; do
                files+=("$f")
            done < <(find "${full}" -maxdepth 1 -type f \( -name '*.cpp' -o -name '*.h' \) -print0)
        elif [[ -f "${full}" ]]; then
            files+=("${full}")
        fi

        if [[ ${#files[@]} -eq 0 ]]; then
            continue
        fi

        for pat in "${RT_FORBIDDEN_PATTERNS[@]}"; do
            # -E: extended regex; -n: line numbers; -H: filename.
            # We exclude comment-only lines and known allow-listed call sites.
            if grep -En -- "${pat}" "${files[@]}" \
                | grep -vE '^\s*[^:]+:[0-9]+:\s*//' \
                | grep -vE '// *RT-?SAFE|// *allow' \
                > "${tmp}" 2>/dev/null; then
                printf '%sRT violation in %s:%s\n' "${RED}" "${path}" "${RESET}" >&2
                cat "${tmp}" >&2
                violations=$((violations + 1))
            fi
        done
    done

    if [[ ${violations} -ne 0 ]]; then
        fail "RT-safety: ${violations} forbidden pattern(s) found in Source audio-path TUs"
        return 1
    fi

    pass "RT-safety: no forbidden alloc/lock/IO patterns in audio-path TUs"
    return 0
}

# ---------------------------------------------------------------------------
# Steps 4 & 5 — Latency verification
# ---------------------------------------------------------------------------

# Latency is asserted via the ChugGateTests "ChugGate Lookahead Tests" suite
# (tests/LookaheadTests.cpp), which directly verifies:
#   * Off => getLatencySamples() == 0 (zero-latency default)
#   * For each legal ms setting: latencySamples == round(tau * fs)
#   * Impulse peak delayed to the expected index
# These checks are exercised by ctest in Step 2; this section re-asserts
# them explicitly so the validation report calls them out by name.

verify_zero_latency() {
    section "Step 4: Zero-latency verification (lookahead off -> 0 samples)"

    # The authoritative zero-latency assertion lives in tests/LookaheadTests.cpp
    # (ChugGate Lookahead Tests, category "DSP"). The binding pass/fail comes
    # from the ctest run in Step 2, which invokes the ChugGateTests binary
    # (a JUCE UnitTestRunner that runs every registered suite and exits
    # non-zero on any failure). Here we assert the fixture is actually
    # present and pins the contract: a freshly constructed LookaheadDelay
    # with lookahead == Off must report getLatencySamples() == 0.
    if [[ ! -f "${ROOT_DIR}/tests/LookaheadTests.cpp" ]]; then
        fail "Zero-latency: tests/LookaheadTests.cpp not found"
        return 1
    fi

    if ! grep -qE 'getLatencySamples\(\)\s*==\s*0' "${ROOT_DIR}/tests/LookaheadTests.cpp"; then
        fail "Zero-latency: LookaheadTests.cpp missing 0-samples assertion"
        return 1
    fi

    # CHUGGATE_SPEC.md §29: "Default: 0 samples. Covers all modes without
    # lookahead." Confirm the default-Off contract is asserted explicitly.
    if ! grep -qiE 'default|Off' "${ROOT_DIR}/tests/LookaheadTests.cpp"; then
        fail "Zero-latency: LookaheadTests.cpp missing default-Off coverage"
        return 1
    fi

    pass "Zero-latency: default/off lookahead asserts 0 reported samples (binding: ctest Step 2)"
    return 0
}

verify_lookahead_latency() {
    section "Step 5: Lookahead reported-latency verification (0.25-10 ms)"

    # CHUGGATE_SPEC.md §17/§29: lookahead settings off, 0.25, 0.5, 1, 2, 5, 10 ms.
    # Reported host latency must equal round(tau_L * fs) exactly — no hidden
    # latency. tests/LookaheadTests.cpp::runLegalLatencySamples() asserts
    # getLatencySamples() == round(ms * 0.001 * fs) for every legal value,
    # and that an impulse peak is delayed to the expected index. The binding
    # pass/fail is the ctest run in Step 2; here we assert the fixture covers
    # every shipped lookahead value with the exact-sample-count formula.
    if [[ ! -f "${ROOT_DIR}/tests/LookaheadTests.cpp" ]]; then
        fail "Lookahead latency: tests/LookaheadTests.cpp not found"
        return 1
    fi

    local ms
    for ms in "${LOOKAHEAD_MS_VALUES[@]}"; do
        # The fixture enumerates the legal values as float literals.
        if ! grep -qE "lookahead[^A-Za-z]*${ms//./\\.}f" "${ROOT_DIR}/tests/LookaheadTests.cpp"; then
            fail "Lookahead latency: ${ms} ms not covered by LookaheadTests fixture"
            return 1
        fi
    done

    # Confirm the exact-sample-count assertion is present.
    if ! grep -qE 'lround\(.+\*\s*0\.001\s*\*\s*kSampleRate\)' "${ROOT_DIR}/tests/LookaheadTests.cpp"; then
        fail "Lookahead latency: exact round(tau*fs) assertion missing"
        return 1
    fi

    pass "Lookahead latency: 0.25/0.5/1/2/5/10 ms assert exact round(tau*fs) samples (binding: ctest Step 2)"
    return 0
}

# ---------------------------------------------------------------------------
# Step 6 — Bypass click-free hook
# ---------------------------------------------------------------------------

verify_bypass_clickfree() {
    section "Step 6: Bypass click-free check"

    # tests/StateTests.cpp::runCrossfadeBypassSmoke() covers A/B switch +
    # bypass toggle (CHUGGATE_SPEC.md §22.1/§22.2). It is part of the
    # ChugGateTests binary run by ctest in Step 2 (binding pass/fail). Here
    # we assert the bypass/crossfade test exists; once a dedicated click-free
    # bypass fixture lands under tests/, this hook will pick it up too.
    if [[ ! -f "${ROOT_DIR}/tests/StateTests.cpp" ]]; then
        fail "Bypass click-free: tests/StateTests.cpp not found"
        return 1
    fi

    if ! grep -qiE 'bypass|Crossfade' "${ROOT_DIR}/tests/StateTests.cpp"; then
        fail "Bypass click-free: StateTests.cpp bypass/crossfade test missing"
        return 1
    fi

    pass "Bypass click-free: StateTests crossfade/bypass suite present (binding: ctest Step 2)"
    return 0
}

# ---------------------------------------------------------------------------
# Step 7 — State round-trip
# ---------------------------------------------------------------------------

verify_state_roundtrip() {
    section "Step 7: State round-trip (schema, A/B, noise, alignment)"

    # StateTests.cpp covers: profiles, A/B copy/switch/serialize-restore
    # with schema_version, factory preset parsing, crossfade/bypass smoke
    # (CHUGGATE_SPEC.md §22, §24). It is run by ctest in Step 2 (binding).
    if [[ ! -f "${ROOT_DIR}/tests/StateTests.cpp" ]]; then
        fail "State round-trip: tests/StateTests.cpp not found"
        return 1
    fi

    if ! grep -qE 'schema_version|runABStateRoundTrip|createFullState' "${ROOT_DIR}/tests/StateTests.cpp"; then
        fail "State round-trip: schema/A-B round-trip test missing"
        return 1
    fi

    # Noise profile round-trip is covered by Analysis tests (NoiseFingerprint).
    if [[ -f "${ROOT_DIR}/tests/AnalysisTests.cpp" ]] \
       && ! grep -qiE 'noise|Noise' "${ROOT_DIR}/tests/AnalysisTests.cpp"; then
        fail "State round-trip: noise-profile round-trip coverage missing"
        return 1
    fi

    # Alignment round-trip is covered by AlignmentTests.
    if [[ -f "${ROOT_DIR}/tests/AlignmentTests.cpp" ]] \
       && ! grep -qiE 'align|latency|delay' "${ROOT_DIR}/tests/AlignmentTests.cpp"; then
        fail "State round-trip: alignment round-trip coverage missing"
        return 1
    fi

    pass "State round-trip: schema/A-B/noise/alignment coverage present (binding: ctest Step 2)"
    return 0
}

# ---------------------------------------------------------------------------
# Step 8 — Multiband reconstruction flat
# ---------------------------------------------------------------------------

verify_multiband_reconstruction() {
    section "Step 8: Multiband reconstruction flat (unity)"

    # MultibandGateTests.cpp covers LR4 two/three-way complementary
    # reconstruction, click-free crossover movement, per-band smoothing
    # (CHUGGATE_SPEC.md §30.5). It is run by ctest in Step 2 (binding).
    if [[ ! -f "${ROOT_DIR}/tests/MultibandGateTests.cpp" ]]; then
        fail "Multiband reconstruction: tests/MultibandGateTests.cpp not found"
        return 1
    fi

    if ! grep -qiE 'complementary reconstruction' "${ROOT_DIR}/tests/MultibandGateTests.cpp"; then
        fail "Multiband reconstruction: reconstruction test missing"
        return 1
    fi

    pass "Multiband reconstruction: reconstruction suite present (binding: ctest Step 2)"
    return 0
}

# ---------------------------------------------------------------------------
# Orchestration
# ---------------------------------------------------------------------------

main() {
    printf '%sChugGate release validation%s — %s\n' "${BOLD}" "${RESET}" "${ROOT_DIR}"
    printf 'Preset: %s | Build: %s | Config: %s\n' "${PRESET}" "${BUILD_DIR}" "${CONFIG}"

    run_step "Build Release (VST3 + Standalone)" build_release || true
    run_step "ctest (ChugGateTests)"             run_ctest            || true
    run_step "RT-safety static scan"             rt_safety_scan       || true
    run_step "Zero-latency (lookahead off)"      verify_zero_latency  || true
    run_step "Lookahead reported latency"        verify_lookahead_latency || true
    run_step "Bypass click-free"                 verify_bypass_clickfree  || true
    run_step "State round-trip"                  verify_state_roundtrip   || true
    run_step "Multiband reconstruction flat"     verify_multiband_reconstruction || true

    section "Validation summary"
    printf 'Passed: %s%d%s   Failed: %s%d%s\n' \
        "${GREEN}" "${PASS_COUNT}" "${RESET}" \
        "${RED}"   "${FAIL_COUNT}" "${RESET}"

    if [[ ${FAIL_COUNT} -ne 0 ]]; then
        printf '%sFailed steps:%s\n' "${RED}" "${RESET}" >&2
        local s
        for s in "${FAILED_STEPS[@]}"; do
            printf '  - %s\n' "$s" >&2
        done
        printf '%sFAIL: ChugGate release validation did not pass.%s\n' "${RED}" "${RESET}" >&2
        exit 1
    fi

    printf '%sPASS: ChugGate release validation complete.%s\n' "${GREEN}" "${RESET}"
    exit 0
}

main "$@"
