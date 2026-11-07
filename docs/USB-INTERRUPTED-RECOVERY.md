# Explicit interrupted USB return

This is an operator-only recovery procedure, not an ordinary disconnect action.
The app and installer never invoke it. Use normal USB return whenever the
receiving machine can still eject the device.

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
