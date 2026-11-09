# Product naming

The visible product name is **ExtraPane** on Windows, Linux, Android and macOS.
Windows native display receiver titles (including the paused-input title) and its
software-device display name are defined in
`platform/windows/include/dm/branding.hpp`. The Qt, Android and AppKit frontends
already use ExtraPane. Driver endpoint and installation labels also use ExtraPane.

A future rename must distinguish visible names from upgrade and protocol identities.
The following older names intentionally remain for compatibility:

- `DMIndirectDisplay` package filenames, hardware IDs and the `DM Project` INF
  provider identify packages already owned by the driver manager. Changing these
  requires an explicit driver migration and signed replacement package.
- `DM` registry paths, credential target prefixes, device enumerators, window
  class/property names and diagnostic status tokens remain stable for existing
  settings, tests and IPC consumers.
- SideScreen installer AppIds, mutexes, legacy filenames and uninstall lookup paths
  allow upgrades to recognize and clean up previous installations.
- C++ namespaces, build target names and on-wire identifiers are implementation
  identifiers, not product labels.

Do not globally replace these identities when updating visible branding. Windows
may continue to show a historical driver provider string for installed packages;
that is preserved ownership metadata, rather than a second application.
