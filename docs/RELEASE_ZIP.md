# Build output and release ZIP

Every application build creates and validates `dist/<TITLE_ID>/` and archives
it as `dist/<TITLE_ID>.zip`. The folder and its ZIP are the only outputs; on
Windows, `./build.ps1` runs the same build through WSL.

Tagged GitHub Releases
attach a ZIP of the validated directory-style application and its
`SHA256SUMS`, and nothing else.

```bash
make app
```

The release workflow uses Python's standard-library `zipfile` module to archive
`dist/<TITLE_ID>/` as `<TITLE_ID>.zip`. The ZIP is a distribution convenience,
not another console filesystem format; extract it before directory deployment.

## Publishing a release

A release is made by pushing the version tag: the workflow builds, attests and publishes the
ZIP and `SHA256SUMS`. Release files are not attached by hand, so that every published file
comes from a run and has an attestation. The publish step handles three cases:

- **No release for the tag:** it creates a pre-release with the two files and generated notes.
- **A release without a ZIP** (notes written in advance, or a draft): it adds the two files
  and leaves the title and notes alone.
- **A release that already has a ZIP:** nothing is replaced or deleted, because the catalog
  at prosperostore.net records each release ZIP's checksum and the store refuses a file that
  differs. The run ends green with a warning that those files were not published by it and
  may have no attestation.

A release ZIP built by the workflow can be checked with
`gh attestation verify PPSA99000.zip -R blackbearreloaded/ProsperoStore` (GitHub CLI).
