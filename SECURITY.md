# Security

## Reporting a vulnerability

Please report security issues privately through
[GitHub's private vulnerability reporting](https://github.com/andrewczwu/fileops-cpp/security/advisories/new)
rather than a public issue. You will get an acknowledgement within a few days.

## What the project does to stay secure

- **Secrets never reach history.** A `gitleaks` pre-commit hook scans staged changes, CI scans the
  full history on every push and pull request, and GitHub push protection is enabled.
- **Supply chain.** Every third-party GitHub Action is pinned to a commit SHA and updated by
  Dependabot after a seven-day cooldown; so is the toolchain image's base. The three C++
  dependencies (CLI11, nlohmann_json, opentelemetry-cpp, GoogleTest) are pinned in `conanfile.py`, their resolved
  revisions are locked in `conan.lock`, and every CI build resolves with `--lockfile`, so the graph
  that was tested is the one that ships. Conan has no automated vulnerability audit comparable to
  `NuGetAudit`; the mitigation is that every package is pinned, resolved strictly from the lockfile and
  bumped deliberately (see CONTRIBUTING.md). Workflows run with `permissions: contents: read` and
  escalate per job only where needed.
- **Code-level defences.** Every Debug build in CI runs the whole test suite under AddressSanitizer
  and UndefinedBehaviorSanitizer with both GCC 14 and Clang 18; clang-tidy runs with every enabled
  check as an error; compiler warnings are errors. CodeQL (default setup) analyses the C++ code.
- **Provenance.** Release artefacts carry SLSA build provenance attestations; verify with
  `gh attestation verify fileops-cpp_<version>_amd64.deb --owner andrewczwu` and add
  `--signer-workflow andrewczwu/fileops-cpp/.github/workflows/release.yml`; without it, any workflow
  in the repository satisfies the check.
- **Small runtime surface.** The release binary is linked fully statically, so the package has no
  dependencies and nothing on the target system can be substituted into it at load time.
- **Repository rules.** A tag ruleset protects `v*` tags from deletion and movement, `main` accepts
  only pull requests with green required checks, and releases are immutable once published.
- **Workflow hygiene.** `actionlint` and `zizmor` run on every change to `.github/workflows`.
- **Least surprise at runtime.** The tool never overwrites an existing file without `--force`,
  never deletes directories, creates temporary files with the final permission bits, and never
  writes to stdout except for `--help` and `--version`.
