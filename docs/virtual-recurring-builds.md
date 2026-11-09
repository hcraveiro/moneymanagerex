# Virtual recurring transaction development builds

The `Virtual Recurring - Desktop CI` workflow runs on pushes to the
`feature/virtual-recurring-transactions` branch and pull requests to
`master`. It builds a Linux `.deb` using wxWidgets 3.3.4 and uploads the
package as a **14-day GitHub Actions artifact**.

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
- Windows AppVeyor configuration exists upstream but is not managed by
  this Linux GitHub Actions workflow. A Windows build will require its
  own verified runner/toolchain.
- A `workflow_dispatch` workflow must be present on the default branch
  before GitHub exposes manual dispatch in the Actions UI. Branch pushes
  run this workflow even while it is only present on the feature branch.
- The fork must have GitHub Actions enabled in repository Settings.
