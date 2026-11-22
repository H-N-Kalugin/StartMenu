# Code signing policy

Free code signing for StartMenu is provided by [SignPath.io](https://about.signpath.io/), certificate by [SignPath Foundation](https://signpath.org/).

## What is signed

Only release builds of `StartMenu.exe` and the installer `StartMenu-<version>.exe`, built by the public GitHub Actions workflow
[`.github/workflows/build.yml`](../.github/workflows/build.yml) from the source code in this repository.
Nothing built on a personal computer is signed.

## Team roles

| Role | Members |
|---|---|
| Committers and reviewers | [NKF-XXX](https://github.com/NKF-XXX) |
| Approvers (approve each signing request) | [NKF-XXX](https://github.com/NKF-XXX) |

## Privacy

This program will not transfer any information to other networked systems unless specifically requested by the user or the person installing or operating it, with one exception:
once a day it asks the GitHub API (`api.github.com`) for the number of the latest StartMenu release, so it can offer an update.
No personal data is sent. The check can be turned off in **Settings → Backups and updates**.

Website icons for tiles that link to websites are downloaded from those websites only when the user pins such a link.
