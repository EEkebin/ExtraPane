# Windows USB storage inventory

`platform/windows/usb_inventory.cpp` is the read-only storage inventory backend
used by the Windows USB resource list. Enumeration does not bind, detach,
dismount, export, or import a USB device. Building the library does not inspect
connected devices. The isolated
`usb-inventory-parsers` test uses synthetic byte buffers only.

The backend collects present disk interfaces using SetupAPI and obtains their
instance IDs locally. It retains both the storage-stack instance ID and the
physical USB ancestor ID; the latter is used for backend binding and ownership
checks. It queries the storage descriptor, physical disk number,
partition layout, USB parent ancestry, volume disk extents, Windows volume, and
currently enumerated paging files. A candidate must report USB bus transport and
removable media. USB disks reporting fixed media are intentionally excluded.
Durable recovery also requires the physical USB node's unique-ID capability and
a bounded, nonempty storage serial that is not all zeros/spaces. Devices without
those identifiers remain unavailable. These identifiers are supplied by device
firmware, not cryptographic identity: identical devices with cloned identifiers
cannot be distinguished from those identifiers alone.

Composite USB devices and devices with multiple children below their physical
USB node are excluded. An unreadable or unfamiliar parent tree is unresolved.
The partition policy excludes bootable MBR partitions and admits only ordinary
data partition types. For GPT it excludes EFI, recovery, MSR, dynamic-storage,
other non-data partitions, and platform-required data partitions. The current
Windows volume's disks and paging-file disks are independently excluded.
These conservative classifications can reject otherwise usable external disks.

Missing or malformed facts keep `inventory_complete` false. Failure to map any
enumerated volume also makes the snapshot incomplete, even if that volume later
turns out to be unrelated. Inventory may require additional rights to inspect
all facts; it does not elevate itself. The caller receives unresolved reasons
instead of an optimistic eligibility result.

USB export ownership needs a separate, trusted local backend probe. The default
probe is absent, so standalone inventory without a probe does not make a device
eligible for transfer. The integrated Windows path supplies the local USB/IP
ownership probe for the locally discovered instance ID. A remote ID or a UI
checkbox cannot supply this fact.

The local process retains each device generation across matching reinspections.
Changes to its descriptor, interface, physical disk number, partition layout,
volume set, or eligibility facts invalidate that generation, as does observing
the device absent. The transfer path binds the user's selection to that revision,
watches device replacement and removal, and revalidates
identities, disk/volume mappings, ownership, and protection before acquiring
exclusive locks. An unplug/replug entirely between snapshots can go unobserved:
generations alone do not prove that a device remained attached. Free space is not used as evidence that
a disk is safe to detach. A list of volumes is not evidence of exclusive access.

Ownership integration, handle-bound reinspection, volume locking, transport and
recovery now exist; see [USB.md](USB.md) for their implementation and limitations.
Physical storage tests have exposed return and cleanup failures, so compilation
and parser tests must not be presented as successful end-to-end storage validation.

Generic HID/audio inventory is being added separately in
`platform/windows/usb_device_inventory.cpp`. It must not set `removable_storage`
to bypass these storage rules. Availability requires a supported, complete device
classification and exact backend ownership checks. Generic implementation status
and remaining driver gates are tracked in the implementation checklist.

API references:

- [Disk layouts](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntdddisk/ni-ntdddisk-ioctl_disk_get_drive_layout_ex)
- [Partition information](https://learn.microsoft.com/en-us/windows/win32/api/winioctl/ns-winioctl-partition_information_ex)
- [Paging-file callback](https://learn.microsoft.com/en-us/windows/win32/api/psapi/nc-psapi-penum_page_file_callbackw)
