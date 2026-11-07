# Windows USB storage inventory

`platform/windows/usb_inventory.cpp` is a read-only inventory backend. It is not
connected to the UI and does not bind, detach, dismount, export, or import a USB
device. Building the library does not inspect connected devices. The isolated
`usb-inventory-parsers` test uses synthetic byte buffers only.

The backend collects present disk interfaces using SetupAPI and obtains their
instance IDs locally. It queries the storage descriptor, physical disk number,
partition layout, USB parent ancestry, volume disk extents, Windows volume, and
currently enumerated paging files. A candidate must report USB bus transport and
removable media. USB disks reporting fixed media are intentionally excluded.

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
probe is absent, so **the current standalone inventory never makes a device
eligible for transfer**. A future usbipd integration must verify ownership for
the locally discovered instance ID. A remote ID or a UI checkbox cannot supply
this fact.

The local process retains each device generation across matching reinspections.
Changes to its descriptor, interface, physical disk number, partition layout,
volume set, or eligibility facts invalidate that generation, as does observing
the device absent. A future transfer implementation must bind the user's
selection to that revision, watch device replacement and removal, and revalidate
identities, disk/volume mappings, ownership, and protection before acquiring
exclusive locks. An unplug/replug entirely between snapshots can go unobserved:
generations alone do not prove that a device remained attached. Free space is not used as evidence that
a disk is safe to detach. A list of volumes is not evidence of exclusive access.

Remaining work includes authoritative ownership integration, device-change
monitoring, handle-bound reinspection, volume lock/dismount and rollback,
transport, remote eject/recovery, and explicit user-tested hardware validation.
The native enumeration path has not been run as part of this implementation.
No USB sharing capability should be enabled from these parser tests alone.

API references:

- [Disk layouts](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntdddisk/ni-ntdddisk-ioctl_disk_get_drive_layout_ex)
- [Partition information](https://learn.microsoft.com/en-us/windows/win32/api/winioctl/ns-winioctl-partition_information_ex)
- [Paging-file callback](https://learn.microsoft.com/en-us/windows/win32/api/psapi/nc-psapi-penum_page_file_callbackw)
