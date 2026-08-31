# Deployment as a BOINC anonymous application

The end goal is to run the custom `dnetc` client (with the Hyunjin core) as a
BOINC **anonymous application** under the **Moo! Wrapper** at `moowrap.net`.

## The wrapper relationship

Moo! Wrapper launches a launcher with a `job.xml` control file. The wrapper
itself does BOINC → wrapper communication and delegates real number-crunching
to the `dnetc` client. Each work unit is staged into the slot directory as a
per-workunit **binary buffer file**; the `dnetc` client reads/writes it through
its standard offline buffer mode.

## The dnetc.ini contract

`packaging/dnetc.ini` is the client config. The critical keys:

```
[buffers]
buffer-file-basename = in       -> reads in.r72
output-file-basename = out      -> writes out.r72
checkpoint-filename  = chkpoint

[rc5-72]
core = 5                        -> select the Hyunjin core (appended index)

[display]  progress-indicator = off
[misc]     run-work-limit = -1   (process the whole work unit)
```

> **IMPORTANT (deployment):** the buffer file the client opens is
> `<buffer-file-basename>.r72`, i.e. `in.r72`. This name must match the
> logical `<open_name>` that Moo's work-unit template stages into the slot
> directory. If your Moo app supplies the work under a different name, adjust
> `buffer-file-basename` / `output-file-basename` in `dnetc.ini` to match.

## Files in packaging/

- `app_info.xml` — BOINC anonymous-app descriptor. Includes `<file_ref>`s for
  the client binary, `dnetc.ini` and `job.xml` with `<open_name>` +
  `<copy_file/>` so BOINC stages our ini (with `core = 5`) and job control
  instead of Moo's stock ones.
- `app_config.xml` — optional resource guard.
- `job.xml` — Moo! Wrapper control file; runs
  `dnetc_hyunjin_1.0_windows_x86_64.exe -ini dnetc.ini -runoffline -multiok=1`.
- `dnetc.ini` — client config (offline buffer mode, `core = 5`).

## BOINC setup (offline / anonymous platform)

1. Copy the files (renaming the binary/ini to the names referenced by
   `app_info.xml` and `job.xml`) into `<boinc>/projects/moowrap.net/`.
2. Enable anonymous computing: add to `cc_config.xml`

   ```xml
   <options>
     <use_anonymous_platform>1</use_anonymous_platform>
   </options>
   ```
3. Restart BOINC.

## Windows distribution

`dist/windows-x86_64/` ships the ready-to-run binary plus its `dnetc.ini`,
`job.xml`, `app_info.xml` and `app_config.xml`. The binary's RC5-72 core (#5)
passes `32/32` self-tests.
