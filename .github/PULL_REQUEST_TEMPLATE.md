## What

<!-- One or two sentences. Link the issue if there is one. -->

## Why

<!-- The motivation; what breaks or is missing without this. -->

## Checklist

- [ ] Title follows [Conventional Commits](https://www.conventionalcommits.org)
      (for example `feat:`, `fix:`, `ci:`, `build:`, `docs:`, `test:`, `refactor:`, `chore:`)
- [ ] Tests added or updated, including telemetry assertions where behaviour changed
- [ ] `make lint tidy test` passes locally (or `make docker-test` inside the CI image)
- [ ] Docs updated (README, man page, ADR) if user-visible behaviour changed
- [ ] No secrets or credentials in the diff (`gitleaks` pre-commit hook ran)
