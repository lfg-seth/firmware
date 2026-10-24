# SNORR cached RAK prereleases

Use the assets on the exact [SNORR RAK4631 prerelease](https://github.com/lfg-seth/firmware/releases/tag/snorr-rak4631-2.7.26.0b65f2e-pre.1) for normal flashing. Download and check SHA256 before selecting a physical board's update method. No rebuild is needed to flash another matching board.

The cached image is **2.7.26.0b65f2e**, compiled from `0b65f2e8444ea2dd3566f61ba2614d6eb33d3fd2`. Its tag includes the later release-workflow/manifest/verifier changes; those do not change firmware/build inputs. `build-manifest.json` identifies the actual binary source. The verifier rejects a tag with additional firmware/build changes.

Assets: RAK4631 UF2, Nordic OTA ZIP, `build-manifest.json`, `SHA256SUMS`. These are the already-built, tested bytes, with no embedded project credentials. This is a prerelease with successful build/portable checks; it has not yet been flashed or verified on hardware. It includes AHT detection and passive neighbor reports, plus the inherited INA219 driver. Live neighbor/current portal ingestion remains separate work.

Publishing procedure: prepare and verify the four files locally, create a draft targeting the release-workflow commit, upload all four files, then publish as a prerelease with **latest disabled**. Do not replace released bytes in place. Make a new tagged prerelease for a new build. Routine flashing downloads the pinned assets; rebuilding is for firmware development or an explicitly selected new target/source.

```sh
python3 bin/test_verify_snorr_release.py
python3 bin/verify_snorr_release.py --tag snorr-rak4631-2.7.26.0b65f2e-pre.1 --asset-dir /path/to/staged-assets
python3 bin/verify_snorr_release.py --tag snorr-rak4631-2.7.26.0b65f2e-pre.1
```

## Two-stage CI publication

The fork-only `snorr_rak_release.yml` workflow runs on `snorr/develop` pushes
that change firmware/build inputs. First, it builds ordinary `rak4631`, runs the
sensor, neighbor and release checks, and uploads four exact files: UF2, Nordic
`-ota.zip`, `build-manifest.json` and `SHA256SUMS`. It does not publish a release
at this stage. The ordinary RAK build selects 180-second private-channel passive
neighbor reporting; device telemetry still needs its separately verified profile.

The build log prints the complete UTF-8/LF manifest between
`BEGIN_SNORR_BUILD_MANIFEST` and `END_SNORR_BUILD_MANIFEST`, followed by its byte
count and SHA256. The manifest records the binary source commit, version,
180-second SNNS setting and exact CI run/attempt/artifact pointer. It marks
hardware verification false; a successful build does not establish hardware or
field acceptance.

After reviewing that successful build, copy its exact manifest bytes to
`.github/releases/<tag>.json`. Push a separate commit that adds **only this one
new manifest**. That commit selects publication rather than another build. The
publisher verifies the prior run's repository, workflow, branch, source SHA,
attempt and success; downloads that exact artifact; and checks all four files
against the tracked manifest. The verifier also rejects intervening firmware or
build changes between the binary source and publication commit.

The publisher atomically creates a new tag at the **manifest commit**, then
creates a draft with the checked assets and publishes it as a prerelease with
latest disabled. The manifest's `source` remains the earlier **binary source
commit**. An existing tag or release fails closed; no tag update, released-asset
replacement or overwrite is supported. A partial publication requires inspection
before choosing a new release identity. Public downloads receive an explicit
post-publication source/manifest/checksum verification in the same workflow.

The inherited release workflow gates Docker, PPA, OBS, COPR, upstream website
publishing and version-bump PRs to `meshtastic/firmware`. On `lfg-seth/firmware`,
its SNORR release job only verifies downloads. Neither workflow deploys the
portal, flashes radios, inherits upstream secrets or writes to other repositories.
