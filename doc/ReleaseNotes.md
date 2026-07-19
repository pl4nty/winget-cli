## New in v1.29

# New Feature: CCM (CMTrace-compatible) log file format

The new `logging.format` user setting controls the format used when writing log files. The default, `winget`, is the
existing WinGet log format. Setting it to `ccm` writes log entries in a
[CMTrace](https://learn.microsoft.com/mem/configmgr/core/support/cmtrace)-compatible format so that winget logs can be
viewed with the CMTrace and OneTrace support tools, which is useful when collecting winget logs alongside Configuration
Manager (CCM) logs.

## Bug Fixes

* None yet
