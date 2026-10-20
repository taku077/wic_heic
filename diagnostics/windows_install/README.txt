# WicHeic x64 experimental installation kit

This is a developmental build. Native WIC + MSI installation tests passed on an
isolated Windows Server 2022 CI machine. Windows 10 Photos UI has NOT been tested.

## Contents
- WicHeic-Repair-EXPERIMENTAL-x64.msi
- Install_Experimental_WIC.ps1
- Install_Experimental_WIC.cmd
- WIC_Preflight_ReadOnly.ps1

## Steps
1. Extract the entire ZIP to a normal LOCAL directory (do not run from inside ZIP).
2. Double-click Install_Experimental_WIC.cmd. It takes a read-only WIC registry
   snapshot and refuses to replace ANY existing WIC encoder or decoder.
3. When WIC_PREFLIGHT_CLEAR appears, Windows Installer runs (UAC may prompt).
   The preflight snapshot and MSI log remain in this same folder.
4. On an existing WIC installation, INSTALL_BLOCKED_EXISTING_WIC is intentional:
   do NOT uninstall the original codec merely to bypass this check. First secure
   a verified restorable original MSI and test restoration on a spare machine.
5. Keep this ZIP and the snapshot. Do not test against real photographs.

Uninstall after a fresh installation: Windows Settings > Apps > WicHeic Repair Test.
If the existing WIC was replaced using the MSI directly, uninstall alone may NOT
restore the previous version; reinstalling the exact original MSI may be required.

The MSI itself can also be opened directly, but this BYPASSES the safe-install
check. Never run direct installation on a machine with an existing codec without
a separate verified rollback route.

These tools do not touch the separate HEIC Manager or your photo library.
