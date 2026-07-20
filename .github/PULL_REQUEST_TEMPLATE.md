## What this changes

<!-- One or two sentences: the behavior before, the behavior after. -->

## How it was verified

<!-- Which test suites you ran (see CONTRIBUTING.md), and what you checked by
     hand in the running app for UI-facing changes. "Tests pass" alone is not
     enough for rendering changes - the suites render on CPU and cannot see
     GPU-path issues. -->

## Checklist

- [ ] `ctest` unit suite passes locally
- [ ] New code carries the MPL-2.0 header (`tools/apply-license-headers.sh --check`)
- [ ] No new dependency without an entry in `packaging/sbom.yaml`
- [ ] User-visible changes have a line in `CHANGELOG.md` under Unreleased
