/** @file
  Block-device / GPT probe with a self-verifying heartbeat.

  Every step writes to ConOut directly with plain strings (no Print formatting)
  so that a silent hang can be located from a photo of the screen.

  This module never returns: BDS resets the console mode after the dispatch
  finishes, which erases everything printed here.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
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
Out (
  IN CONST CHAR16  *Text
  )
{
  if ((gST != NULL) && (gST->ConOut != NULL)) {
    gST->ConOut->OutputString (gST->ConOut, (CHAR16 *)Text);
  }
}

/* Unsigned decimal, no leading zeros, written into a caller buffer. */
STATIC
CONST CHAR16 *
Num (
  IN OUT CHAR16  *Buffer,
  IN UINT64      Value
  )
{
  CHAR16  Tmp[24];
  UINTN   Len;
  UINTN   Index;

  Len = 0;
  if (Value == 0) {
    Tmp[Len++] = L'0';
  } else {
    while ((Value != 0) && (Len < 24)) {
      Tmp[Len++] = (CHAR16)(L'0' + (Value % 10));
      Value     /= 10;
    }
  }

  for (Index = 0; Index < Len; Index++) {
    Buffer[Index] = Tmp[Len - 1 - Index];
  }

  Buffer[Len] = L'\0';
  return Buffer;
}

STATIC
VOID
OutNum (  IN CONST CHAR16  *Prefix,
          IN UINT64        Value,
          IN CONST CHAR16  *Suffix )
{
  CHAR16  Buf[24];

  Out ((CHAR16 *)Prefix);
  Out ((CHAR16 *)Num (Buf, Value));
  Out ((CHAR16 *)Suffix);
}

STATIC
VOID
ScanOne (
  IN EFI_BLOCK_IO_PROTOCOL  *BlockIo,
  IN UINTN                  Index
  )
{
  EFI_STATUS  Status;
  GPT_HEADER  Header;
  GPT_ENTRY   *Entries;
  UINTN       EntryBytes;
  UINT32      Entry;
  CHAR16      Buf[24];

  OutNum (L"\r\nDEV#", Index, L" ");
  if ((BlockIo == NULL) || (BlockIo->Media == NULL)) {
    Out (L"no-media-object\r\n");
    return;
  }

  OutNum (L"blk=", BlockIo->Media->BlockSize, L" ");
  OutNum (L"last=", BlockIo->Media->LastBlock, L" ");
  OutNum (
    L"GiB=",
    DivU64x32 (
      MultU64x32 (BlockIo->Media->LastBlock + 1, BlockIo->Media->BlockSize),
      1024 * 1024 * 1024
      ),
    L" "
    );
  OutNum (L"present=", BlockIo->Media->MediaPresent ? 1 : 0, L"\r\n");

  ZeroMem (&Header, sizeof (Header));
  Status = BlockIo->ReadBlocks (BlockIo, BlockIo->Media->MediaId, 1, sizeof (Header), &Header);
  if (EFI_ERROR (Status)) {
    OutNum (L"  read1-fail=0x", (UINT64)Status, L"\r\n");
    return;
  }

  if (Header.Signature != GptSignature) {
    Out (L"  no-gpt-signature\r\n");
    return;
  }

  OutNum (L"  first_usable=", Header.FirstUsableLba, L" ");
  OutNum (L"last_usable=", Header.LastUsableLba, L"\r\n");
  OutNum (L"  entries=", Header.NumberOfPartitionEntries, L" ");
  OutNum (L"entry_size=", Header.SizeOfPartitionEntry, L"\r\n");

  if ((Header.SizeOfPartitionEntry < sizeof (GPT_ENTRY)) ||
      (Header.NumberOfPartitionEntries == 0) ||
      (Header.NumberOfPartitionEntries > 256))
  {
    Out (L"  bad-geometry\r\n");
    return;
  }

  EntryBytes = Header.SizeOfPartitionEntry * Header.NumberOfPartitionEntries;
  Entries    = AllocatePool (EntryBytes);
  if (Entries == NULL) {
    Out (L"  alloc-fail\r\n");
    return;
  }

  Status = BlockIo->ReadBlocks (BlockIo, BlockIo->Media->MediaId, Header.PartitionEntryLba, EntryBytes, Entries);
  if (EFI_ERROR (Status)) {
    OutNum (L"  read-entry-fail=0x", (UINT64)Status, L"\r\n");
    FreePool (Entries);
    return;
  }

  for (Entry = 0; Entry < Header.NumberOfPartitionEntries; Entry++) {
    GPT_ENTRY  *Item = (GPT_ENTRY *)((UINT8 *)Entries + Entry * Header.SizeOfPartitionEntry);
    BOOLEAN    Zero  = TRUE;
    UINTN      Byte;

    for (Byte = 0; Byte < sizeof (EFI_GUID); Byte++) {
      if (((UINT8 *)&Item->PartitionTypeGuid)[Byte] != 0) {
        Zero = FALSE;
        break;
      }
    }

    if (Zero) {
      continue;
    }

    /* Name is UTF-16LE, NUL padded; keep it short so lines stay on screen. */
    Buf[0] = L'\0';
    CopyMem (Buf, Item->PartitionName, 22);
    Buf[11] = L'\0';

    Out (L"  ");
    Out (Buf);
    OutNum (L" ", Item->StartingLba, L"..");
    OutNum (L"", Item->EndingLba, L"\r\n");
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
  EFI_STATUS  Status;
  EFI_HANDLE  *Handles;
  UINTN       Count;
  UINTN       Index;
  UINTN       Attempt;

  Out (L"\r\nGPTDUMP-A: entry point reached\r\n");
  Out (L"GPTDUMP-A1: stall 1s test start\r\n");
  gBS->Stall (1000 * 1000);
  Out (L"GPTDUMP-A2: stall ok\r\n");

  Handles = NULL;
  Count   = 0;

  for (Attempt = 0; Attempt < 30; Attempt++) {
    Status = gBS->LocateHandleBuffer (ByProtocol, &gEfiBlockIoProtocolGuid, NULL, &Count, &Handles);
    if (!EFI_ERROR (Status) && (Count > 0)) {
      break;
    }

    if (Handles != NULL) {
      FreePool (Handles);
      Handles = NULL;
    }

    Out (L".");
    gBS->Stall (2 * 1000 * 1000);
  }

  Out (L"\r\nGPTDUMP-B: loop finished\r\n");

  if ((Handles == NULL) || (Count == 0)) {
    Out (L"GPTDUMP-C: NO BLOCK DEVICES\r\n");
  } else {
    OutNum (L"GPTDUMP-C: devices=", Count, L"\r\n");
    for (Index = 0; Index < Count; Index++) {
      EFI_BLOCK_IO_PROTOCOL  *BlockIo;

      Status = gBS->HandleProtocol (Handles[Index], &gEfiBlockIoProtocolGuid, (VOID **)&BlockIo);
      if (!EFI_ERROR (Status) && (BlockIo != NULL)) {
        ScanOne (BlockIo, Index);
      }
    }

    FreePool (Handles);
  }

  Out (L"GPTDUMP-Z: holding screen forever\r\n");

  for ( ; ; ) {
    gBS->Stall (3 * 1000 * 1000);
  }
}
