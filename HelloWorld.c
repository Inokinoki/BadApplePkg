#include <Uefi.h>
#include <Library/UefiApplicationEntryPoint.h>
#include <Library/UefiLib.h>

// Provide gBS
#include <Library/UefiBootServicesTableLib.h>

// Protocols
#include <Protocol/GraphicsOutput.h>
#include <Protocol/Shell.h>

#include <Library/ShellLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/BmpSupportLib.h>  
#include <IndustryStandard/Bmp.h>

#include <Library/PrintLib.h>

#define VIDEO_WIDTH   480
#define VIDEO_HEIGHT  360

EFI_STATUS 
LoadBMP (
	IN CHAR16  *FileName,
	OUT EFI_GRAPHICS_OUTPUT_BLT_PIXEL **Buffer,
	OUT UINTN *Height,
	OUT UINTN *Width,
	OUT UINTN *Size
)
{
	EFI_STATUS Status = EFI_SUCCESS;
    SHELL_FILE_HANDLE FileHandle;
    CHAR16  *FullFileName;

	if (FileName == NULL) {	
        return EFI_UNSUPPORTED;
    }
	
    FullFileName = ShellFindFilePath(FileName);
    if (FullFileName == NULL) {
        Status = EFI_NOT_FOUND;
        goto Cleanup;
    }
    Status = ShellOpenFileByName(FullFileName, &FileHandle, EFI_FILE_MODE_READ, 0);
    if (EFI_ERROR(Status)) {
        goto Cleanup;
    }

    ShellSetFilePosition(FileHandle, 0);
	EFI_FILE_INFO *FileInfo = ShellGetFileInfo(FileHandle);

	void *File = (void *) 1; //To make BmpSupportLib happy
	//UINTN Size = FileInfo->FileSize;

	ShellReadFile(FileHandle, &FileInfo->FileSize, File);
	if (EFI_ERROR(Status)) {
        goto Cleanup;
    }

	// *Buffer = 0;
	*Height = 0;
	*Width = 0;

	Status = TranslateBmpToGopBlt(File, FileInfo->FileSize, Buffer, Size, Height, Width);
	if (EFI_ERROR(Status)) {
		goto Cleanup;
	}
  Cleanup:
  	ShellCloseFile(&FileHandle);
    if (FullFileName != NULL) {
       FreePool(FullFileName);
    }

	return Status;
}

/**
  Entry point for the game.

  @param[in] ImageHandle    The firmware allocated handle for the EFI image.  
  @param[in] SystemTable    A pointer to the EFI System Table.
  
  @retval EFI_SUCCESS       The entry point is executed successfully.
  @retval other             Some error occurs when executing this entry point.

**/
EFI_STATUS
EFIAPI
UefiMain (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{ 
  EFI_STATUS Status = EFI_SUCCESS;

  Print(L"Hello World!\n");
  EFI_GRAPHICS_OUTPUT_PROTOCOL *graphicsProtocol = NULL;
  Status = gBS->LocateProtocol(&gEfiGraphicsOutputProtocolGuid, NULL, (void**)&graphicsProtocol);
  if (EFI_SUCCESS != Status) {
    Print(L"Error locating graphics protocol\n");
    return Status;
  }
  Print(L"Graphics protocol located\n");
  // graphicsProtocol->QueryMode(graphicsProtocol, );
  UINTN SizeOfInfo = 0;
  EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *Info;

  UINT32 screenWidth = VIDEO_WIDTH, screenHeight = VIDEO_HEIGHT;
  for(UINTN i = 0; i < graphicsProtocol->Mode->MaxMode; i++)
  {
    Status = graphicsProtocol->QueryMode(
        graphicsProtocol,
        i,
        &SizeOfInfo,
        &Info
    );
    if(EFI_ERROR(Status))
    {
      Print(L"Failed to Querymode.\n");
      return Status;
    }
    if (Info->HorizontalResolution >= 480 && Info->VerticalResolution >= 360) {
      // Buffer the screen scale
      screenWidth = Info->HorizontalResolution;
      screenHeight = Info->VerticalResolution;
      Print(L"Screen info %dx%d.\n", screenWidth, screenHeight);
      Status = graphicsProtocol->SetMode(graphicsProtocol, i);
      break;
    }
  }

  // Read files
  SHELL_FILE_HANDLE BABinFileHandle;
  CHAR16  *FullFileName;
  FullFileName = ShellFindFilePath(L"BA.bin");
  if (FullFileName == NULL) {
    Print(L"Failed to find BadApple bin compression file.\n");
    goto Cleanup;
  }
  Status = ShellOpenFileByName(FullFileName, &BABinFileHandle, EFI_FILE_MODE_READ, 0);
  if (EFI_ERROR(Status)) {
    Print(L"Failed to load BadApple bin compression file.\n");
    goto Cleanup;
  }

  EFI_STRING StringPtr = NULL;
  UINTN Length = StrLen (L"BadAppleIFrames\\image-000.bmp") + 1;
  StringPtr = AllocateZeroPool (Length * sizeof (CHAR16));
  UINTN SpriteSheetSize;
  UINTN SpriteSheetHeight;
  UINTN SpriteSheetWidth;
  EFI_GRAPHICS_OUTPUT_BLT_PIXEL *intraFrames[52];
  for (int i = 0; i < 52; i+=1) {
    intraFrames[i] = NULL;
    UnicodeSPrint (StringPtr, Length * sizeof (CHAR16), L"BadAppleIFrames\\image-%03d.bmp", i + 1);
    // Print(L"Loading key frame %d.\n", i + 1);
    Status = LoadBMP(StringPtr, &intraFrames[i], &SpriteSheetHeight, &SpriteSheetWidth, &SpriteSheetSize);
    if (EFI_ERROR(Status)) {
      Print(L"Failed to load key frame.\n");
      goto Cleanup;
    }
    // Print(L"Key frame %d loaded.\n", i + 1);
  }

  // Setup tick loop
  // BadAppleFrames
  Print(L"Setup tick loop.\n");
  EFI_EVENT TickEvent;
  UINTN eventId;
  gBS->CreateEvent(EVT_TIMER, 0, NULL, NULL, &TickEvent);
	gBS->SetTimer(TickEvent, TimerPeriodic, EFI_TIMER_PERIOD_MILLISECONDS(33));

  EFI_GRAPHICS_OUTPUT_BLT_PIXEL *DrawBuffer;
  UINTN ReadLength = 4;
  UINTN BeginX = (screenWidth - VIDEO_WIDTH) / 2;
  UINTN BeginY = (screenHeight - VIDEO_HEIGHT) / 2;
  DrawBuffer = NULL; // = AllocateCopyPool(VIDEO_WIDTH * BMP_TILE_LENGTH * VIDEO_HEIGHT * BMP_TILE_LENGTH, BackgroundBuffer);
  int intraFrameIndex = 0;
  unsigned char Buffer[4];
  int NeedRefresh = 1;
  for (int i = 0; i < 6573; i+=1) {
    NeedRefresh = 1;
    UINTN DecodedInt32;
    ReadLength = 4;
    Status = ShellReadFile(BABinFileHandle, &ReadLength, (void *)Buffer);
    if (ReadLength != 4) {
      // FIXME: How can this happens
    }
    // Read as little endian
    DecodedInt32 = ((Buffer[3] << 24) | (Buffer[2] << 16) | (Buffer[1] << 8) | Buffer[0]);
    Print(L"Read %d bytes: 0x%04X.\n", ReadLength, DecodedInt32);
    if (EFI_ERROR(Status)) {
      Print(L"Read file failed at %d.\n", i);
      goto Cleanup;
    }
    if ((DecodedInt32 & 0x80000000) != 0) {
      if ((DecodedInt32 & 0x7FFF7FFF) == 0x7FFF7FFF) {
        // Keep frame and do not refresh
        NeedRefresh = 0;
        Print(L"Not need refresh at %d.\n", i);
      } else {
        Print(L"Patching at %d.\n", i);
        goto Cleanup;
        do {
          // UINTN PatchX = ((DecodedInt32 & 0x7FFF0000) >> 16), PatchY = (DecodedInt32 & 0x7FFF);
          // TODO: Begin patch until 0xFFFFFFFF

          // Read next
          ReadLength = 4;
          Status = ShellReadFile(BABinFileHandle, &ReadLength, (void *)Buffer);
          // Read as little endian
          DecodedInt32 = ((Buffer[3] << 24) | (Buffer[2] << 16) | (Buffer[1] << 8) | Buffer[0]);
          Print(L"Read %d bytes: 0x%04X.\n", ReadLength, DecodedInt32);
        } while ((DecodedInt32 & 0x7FFF7FFF) != 0x7FFF7FFF);
      }
    } else {
      // Read one intra-frame
      Print(L"Print key frame %d at %d.\n", intraFrameIndex, i);
      DrawBuffer = intraFrames[intraFrameIndex++];
    }

    // Wait for tick
    gBS->WaitForEvent(1, &TickEvent, &eventId);
    if (NeedRefresh)
      graphicsProtocol->Blt(graphicsProtocol, DrawBuffer, EfiBltBufferToVideo, 0, 0, BeginX, BeginY, VIDEO_WIDTH, VIDEO_HEIGHT, 0);
  }
Cleanup:
  FreePool(StringPtr);
  if (DrawBuffer != NULL) {
    FreePool(DrawBuffer);
  }
  ShellCloseFile(&BABinFileHandle);
  if (FullFileName != NULL) {
    FreePool(FullFileName);
  }
  gBS->CloseEvent(TickEvent);
  gST->ConOut->EnableCursor(gST->ConOut, TRUE);
  if (!EFI_ERROR(Status)) {
    gST->ConOut->ClearScreen(gST->ConOut);
  }
  return Status;
}
