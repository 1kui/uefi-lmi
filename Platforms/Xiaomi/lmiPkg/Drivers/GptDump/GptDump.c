/** @file
  Print the GPT of every block device the firmware can see, then hold the
  screen so it can be photographed.

  Bring-up aid for reading the handset's real partition geometry (the LS-1
  build has no adb, no root and fastboot does not expose partition offsets).

  This variant deliberately never returns: BDS changes the console mode right
  after the driver dispatch finishes, which wipes everything printed above.
  Staying here forever keeps the listing on screen until the phone is reset.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Protocol/BlockIo.h>

#pragma pack(1)
typedef struct {
  UINT64    Signature;
  UINT32    Revision;
  UINT32    HeaderSize;
  UINT32    HeaderCrc;
  UINT32    Reserved;
  UINT64    MyLba;
  UINT64    AlternateLba;
  UINT64    FirstUsableLba;
  UINT64    LastUsableLba;
  EFI_GUID  DiskGuid;
  UINT64    PartitionEntryLba;
  UINT32    NumberOfPartitionEntries;
  UINT32    SizeOfPartitionEntry;
  UINT32    PartitionEntryArrayCrc;
} GPT_HEADER;

typedef struct {
  EFI_GUID  PartitionTypeGuid;
  EFI_GUID  UniquePartitionGuid;
  UINT64    StartingLba;
  UINT64    EndingLba;
  UINT64    Attributes;
  CHAR16    PartitionName[36];
} GPT_ENTRY;
#pragma pack()

STATIC CONST UINT64  GptSignature = 0x5452415020494645ULL;   /* "EFI PART" */

STATIC
VOID
PrintOneDevice (
  IN EFI_BLOCK_IO_PROTOCOL  *BlockIo,
  IN UINTN                  Index
  )
{
  EFI_STATUS  Status;
  GPT_HEADER  Header;
  GPT_ENTRY   *Entries;
  UINTN       EntryBytes;
  UINT32      Entry;
  UINTN       MediaId;
  BOOLEAN     HasGpt;

  if ((BlockIo->Media == NULL) || !BlockIo->Media->MediaPresent) {
    Print (L"[%u] no media\r\n", (UINT32)Index);
    return;
  }

  Print (
    L"[%u] block=%u last=%Lu  size=%Lu GiB\r\n",
    (UINT32)Index,
    BlockIo->Media->BlockSize,
    BlockIo->Media->LastBlock,
    DivU64x32 (
      MultU64x32 (BlockIo->Media->LastBlock + 1, BlockIo->Media->BlockSize),
      1024 * 1024 * 1024
      )
    );

  MediaId = BlockIo->Media->MediaId;
  HasGpt  = FALSE;
  ZeroMem (&Header, sizeof (Header));

  Status = BlockIo->ReadBlocks (BlockIo, MediaId, 1, sizeof (Header), &Header);
  if (!EFI_ERROR (Status) && (Header.Signature == GptSignature)) {
    HasGpt = TRUE;
  }

  if (!HasGpt) {
    Print (L"    (no GPT at LBA 1)\r\n");
    return;
  }

  Print (
    L"    first_usable=%Lu last_usable=%Lu entries@%Lu n=%u size=%u\r\n",
    Header.FirstUsableLba,
    Header.LastUsableLba,
    Header.PartitionEntryLba,
    Header.NumberOfPartitionEntries,
    Header.SizeOfPartitionEntry
    );

  if ((Header.SizeOfPartitionEntry < sizeof (GPT_ENTRY)) ||
      (Header.NumberOfPartitionEntries == 0) ||
      (Header.NumberOfPartitionEntries > 256))
  {
    return;
  }

  EntryBytes = Header.SizeOfPartitionEntry * Header.NumberOfPartitionEntries;
  Entries    = AllocatePool (EntryBytes);
  if (Entries == NULL) {
    return;
  }

  Status = BlockIo->ReadBlocks (BlockIo, MediaId, Header.PartitionEntryLba, EntryBytes, Entries);
  if (EFI_ERROR (Status)) {
    Print (L"    entry read failed 0x%x\r\n", Status);
    FreePool (Entries);
    return;
  }

  for (Entry = 0; Entry < Header.NumberOfPartitionEntries; Entry++) {
    GPT_ENTRY  *Item = (GPT_ENTRY *)((UINT8 *)Entries + Entry * Header.SizeOfPartitionEntry);
    BOOLEAN    Zero  = TRUE;
    UINTN      Byte;
    CHAR16     Name[37];

    for (Byte = 0; Byte < sizeof (EFI_GUID); Byte++) {
      if (((UINT8 *)&Item->PartitionTypeGuid)[Byte] != 0) {
        Zero = FALSE;
        break;
      }
    }

    if (Zero) {
      continue;
    }

    CopyMem (Name, Item->PartitionName, sizeof (Item->PartitionName));
    Name[36] = L'\0';

    Print (
      L"  %2u %s  %Lu..%Lu (%Lu MiB)\r\n",
      (UINT32)(Entry + 1),
      Name,
      Item->StartingLba,
      Item->EndingLba,
      DivU64x32 (
        MultU64x32 (Item->EndingLba - Item->StartingLba + 1, BlockIo->Media->BlockSize),
        1024 * 1024
        )
      );
  }

  FreePool (Entries);
}

EFI_STATUS
EFIAPI
GptDumpEntry (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS             Status;
  EFI_HANDLE             *Handles;
  UINTN                  Count;
  UINTN                  Index;
  UINTN                  Attempt;

  Handles = NULL;
  Count   = 0;

  /* UFSDxe publishes its block device well after the APRIORI dispatch, so wait
     for it rather than relying on a BlockIO depex. */
  for (Attempt = 0; Attempt < 120; Attempt++) {
    Status = gBS->LocateHandleBuffer (
                    ByProtocol,
                    &gEfiBlockIoProtocolGuid,
                    NULL,
                    &Count,
                    &Handles
                    );
    if (!EFI_ERROR (Status) && (Count > 0)) {
      break;
    }

    if (Handles != NULL) {
      FreePool (Handles);
      Handles = NULL;
    }

    gBS->Stall (500 * 1000);
  }

  Print (L"\r\n===== GPTDUMP =====\r\n");

  if ((Handles == NULL) || (Count == 0)) {
    Print (L"NO BLOCK DEVICES after 60s wait\r\n");
  } else {
    Print (L"%u block device(s)\r\n", (UINT32)Count);
    for (Index = 0; Index < Count; Index++) {
      EFI_BLOCK_IO_PROTOCOL  *BlockIo;

      Status = gBS->HandleProtocol (Handles[Index], &gEfiBlockIoProtocolGuid, (VOID **)&BlockIo);
      if (!EFI_ERROR (Status) && (BlockIo != NULL)) {
        PrintOneDevice (BlockIo, Index);
      }
    }

    FreePool (Handles);
  }

  Print (L"===== END. screen held for 15 min =====\r\n");

  /* Never return: BDS would reset the console mode and erase all of the above. */
  for ( ; ; ) {
    gBS->Stall (5 * 1000 * 1000);
  }
}
