/** @file
  Enumerate libkrun's fixed virtio-mmio transport range.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <IndustryStandard/Krun.h>
#include <Guid/VirtioMmioTransport.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/DevicePathLib.h>
#include <Library/IoLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/VirtioMmioDeviceLib.h>
#include <Protocol/DevicePath.h>
#include <Protocol/DriverBinding.h>
#include <Library/UefiLib.h>

#define VIRTIO_MMIO_MAGIC         0x74726976
#define VIRTIO_MMIO_MAGIC_OFFSET  0x000

#pragma pack (1)
typedef struct {
  VENDOR_DEVICE_PATH          Vendor;
  UINT64                      PhysBase;
  EFI_DEVICE_PATH_PROTOCOL    End;
} KRUN_VIRTIO_DEVICE_PATH;
#pragma pack ()

STATIC EFI_HANDLE  mKrunVirtioHandles[KRUN_VIRTIO_MMIO_DEVICE_COUNT];
STATIC UINTN       mKrunVirtioHandleCount = 0;
STATIC BOOLEAN     mKrunVirtioConnected[KRUN_VIRTIO_MMIO_DEVICE_COUNT];

/**
  Retry controller binding whenever a DXE driver binding is installed.

  The BDS architecture protocol is installed before Virtio10Dxe has registered
  its driver binding, so using it as a notification closes the event too early
  and leaves the virtio-mmio controllers unbound.
**/
STATIC
VOID
EFIAPI
OnDriverBinding (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  UINTN    Index;
  EFI_STATUS  Status;
  BOOLEAN  AllConnected;

  AllConnected = TRUE;

  for (Index = 0; Index < mKrunVirtioHandleCount; Index++) {
    if ((mKrunVirtioHandles[Index] != NULL) && !mKrunVirtioConnected[Index]) {
      Status = gBS->ConnectController (mKrunVirtioHandles[Index], NULL, NULL, TRUE);
      if (EFI_ERROR (Status)) {
        DEBUG ((
          DEBUG_VERBOSE,
          "%a: ConnectController not ready for handle[%d]: %r\n",
          __func__,
          Index,
          Status
          ));
        AllConnected = FALSE;
      } else {
        mKrunVirtioConnected[Index] = TRUE;
      }
    }
  }

  if (AllConnected) {
    gBS->CloseEvent (Event);
  }
}

EFI_STATUS
EFIAPI
InitializeKrunVirtioMmioDxe (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  UINTN                    Index;
  UINTN                    Base;
  EFI_STATUS               Status;
  EFI_HANDLE               Handle;
  KRUN_VIRTIO_DEVICE_PATH  *DevicePath;
  VOID                     *Registration;

  for (Index = 0; Index < KRUN_VIRTIO_MMIO_DEVICE_COUNT; Index++) {
    Base = KRUN_VIRTIO_MMIO_BASE + Index * KRUN_VIRTIO_MMIO_STRIDE;
    if (MmioRead32 (Base + VIRTIO_MMIO_MAGIC_OFFSET) != VIRTIO_MMIO_MAGIC) {
      continue;
    }

    DevicePath = (KRUN_VIRTIO_DEVICE_PATH *)CreateDeviceNode (
                                              HARDWARE_DEVICE_PATH,
                                              HW_VENDOR_DP,
                                              sizeof (*DevicePath)
                                              );
    if (DevicePath == NULL) {
      return EFI_OUT_OF_RESOURCES;
    }

    CopyGuid (&DevicePath->Vendor.Guid, &gVirtioMmioTransportGuid);
    DevicePath->PhysBase = Base;
    SetDevicePathNodeLength (
      &DevicePath->Vendor,
      sizeof (*DevicePath) - sizeof (DevicePath->End)
      );
    SetDevicePathEndNode (&DevicePath->End);

    Handle = NULL;
    Status = gBS->InstallProtocolInterface (
                    &Handle,
                    &gEfiDevicePathProtocolGuid,
                    EFI_NATIVE_INTERFACE,
                    DevicePath
                    );
    if (!EFI_ERROR (Status)) {
      Status = VirtioMmioInstallDevice (Base, Handle);
    }

    if (!EFI_ERROR (Status)) {
      // Save the handle until Virtio10Dxe registers its driver binding.
      mKrunVirtioHandles[mKrunVirtioHandleCount++] = Handle;
      DEBUG ((DEBUG_INFO, "%a: Added VirtIO-MMIO device at 0x%Lx\n", __func__, Base));
    } else {
      if (Handle != NULL) {
        gBS->UninstallProtocolInterface (
               Handle,
               &gEfiDevicePathProtocolGuid,
               DevicePath
               );
      }

      DEBUG ((DEBUG_ERROR, "%a: failed to add transport at 0x%Lx: %r\n", __func__, Base, Status));
      FreePool (DevicePath);
    }
  }

  // Bind the devices once Virtio10Dxe has installed its driver binding.
  EfiCreateProtocolNotifyEvent (
    &gEfiDriverBindingProtocolGuid,
    TPL_CALLBACK,
    OnDriverBinding,
    NULL,
    &Registration
    );

  return EFI_SUCCESS;
}
