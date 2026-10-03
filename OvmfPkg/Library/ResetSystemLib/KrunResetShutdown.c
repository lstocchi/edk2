/** @file
  Reset shutdown implementation for the libkrun platform.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Base.h>

#include <IndustryStandard/Krun.h>
#include <Library/BaseLib.h>
#include <Library/IoLib.h>
#include <Library/ResetSystemLib.h>

/**
  Request ACPI S5 through libkrun's hardware-reduced sleep control register.
**/
VOID
EFIAPI
ResetShutdown (
  VOID
  )
{
  IoWrite8 (KRUN_ACPI_SLEEP_CONTROL_PORT, BIT5);
  CpuDeadLoop ();
}
