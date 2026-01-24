/* efi_types.h - Minimal UEFI type definitions */
#ifndef EFI_TYPES_H
#define EFI_TYPES_H

#include <stdint.h>
#include <stddef.h>

/* Basic UEFI types */
typedef uint64_t UINTN;
typedef int64_t  INTN;
typedef uint8_t  UINT8;
typedef uint16_t UINT16;
typedef uint32_t UINT32;
typedef uint64_t UINT64;
typedef int8_t   INT8;
typedef int16_t  INT16;
typedef int32_t  INT32;
typedef int64_t  INT64;
typedef uint8_t  BOOLEAN;
typedef uint16_t CHAR16;
typedef void     VOID;

typedef UINTN    EFI_STATUS;
typedef VOID*    EFI_HANDLE;
typedef VOID*    EFI_EVENT;
typedef UINT64   EFI_PHYSICAL_ADDRESS;
typedef UINT64   EFI_VIRTUAL_ADDRESS;

/* EFI Status codes */
#define EFI_SUCCESS              0
#define EFI_ERROR                0x8000000000000000ULL
#define EFI_LOAD_ERROR           (EFI_ERROR | 1)
#define EFI_INVALID_PARAMETER    (EFI_ERROR | 2)
#define EFI_UNSUPPORTED          (EFI_ERROR | 3)
#define EFI_BAD_BUFFER_SIZE      (EFI_ERROR | 4)
#define EFI_BUFFER_TOO_SMALL     (EFI_ERROR | 5)
#define EFI_NOT_FOUND            (EFI_ERROR | 14)

#define TRUE  1
#define FALSE 0
#ifndef NULL
#define NULL  ((void*)0)
#endif

/* Calling convention for UEFI (Microsoft x64 ABI) */
#define EFIAPI __attribute__((ms_abi))

/* GUIDs */
typedef struct {
    UINT32 Data1;
    UINT16 Data2;
    UINT16 Data3;
    UINT8  Data4[8];
} EFI_GUID;

/* Table header */
typedef struct {
    UINT64 Signature;
    UINT32 Revision;
    UINT32 HeaderSize;
    UINT32 CRC32;
    UINT32 Reserved;
} EFI_TABLE_HEADER;

/* Memory types */
typedef enum {
    EfiReservedMemoryType,
    EfiLoaderCode,
    EfiLoaderData,
    EfiBootServicesCode,
    EfiBootServicesData,
    EfiRuntimeServicesCode,
    EfiRuntimeServicesData,
    EfiConventionalMemory,
    EfiUnusableMemory,
    EfiACPIReclaimMemory,
    EfiACPIMemoryNVS,
    EfiMemoryMappedIO,
    EfiMemoryMappedIOPortSpace,
    EfiPalCode,
    EfiPersistentMemory,
    EfiMaxMemoryType
} EFI_MEMORY_TYPE;

/* Memory descriptor */
typedef struct {
    UINT32                Type;
    EFI_PHYSICAL_ADDRESS  PhysicalStart;
    EFI_VIRTUAL_ADDRESS   VirtualStart;
    UINT64                NumberOfPages;
    UINT64                Attribute;
} EFI_MEMORY_DESCRIPTOR;

/* Graphics output protocol types */
typedef enum {
    PixelRedGreenBlueReserved8BitPerColor,
    PixelBlueGreenRedReserved8BitPerColor,
    PixelBitMask,
    PixelBltOnly,
    PixelFormatMax
} EFI_GRAPHICS_PIXEL_FORMAT;

typedef struct {
    UINT32 RedMask;
    UINT32 GreenMask;
    UINT32 BlueMask;
    UINT32 ReservedMask;
} EFI_PIXEL_BITMASK;

typedef struct {
    UINT32                    Version;
    UINT32                    HorizontalResolution;
    UINT32                    VerticalResolution;
    EFI_GRAPHICS_PIXEL_FORMAT PixelFormat;
    EFI_PIXEL_BITMASK         PixelInformation;
    UINT32                    PixelsPerScanLine;
} EFI_GRAPHICS_OUTPUT_MODE_INFORMATION;

typedef struct {
    UINT32                              MaxMode;
    UINT32                              Mode;
    EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *Info;
    UINTN                               SizeOfInfo;
    EFI_PHYSICAL_ADDRESS                FrameBufferBase;
    UINTN                               FrameBufferSize;
} EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE;

/* Forward declarations */
struct _EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL;
struct _EFI_BOOT_SERVICES;
struct _EFI_RUNTIME_SERVICES;
struct _EFI_GRAPHICS_OUTPUT_PROTOCOL;

/* Simple text output protocol */
typedef EFI_STATUS (EFIAPI *EFI_TEXT_RESET)(
    struct _EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *This,
    BOOLEAN ExtendedVerification
);

typedef EFI_STATUS (EFIAPI *EFI_TEXT_STRING)(
    struct _EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *This,
    CHAR16 *String
);

typedef EFI_STATUS (EFIAPI *EFI_TEXT_CLEAR_SCREEN)(
    struct _EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *This
);

typedef struct _EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL {
    EFI_TEXT_RESET       Reset;
    EFI_TEXT_STRING      OutputString;
    VOID                 *TestString;
    VOID                 *QueryMode;
    VOID                 *SetMode;
    VOID                 *SetAttribute;
    EFI_TEXT_CLEAR_SCREEN ClearScreen;
    VOID                 *SetCursorPosition;
    VOID                 *EnableCursor;
    VOID                 *Mode;
} EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL;

/* Graphics output protocol */
typedef EFI_STATUS (EFIAPI *EFI_GRAPHICS_OUTPUT_PROTOCOL_QUERY_MODE)(
    struct _EFI_GRAPHICS_OUTPUT_PROTOCOL *This,
    UINT32 ModeNumber,
    UINTN *SizeOfInfo,
    EFI_GRAPHICS_OUTPUT_MODE_INFORMATION **Info
);

typedef EFI_STATUS (EFIAPI *EFI_GRAPHICS_OUTPUT_PROTOCOL_SET_MODE)(
    struct _EFI_GRAPHICS_OUTPUT_PROTOCOL *This,
    UINT32 ModeNumber
);

typedef struct _EFI_GRAPHICS_OUTPUT_PROTOCOL {
    EFI_GRAPHICS_OUTPUT_PROTOCOL_QUERY_MODE QueryMode;
    EFI_GRAPHICS_OUTPUT_PROTOCOL_SET_MODE   SetMode;
    VOID                                    *Blt;
    EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE       *Mode;
} EFI_GRAPHICS_OUTPUT_PROTOCOL;

/* File protocol types */
typedef struct _EFI_FILE_PROTOCOL EFI_FILE_PROTOCOL;

typedef EFI_STATUS (EFIAPI *EFI_FILE_OPEN)(
    EFI_FILE_PROTOCOL *This,
    EFI_FILE_PROTOCOL **NewHandle,
    CHAR16 *FileName,
    UINT64 OpenMode,
    UINT64 Attributes
);

typedef EFI_STATUS (EFIAPI *EFI_FILE_CLOSE)(
    EFI_FILE_PROTOCOL *This
);

typedef EFI_STATUS (EFIAPI *EFI_FILE_READ)(
    EFI_FILE_PROTOCOL *This,
    UINTN *BufferSize,
    VOID *Buffer
);

typedef EFI_STATUS (EFIAPI *EFI_FILE_GET_INFO)(
    EFI_FILE_PROTOCOL *This,
    EFI_GUID *InformationType,
    UINTN *BufferSize,
    VOID *Buffer
);

struct _EFI_FILE_PROTOCOL {
    UINT64          Revision;
    EFI_FILE_OPEN   Open;
    EFI_FILE_CLOSE  Close;
    VOID            *Delete;
    EFI_FILE_READ   Read;
    VOID            *Write;
    VOID            *GetPosition;
    VOID            *SetPosition;
    EFI_FILE_GET_INFO GetInfo;
    VOID            *SetInfo;
    VOID            *Flush;
};

#define EFI_FILE_MODE_READ   0x0000000000000001ULL
#define EFI_FILE_MODE_WRITE  0x0000000000000002ULL
#define EFI_FILE_MODE_CREATE 0x8000000000000000ULL

/* Simple file system protocol */
typedef struct _EFI_SIMPLE_FILE_SYSTEM_PROTOCOL EFI_SIMPLE_FILE_SYSTEM_PROTOCOL;

typedef EFI_STATUS (EFIAPI *EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_OPEN_VOLUME)(
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *This,
    EFI_FILE_PROTOCOL **Root
);

struct _EFI_SIMPLE_FILE_SYSTEM_PROTOCOL {
    UINT64 Revision;
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_OPEN_VOLUME OpenVolume;
};

/* File info */
typedef struct {
    UINT64 Size;
    UINT64 FileSize;
    UINT64 PhysicalSize;
    /* ... more fields but we don't need them */
} EFI_FILE_INFO;

/* Boot services */
typedef EFI_STATUS (EFIAPI *EFI_ALLOCATE_PAGES)(
    UINTN Type,
    EFI_MEMORY_TYPE MemoryType,
    UINTN Pages,
    EFI_PHYSICAL_ADDRESS *Memory
);

typedef EFI_STATUS (EFIAPI *EFI_FREE_PAGES)(
    EFI_PHYSICAL_ADDRESS Memory,
    UINTN Pages
);

typedef EFI_STATUS (EFIAPI *EFI_GET_MEMORY_MAP)(
    UINTN *MemoryMapSize,
    EFI_MEMORY_DESCRIPTOR *MemoryMap,
    UINTN *MapKey,
    UINTN *DescriptorSize,
    UINT32 *DescriptorVersion
);

typedef EFI_STATUS (EFIAPI *EFI_ALLOCATE_POOL)(
    EFI_MEMORY_TYPE PoolType,
    UINTN Size,
    VOID **Buffer
);

typedef EFI_STATUS (EFIAPI *EFI_FREE_POOL)(
    VOID *Buffer
);

typedef EFI_STATUS (EFIAPI *EFI_LOCATE_PROTOCOL)(
    EFI_GUID *Protocol,
    VOID *Registration,
    VOID **Interface
);

typedef EFI_STATUS (EFIAPI *EFI_EXIT_BOOT_SERVICES)(
    EFI_HANDLE ImageHandle,
    UINTN MapKey
);

typedef EFI_STATUS (EFIAPI *EFI_HANDLE_PROTOCOL)(
    EFI_HANDLE Handle,
    EFI_GUID *Protocol,
    VOID **Interface
);

typedef struct _EFI_BOOT_SERVICES {
    EFI_TABLE_HEADER        Hdr;
    VOID                    *RaiseTPL;
    VOID                    *RestoreTPL;
    EFI_ALLOCATE_PAGES      AllocatePages;
    EFI_FREE_PAGES          FreePages;
    EFI_GET_MEMORY_MAP      GetMemoryMap;
    EFI_ALLOCATE_POOL       AllocatePool;
    EFI_FREE_POOL           FreePool;
    VOID                    *CreateEvent;
    VOID                    *SetTimer;
    VOID                    *WaitForEvent;
    VOID                    *SignalEvent;
    VOID                    *CloseEvent;
    VOID                    *CheckEvent;
    VOID                    *InstallProtocolInterface;
    VOID                    *ReinstallProtocolInterface;
    VOID                    *UninstallProtocolInterface;
    EFI_HANDLE_PROTOCOL     HandleProtocol;
    VOID                    *Reserved;
    VOID                    *RegisterProtocolNotify;
    VOID                    *LocateHandle;
    VOID                    *LocateDevicePath;
    VOID                    *InstallConfigurationTable;
    VOID                    *LoadImage;
    VOID                    *StartImage;
    VOID                    *Exit;
    VOID                    *UnloadImage;
    EFI_EXIT_BOOT_SERVICES  ExitBootServices;
    VOID                    *GetNextMonotonicCount;
    VOID                    *Stall;
    VOID                    *SetWatchdogTimer;
    VOID                    *ConnectController;
    VOID                    *DisconnectController;
    VOID                    *OpenProtocol;
    VOID                    *CloseProtocol;
    VOID                    *OpenProtocolInformation;
    VOID                    *ProtocolsPerHandle;
    VOID                    *LocateHandleBuffer;
    EFI_LOCATE_PROTOCOL     LocateProtocol;
} EFI_BOOT_SERVICES;

/* Configuration table */
typedef struct {
    EFI_GUID VendorGuid;
    VOID     *VendorTable;
} EFI_CONFIGURATION_TABLE;

/* System table */
typedef struct {
    EFI_TABLE_HEADER                  Hdr;
    CHAR16                            *FirmwareVendor;
    UINT32                            FirmwareRevision;
    EFI_HANDLE                        ConsoleInHandle;
    VOID                              *ConIn;
    EFI_HANDLE                        ConsoleOutHandle;
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL   *ConOut;
    EFI_HANDLE                        StandardErrorHandle;
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL   *StdErr;
    VOID                              *RuntimeServices;
    EFI_BOOT_SERVICES                 *BootServices;
    UINTN                             NumberOfTableEntries;
    EFI_CONFIGURATION_TABLE           *ConfigurationTable;
} EFI_SYSTEM_TABLE;

/* Well-known GUIDs */
#define EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID \
    {0x9042a9de, 0x23dc, 0x4a38, {0x96, 0xfb, 0x7a, 0xde, 0xd0, 0x80, 0x51, 0x6a}}

#define EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID \
    {0x964e5b22, 0x6459, 0x11d2, {0x8e, 0x39, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b}}

#define EFI_LOADED_IMAGE_PROTOCOL_GUID \
    {0x5B1B31A1, 0x9562, 0x11d2, {0x8E, 0x3F, 0x00, 0xA0, 0xC9, 0x69, 0x72, 0x3B}}

#define EFI_FILE_INFO_GUID \
    {0x09576e92, 0x6d3f, 0x11d2, {0x8e, 0x39, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b}}

#define EFI_ACPI_20_TABLE_GUID \
    {0x8868e871, 0xe4f1, 0x11d3, {0xbc, 0x22, 0x00, 0x80, 0xc7, 0x3c, 0x88, 0x81}}

#define EFI_ACPI_TABLE_GUID \
    {0xeb9d2d30, 0x2d88, 0x11d3, {0x9a, 0x16, 0x00, 0x90, 0x27, 0x3f, 0xc1, 0x4d}}

/* Loaded image protocol */
typedef struct {
    UINT32            Revision;
    EFI_HANDLE        ParentHandle;
    EFI_SYSTEM_TABLE  *SystemTable;
    EFI_HANDLE        DeviceHandle;
    VOID              *FilePath;
    VOID              *Reserved;
    UINT32            LoadOptionsSize;
    VOID              *LoadOptions;
    VOID              *ImageBase;
    UINT64            ImageSize;
    EFI_MEMORY_TYPE   ImageCodeType;
    EFI_MEMORY_TYPE   ImageDataType;
    VOID              *Unload;
} EFI_LOADED_IMAGE_PROTOCOL;

#endif /* EFI_TYPES_H */
