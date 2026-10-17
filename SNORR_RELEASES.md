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

The inherited release workflow gates Docker, PPA, OBS, COPR, upstream website publishing and version-bump PRs to `meshtastic/firmware`. On `lfg-seth/firmware`, SNORR RAK tags run only a read-only download/manifest/SHA256 verification job. It does not rebuild firmware, upload binaries, inherit upstream secrets or write to other repositories. Release creation/upload remains an explicit publisher action.
