# Virtual recurring transaction development builds

The `Virtual Recurring - Desktop CI` workflow runs on pushes to the
`feature/virtual-recurring-transactions` branch and pull requests to
`master`. It builds a Linux `.deb` using wxWidgets 3.3.4 and Windows
x64 portable/installer packages. CI artifacts are retained for 14 days.

The workflow can also create a GitHub **prerelease** when an explicit
`virtual-v*` tag is pushed. This is not triggered by ordinary commits.
For example, when a beta is ready, tag the desired commit as
`virtual-v0.1.0-beta.1` and push that tag. This builds the tagged commit
and publishes its `.deb` asset as a prerelease, not a stable release.
No tag or release is created by the workflow itself.

Notes:
- Do not use a financial database without backing it up first. Version-22
  schema files may be incompatible with stock version-21 applications.
- The desktop database submodule is pinned to the companion
  `hcraveiro/database` fork, which contains the version-22 migration.
- The Windows packaging workflow verifies that `mmex.exe` and the
  `libcurl.dll` runtime are included in the portable ZIP.
- A `workflow_dispatch` workflow must be present on the default branch
  before GitHub exposes manual dispatch in the Actions UI. Branch pushes
  run this workflow even while it is only present on the feature branch.
- The fork must have GitHub Actions enabled in repository Settings.

## Upstream synchronization — 2026-11-10

Merged the official `moneymanagerex/moneymanagerex` `master` commit
`346f658e5c17192846798b30b45bd1f1e66a9781` into the Virtual
Recurring feature branch. The merge keeps both parents and includes these
three upstream commits, in chronological commit order:

- `dff5ce1e9` — FIX #8583, #8605; change default-currency inserts to
  `INSERT OR IGNORE` in `CurrencyTable.cpp`
- `4263938b3` — JournalList: allow button shortcuts
- `346f658e5` — `sqlite2cpp` generator and README corrections

All four upstream-modified files were identical to the original feature
branch merge base, so the merge introduced no overlapping changes.
The Virtual recurrence code, v22 database submodule, and feature-specific
tests were preserved.

The previous feature branch head is retained at
`backup/virtual-before-upstream-20261110` for traceability.
