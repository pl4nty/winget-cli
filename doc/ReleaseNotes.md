## New in v1.30

### `--ignore-unavailable` flag for `install`

Added a new `--ignore-unavailable` flag to the `install` command. When installing multiple packages, this flag allows the operation to continue with the remaining packages instead of failing entirely when one or more packages are not found in the configured sources. This brings the same behavior previously available with `import --ignore-unavailable` to direct multi-package installs.

### Manifest schema 1.30: `IconSha256` is required

`Icons[].IconSha256` is now required (and no longer nullable) in the 1.30 locale, default locale and singleton manifest schemas. Earlier manifest versions are unchanged.

## Bug Fixes

* Updated NUnit to v4
* Fixed a crash (`0x8000ffff`) when using `--disable-interactivity` with the Resume experimental feature enabled during install operations.
