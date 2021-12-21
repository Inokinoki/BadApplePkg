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

  //Setup tick loop
  // BadAppleFrames
  EFI_EVENT TickEvent;
  UINTN eventId;
  gBS->CreateEvent(EVT_TIMER, 0, NULL, NULL, &TickEvent);
	gBS->SetTimer(TickEvent, TimerPeriodic, EFI_TIMER_PERIOD_MILLISECONDS(3000));
  // EFI_GRAPHICS_OUTPUT_BLT_PIXEL *temp;
  // EFI_GRAPHICS_OUTPUT_BLT_PIXEL *DrawBuffer;
  EFI_GRAPHICS_OUTPUT_BLT_PIXEL *SpriteSheet = NULL;
  UINTN SpriteSheetSize;
  UINTN SpriteSheetHeight;
  UINTN SpriteSheetWidth;
  UINTN BeginX = (screenWidth - VIDEO_WIDTH) / 2;
  UINTN BeginY = (screenHeight - VIDEO_HEIGHT) / 2;
  EFI_STRING  StringPtr = NULL;
  UINTN Length = StrLen (L"BadAppleFrames\\image-3288.bmp") + 1;
  StringPtr = AllocateZeroPool (Length * sizeof (CHAR16));
  for (int i = 0; i < 6573; i+=1) {
    //Copy Background to DrawBuffer
    // DrawBuffer = AllocateCopyPool(LevelWidth * SpriteLength * LevelHeight * SpriteLength, BackgroundBuffer);

    // Get frame
    /*
    UINTN
    EFIAPI
    UnicodeSPrint (
      OUT CHAR16        *StartOfBuffer,
      IN  UINTN         BufferSize,
      IN  CONST CHAR16  *FormatString,
      ...
      );
    */
    UnicodeSPrint (
      StringPtr,
      Length * sizeof (CHAR16),
      L"BadAppleFrames\\image-%04d.bmp",
      i
    );
    // Print(StringPtr);
    Status = LoadBMP(StringPtr, &SpriteSheet, &SpriteSheetHeight, &SpriteSheetWidth, &SpriteSheetSize);

    //Wait for tick
    gBS->WaitForEvent(1, &TickEvent, &eventId);
    if (EFI_ERROR(Status)) {
      Print(StringPtr);
    } else {
      // ExtractBuffer(DrawBuffer, LevelWidth * SpriteLength, LevelHeight * SpriteLength, 0, 0, &temp, 512, 512);
      graphicsProtocol->Blt(graphicsProtocol, SpriteSheet, EfiBltBufferToVideo, 0, 0, BeginX, BeginY, VIDEO_WIDTH, VIDEO_HEIGHT, 0);
      //Free screen and temporary
      // FreePool(temp);
      // FreePool(DrawBuffer);
    }
  }
// Cleanup:
  FreePool (StringPtr);
  gBS->CloseEvent(TickEvent);
  gST->ConOut->EnableCursor(gST->ConOut, TRUE);
  if (!EFI_ERROR(Status)) {
    gST->ConOut->ClearScreen(gST->ConOut);
  }
  return Status;
}