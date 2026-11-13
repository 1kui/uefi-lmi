##
#  Copyright (c) 2011 - 2022, ARM Limited. All rights reserved.
#  Copyright (c) 2014, Linaro Limited. All rights reserved.
#  Copyright (c) 2015 - 2020, Intel Corporation. All rights reserved.
#  Copyright (c) 2018, Bingxing Wang. All rights reserved.
#  Copyright (c) Microsoft Corporation.
#
#  SPDX-License-Identifier: BSD-2-Clause-Patent
##

################################################################################
#
# Defines Section - statements that will be processed to create a Makefile.
#
################################################################################
[Defines]
  PLATFORM_NAME                  = lmi
  PLATFORM_GUID                  = 28F1A3BF-193A-47E3-A7B9-5A435EAAB2EE
  PLATFORM_VERSION               = 0.1
  DSC_SPECIFICATION              = 0x00010005
  OUTPUT_DIRECTORY               = Build/lmiPkg
  SUPPORTED_ARCHITECTURES        = AARCH64
  BUILD_TARGETS                  = RELEASE|DEBUG
  SKUID_IDENTIFIER               = DEFAULT
  FLASH_DEFINITION               = lmiPkg/lmi.fdf
  USE_CUSTOM_DISPLAY_DRIVER      = 0

  #
  # 0 = SM8250
  # 1 = SM8250-AB
  # 2 = SM8250-AC
  #
  SOC_TYPE                       = 0

!include KonaPkg/KonaPkg.dsc.inc

[PcdsFixedAtBuild]
  #
  # DDR Memory
  #
  gArmTokenSpaceGuid.PcdSystemMemoryBase|0x80000000

  #
  # UEFI Stack
  #
  gArmPlatformTokenSpaceGuid.PcdCPUCoresStackBase|0x9FF90000
  gArmPlatformTokenSpaceGuid.PcdCPUCorePrimaryStackSize|0x40000

  #
  # SMBIOS
  #
  gSiliciumPkgTokenSpaceGuid.PcdSmbiosSystemManufacturer|"Xiaomi"
  gSiliciumPkgTokenSpaceGuid.PcdSmbiosSystemRetailModel|"lmi"
  gSiliciumPkgTokenSpaceGuid.PcdSmbiosSystemModel|"Redmi K30 Pro"
  gSiliciumPkgTokenSpaceGuid.PcdSmbiosSystemRetailSku|"lmi"
  gSiliciumPkgTokenSpaceGuid.PcdSmbiosSystemBoardModel|"lmi"

  #
  # Simple Frame Buffer
  #
  gSiliciumPkgTokenSpaceGuid.PcdFrameBufferWidth|1080
  gSiliciumPkgTokenSpaceGuid.PcdFrameBufferHeight|2400
  gSiliciumPkgTokenSpaceGuid.PcdFrameBufferColorDepth|32

  #
  # Platform PEI
  #
  gQcomPkgTokenSpaceGuid.PcdPlatformType|"LA"

[LibraryClasses]
  #
  # Memory Libraries
  #
  MemoryMapLib|lmiPkg/Library/MemoryMapLib/MemoryMapLib.inf

  #
  # QCOM Libraries
  #
  ConfigurationMapLib|lmiPkg/Library/ConfigurationMapLib/ConfigurationMapLib.inf

  #
  # UniXNU: libraries the shared drivers need. UniXNURamDiskDxe reads the Mu-Silicium
  # FDT handoff through FdtLib and publishes a block device through HobLib.
  #
  HobLib|MdePkg/Library/DxeHobLib/DxeHobLib.inf
  FdtLib|MdePkg/Library/BaseFdtLib/BaseFdtLib.inf
  UniXNULib|UniXNUPkg/Library/UniXNULib/UniXNULib.inf

#
# Windows (WOA) bring-up variant. The UniXNU menu and the XNU loader are not
# needed to boot Windows, and their one-second auto-boot hides the BDS front
# page and the bring-up output. They are replaced by GptDump, which prints the
# real GPT of every block device the firmware can see: that both proves UFSDxe
# enumerated the handset's LUN 0 and records the partition geometry this
# 128 GB unit actually has.
#
[Components]
  lmiPkg/Drivers/GptDump/GptDump.inf
