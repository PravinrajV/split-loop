# mkqnximage snippets

These files are applied under a disposable `mkqnximage` working directory
(see `scripts/build-image.sh`). Do not commit QNX-generated `output/`,
`local/misc_files/` SSH host keys, or license material.

- `ifs_files.smoke` — packs `sl` and `sl-baseline` into `/proc/boot`
- `post_start.smoke` — prints `SMOKE_HELLO_END` so the host driver knows boot finished
- `ifs_start.custom` / `post_start.custom` — empty placeholders kept for parity with the build used for acceptance
