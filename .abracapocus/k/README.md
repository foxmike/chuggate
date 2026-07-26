# k

## Goal

Validation scripts, release documentation, and verification.

Validation script: build Release, ctest, RT-safety checks, zero-latency verification, all lookahead values report correct samples, bypass click-free, state round-trip, multiband reconstruction flat.

Release docs: verification checklist per spec section 31. Host testing that cannot run in CI documented honestly as requiring manual validation.

Preset content refinement.

## Notes

- Async: Wave 1: k.1 (validation) and k.2 (preset content) independent. Wave 2: k.3 (release docs) depends on k.1.
- Risk: Low risk — no DSP changes.
