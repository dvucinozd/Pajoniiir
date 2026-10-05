# Controller Profile Update Procedure

Status: **current P4-only procedure, reconciled 2026-10-06**. Guarded overwrite
and reboot persistence passed with the FLX4 profile during M2 qualification.
Physical use of a non-FLX4 profile remains unqualified.

This procedure replaces `SD:/controllers/<id>/profile.s3bin` through the P4
Wi-Fi Remote, so the enclosed SD card does not need to be removed. The P4 does
not compile or accept `profile.json`.

## Prepare the binary

Compile and test the profile on the development machine:

```powershell
python tools/controller_profile/compile_profile.py `
  controllers/pioneer_ddj_flx4/profile.json `
  -o controllers/pioneer_ddj_flx4/profile.s3bin
```

The binary must be S3CP v2, v3 or v4, 32-16384 bytes, and contain a valid length and
CRC-32. A profile ID is its SD directory name and may contain only ASCII
letters, digits, `_` and `-`, with a maximum of 39 characters.

The compiler chooses the oldest representable binary version. v91 supports
v2/v3/v4; the bare historical M2.4 image supports v2 only. Before a firmware
rollback, recompile a profile representable by that runtime; changing the
version bytes does not convert it. DDJ-400 and other non-FLX4 profiles remain
hardware NOT RUN even if upload validation succeeds.

## Upload from the P4 web UI

1. On P4 Settings, enable **Wi-Fi Remote**.
2. Join `Pajoniiir` with password `Pajoniiir` and open
   `http://192.168.4.1/`.
3. In **CONTROLLER PROFILE**, enter the exact profile ID and select the compiled
   `.s3bin`.
4. For an existing ID, enable **Allow overwrite**. This is deliberately off by
   default.
5. Select **UPLOAD PROFILE** and confirm the dialog. Keep power connected until
   the success message appears.
6. The P4 validates and stores the file, rescans the registry and activates a
   matching profile in its local controller runtime. Check `/api/status`:
   `profile_state` should reach `active`, and `active_profile` should equal the
   uploaded ID only after the current USB binding epoch is revalidated.

For a v4 profile, initial SysEx is queued once by the activation worker before
the first LED snapshot. An enqueue success does not prove device-side acceptance;
failed or stale initialization cannot activate a profile.

## Direct API

```powershell
curl.exe -X POST "http://192.168.4.1/api/controller-profile" `
  -H "Content-Type: application/octet-stream" `
  -H "X-DDJ-Control: 1" `
  -H "X-DDJ-Profile-ID: pioneer_ddj_flx4" `
  -H "X-DDJ-Profile-Overwrite: 1" `
  --data-binary "@controllers/pioneer_ddj_flx4/profile.s3bin"

curl.exe "http://192.168.4.1/api/controller-profiles"
curl.exe "http://192.168.4.1/api/status"
```

Use overwrite `0` or omit the header for a new ID. Existing IDs then return
HTTP 409. Invalid IDs, sizes, headers or CRCs return HTTP 400. A receive timeout
returns 408; storage failures return 500.

## Power-loss and concurrency behavior

- The complete blob is validated in RAM before any SD write.
- Upload is written and synced to `.profile.s3bin.upload` in the target
  directory, then read back and validated.
- On overwrite, the current target is renamed to `.profile.s3bin.backup` before
  the validated upload becomes `profile.s3bin`.
- If final validation fails, the backup is restored. On a later scan, a missing
  target is restored from backup and an incomplete upload is removed. A valid
  completed target wins over stale temporary files.
- Install returns HTTP 409 while profile activation is active or queued. The
  existing file and active P4-local runtime remain unchanged.

The built-in FLX4 map remains the exact-VID/PID fallback. A successful HTTP
response means the SD install and P4 rescan succeeded; local activation must be
confirmed through `/api/status` or P4 logs.
