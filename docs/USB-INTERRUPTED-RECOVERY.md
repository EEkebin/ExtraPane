# Explicit interrupted USB return

The manual command below handles older interrupted records. Current sessions
first attempt normal receiver eject, then revoke their own exporter and verify
exclusive local ownership before restoring the exact source if needed. An
interrupted return is never represented as a clean eject.

## An unplugged source must not block uninstall

`ExtraPane.UsbRecovery.exe --release-absent <journal-id>` checks the protected
version 2 bound record, exact private binding and device instance, fresh device
absence, and absence of an exporter. It archives the original record under
`UsbRecoveryHistory` with `InterruptedAbsentRelease`, removes only that binding,
and retires the active record. It does not open a disk or change its attributes.
It cannot prove the absent medium's filesystem health or restore an offline bit
on hardware that is not connected.

Uninstall invokes `--prepare-uninstall` to reconcile eligible inactive records
before checking/removing the backend. Compatible in-place upgrades verify and
reuse the existing backend without changing bindings or recovery records.
Foreign, active or ambiguous ownership is still a reason to stop cleanup with
an actionable error. Do not delete these records manually.

## Manual interrupted return for an attached source

Version 2 `bound` journals cannot distinguish a failed startup from a device that
was imported and written to. Seeing no imported disk now does not prove that
earlier writes were flushed. Interrupted return restores access to the bytes
currently on the source medium; it cannot recover writes left in a receiver's
cache and does not claim a successful eject.

Before approving an interrupted return:

1. Stop USB sharing on both machines. Inspect the receiver's managed import ports
   and confirm that the old device is no longer attached. Do not restart sharing.
2. On the source, run the elevated helper with `--list`, then
   `--inspect <exact-journal-id>`. Inspection is read-only. Require
   `journalVersion: 2` and `matchesRecordedIdentity: true`; review the original
   disk number, device instance, stable fingerprint and current binding.
3. Review the specific device and acknowledge that previous receiver writes may
   not have been flushed. Do not infer this acknowledgment from a request to
   disconnect, reinstall or uninstall.
4. Only after that approval, run:

   ```powershell
   .\ExtraPane.UsbRecovery.exe --recover-interrupted <exact-journal-id> --acknowledge-unflushed-receiver
   ```

The helper accepts only an unchanged version 2 `bound` record whose original disk
was online. It reserves the exact source, checks the raw partition identity,
rejects active exporters and attached/forced/foreign bindings, adopts only the
recorded binding, removes that exact binding, and waits for an idle source with
the same physical identity. It restores only the original offline attribute;
read-only and unrelated attributes remain unchanged. It neither formats nor
repairs the filesystem.

Before any binding or disk change, the helper saves the complete original journal
and explicit acknowledgment in the protected machine registry under
`HKLM\SOFTWARE\ExtraPane\Components\UsbRecoveryHistory\<journal-id>.<owner>`.
`AuthorizedAtUtc` records the approval command's time; `Phase` is 1 for authorized,
2 for verified local binding release, and 3 for source restoration. The original
record is never rewritten as `ejected`. Only after source restoration and audit
persistence does the helper retire the active recovery journal. A failure retains
the journal and audit for inspection; do not delete either manually.

This command checks local exclusivity. Existing version 2 journals do not contain
a destination identity that could authenticate a new remote cleanup receipt.
Receiver shutdown/port inspection and acknowledgment therefore remain explicit
operator steps. Byte-identical cloned media in the same reader cannot be
distinguished by the reader identity and partition metadata.
