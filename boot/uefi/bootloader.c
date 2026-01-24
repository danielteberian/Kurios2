/* bootloader.c - UEFI Bootloader for Kurios2 */

#include "efi_types.h"

/* Boot info structure - must match boot_info.inc */
#define KURIOS_BOOT_MAGIC    0x4B55524953ULL
#define BOOT_PROTOCOL_VERSION 0x0001

#define BOOT_FLAG_BIOS        (1 << 0)
#define BOOT_FLAG_UEFI        (1 << 1)
#define BOOT_FLAG_FRAMEBUFFER (1 << 2)
#define BOOT_FLAG_ACPI        (1 << 3)

typedef struct {
    UINT64 base;
    UINT64 length;
    UINT32 type;
    UINT32 acpi_attrs;
} MemoryMapEntry;

typedef struct {
    UINT64 address;
    UINT32 width;
    UINT32 height;
    UINT32 pitch;
    UINT32 bpp;
    UINT32 red_mask;
    UINT32 green_mask;
    UINT32 blue_mask;
} FramebufferInfo;

typedef struct {
    UINT64 magic;
    UINT64 version;
    UINT64 flags;
    UINT64 memory_map;
    UINT64 memory_count;
    UINT64 framebuffer;
    UINT64 acpi_rsdp;
    UINT64 kernel_phys;
    UINT64 kernel_size;
    UINT64 cmdline;
    UINT64 boot_drive;
} BootInfo;

/* Global pointers */
static EFI_SYSTEM_TABLE *gST;
static EFI_BOOT_SERVICES *gBS;
static EFI_HANDLE gImageHandle;

/* Kernel path */
static CHAR16 *KernelPath = L"\\EFI\\KURIOS\\KERNEL.BIN";

/* Boot info and related structures */
static BootInfo *gBootInfo;
static MemoryMapEntry *gMemoryMap;
static FramebufferInfo *gFramebufferInfo;

/* Helper to compare GUIDs */
static BOOLEAN guid_equal(EFI_GUID *a, EFI_GUID *b) {
    UINT8 *pa = (UINT8*)a;
    UINT8 *pb = (UINT8*)b;
    for (int i = 0; i < 16; i++) {
        if (pa[i] != pb[i]) return FALSE;
    }
    return TRUE;
}

/* Print string to console */
static void print(CHAR16 *str) {
    gST->ConOut->OutputString(gST->ConOut, str);
}

/* Print hex number */
static void print_hex(UINT64 value) {
    CHAR16 buf[17];
    CHAR16 *hex = L"0123456789ABCDEF";
    buf[16] = 0;
    for (int i = 15; i >= 0; i--) {
        buf[i] = hex[value & 0xF];
        value >>= 4;
    }
    print(buf);
}

/* Find ACPI RSDP from configuration tables */
static VOID *find_acpi_rsdp(void) {
    EFI_GUID acpi20_guid = EFI_ACPI_20_TABLE_GUID;
    EFI_GUID acpi_guid = EFI_ACPI_TABLE_GUID;

    for (UINTN i = 0; i < gST->NumberOfTableEntries; i++) {
        if (guid_equal(&gST->ConfigurationTable[i].VendorGuid, &acpi20_guid)) {
            return gST->ConfigurationTable[i].VendorTable;
        }
    }

    for (UINTN i = 0; i < gST->NumberOfTableEntries; i++) {
        if (guid_equal(&gST->ConfigurationTable[i].VendorGuid, &acpi_guid)) {
            return gST->ConfigurationTable[i].VendorTable;
        }
    }

    return NULL;
}

/* Set up graphics output */
static EFI_STATUS setup_graphics(void) {
    EFI_GUID gop_guid = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;
    EFI_GRAPHICS_OUTPUT_PROTOCOL *gop;
    EFI_STATUS status;

    status = gBS->LocateProtocol(&gop_guid, NULL, (VOID**)&gop);
    if (status != EFI_SUCCESS) {
        print(L"Graphics output not available\r\n");
        return status;
    }

    /* Find best mode (highest resolution with 32bpp) */
    UINT32 best_mode = gop->Mode->Mode;
    UINT32 best_pixels = 0;

    for (UINT32 i = 0; i < gop->Mode->MaxMode; i++) {
        EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *info;
        UINTN size;

        status = gop->QueryMode(gop, i, &size, &info);
        if (status != EFI_SUCCESS) continue;

        /* Only consider 32-bit modes */
        if (info->PixelFormat == PixelRedGreenBlueReserved8BitPerColor ||
            info->PixelFormat == PixelBlueGreenRedReserved8BitPerColor) {
            UINT32 pixels = info->HorizontalResolution * info->VerticalResolution;
            if (pixels > best_pixels) {
                best_pixels = pixels;
                best_mode = i;
            }
        }
    }

    /* Set the best mode */
    status = gop->SetMode(gop, best_mode);
    if (status != EFI_SUCCESS) {
        print(L"Failed to set graphics mode\r\n");
        return status;
    }

    /* Fill in framebuffer info */
    gFramebufferInfo->address = gop->Mode->FrameBufferBase;
    gFramebufferInfo->width = gop->Mode->Info->HorizontalResolution;
    gFramebufferInfo->height = gop->Mode->Info->VerticalResolution;
    gFramebufferInfo->pitch = gop->Mode->Info->PixelsPerScanLine * 4;
    gFramebufferInfo->bpp = 32;

    if (gop->Mode->Info->PixelFormat == PixelRedGreenBlueReserved8BitPerColor) {
        gFramebufferInfo->red_mask = 0x000000FF;
        gFramebufferInfo->green_mask = 0x0000FF00;
        gFramebufferInfo->blue_mask = 0x00FF0000;
    } else {
        gFramebufferInfo->red_mask = 0x00FF0000;
        gFramebufferInfo->green_mask = 0x0000FF00;
        gFramebufferInfo->blue_mask = 0x000000FF;
    }

    print(L"Graphics: ");
    print_hex(gFramebufferInfo->width);
    print(L"x");
    print_hex(gFramebufferInfo->height);
    print(L"\r\n");

    return EFI_SUCCESS;
}

/* Load kernel from filesystem */
static EFI_STATUS load_kernel(VOID **kernel_base, UINTN *kernel_size) {
    EFI_GUID lip_guid = EFI_LOADED_IMAGE_PROTOCOL_GUID;
    EFI_GUID sfsp_guid = EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID;
    EFI_GUID fi_guid = EFI_FILE_INFO_GUID;

    EFI_LOADED_IMAGE_PROTOCOL *loaded_image;
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *fs;
    EFI_FILE_PROTOCOL *root, *kernel_file;
    EFI_STATUS status;

    /* Get loaded image protocol */
    status = gBS->HandleProtocol(gImageHandle, &lip_guid, (VOID**)&loaded_image);
    if (status != EFI_SUCCESS) {
        print(L"Failed to get loaded image protocol\r\n");
        return status;
    }

    /* Get filesystem protocol */
    status = gBS->HandleProtocol(loaded_image->DeviceHandle, &sfsp_guid, (VOID**)&fs);
    if (status != EFI_SUCCESS) {
        print(L"Failed to get filesystem protocol\r\n");
        return status;
    }

    /* Open volume */
    status = fs->OpenVolume(fs, &root);
    if (status != EFI_SUCCESS) {
        print(L"Failed to open volume\r\n");
        return status;
    }

    /* Open kernel file */
    status = root->Open(root, &kernel_file, KernelPath, EFI_FILE_MODE_READ, 0);
    if (status != EFI_SUCCESS) {
        print(L"Failed to open kernel file: ");
        print(KernelPath);
        print(L"\r\n");
        return status;
    }

    /* Get file size */
    UINT8 info_buffer[256];
    UINTN info_size = sizeof(info_buffer);
    status = kernel_file->GetInfo(kernel_file, &fi_guid, &info_size, info_buffer);
    if (status != EFI_SUCCESS) {
        print(L"Failed to get kernel file info\r\n");
        kernel_file->Close(kernel_file);
        return status;
    }

    EFI_FILE_INFO *file_info = (EFI_FILE_INFO*)info_buffer;
    *kernel_size = file_info->FileSize;

    print(L"Kernel size: ");
    print_hex(*kernel_size);
    print(L" bytes\r\n");

    /* Allocate memory for kernel at 1MB */
    UINTN pages = (*kernel_size + 4095) / 4096;
    EFI_PHYSICAL_ADDRESS kernel_addr = 0x100000;  /* 1MB */

    status = gBS->AllocatePages(0, EfiLoaderData, pages, &kernel_addr);
    if (status != EFI_SUCCESS) {
        print(L"Failed to allocate memory for kernel\r\n");
        kernel_file->Close(kernel_file);
        return status;
    }

    *kernel_base = (VOID*)kernel_addr;

    /* Read kernel */
    status = kernel_file->Read(kernel_file, kernel_size, *kernel_base);
    if (status != EFI_SUCCESS) {
        print(L"Failed to read kernel\r\n");
        kernel_file->Close(kernel_file);
        return status;
    }

    kernel_file->Close(kernel_file);
    root->Close(root);

    print(L"Kernel loaded at 0x");
    print_hex((UINT64)*kernel_base);
    print(L"\r\n");

    return EFI_SUCCESS;
}

/* Convert EFI memory type to E820 type */
static UINT32 efi_to_e820_type(UINT32 efi_type) {
    switch (efi_type) {
        case EfiConventionalMemory:
        case EfiLoaderCode:
        case EfiLoaderData:
        case EfiBootServicesCode:
        case EfiBootServicesData:
            return 1;  /* Usable */
        case EfiACPIReclaimMemory:
            return 3;  /* ACPI reclaimable */
        case EfiACPIMemoryNVS:
            return 4;  /* ACPI NVS */
        case EfiUnusableMemory:
            return 5;  /* Bad memory */
        default:
            return 2;  /* Reserved */
    }
}

/* Get memory map and exit boot services */
static EFI_STATUS get_memory_map_and_exit(void) {
    EFI_STATUS status;
    UINTN map_size = 0;
    UINTN map_key;
    UINTN desc_size;
    UINT32 desc_version;
    EFI_MEMORY_DESCRIPTOR *efi_map = NULL;

    /* Get memory map size */
    status = gBS->GetMemoryMap(&map_size, NULL, &map_key, &desc_size, &desc_version);
    if (status != EFI_BUFFER_TOO_SMALL) {
        print(L"Failed to get memory map size\r\n");
        return status;
    }

    /* Allocate buffer (add extra space for the allocation itself) */
    map_size += desc_size * 4;
    status = gBS->AllocatePool(EfiLoaderData, map_size, (VOID**)&efi_map);
    if (status != EFI_SUCCESS) {
        print(L"Failed to allocate memory map buffer\r\n");
        return status;
    }

    /* Get actual memory map */
    status = gBS->GetMemoryMap(&map_size, efi_map, &map_key, &desc_size, &desc_version);
    if (status != EFI_SUCCESS) {
        print(L"Failed to get memory map\r\n");
        return status;
    }

    /* Convert to our format */
    UINTN entry_count = map_size / desc_size;
    gBootInfo->memory_count = 0;

    for (UINTN i = 0; i < entry_count && gBootInfo->memory_count < 64; i++) {
        EFI_MEMORY_DESCRIPTOR *desc = (EFI_MEMORY_DESCRIPTOR*)((UINT8*)efi_map + i * desc_size);

        gMemoryMap[gBootInfo->memory_count].base = desc->PhysicalStart;
        gMemoryMap[gBootInfo->memory_count].length = desc->NumberOfPages * 4096;
        gMemoryMap[gBootInfo->memory_count].type = efi_to_e820_type(desc->Type);
        gMemoryMap[gBootInfo->memory_count].acpi_attrs = 0;
        gBootInfo->memory_count++;
    }

    /* Exit boot services */
    status = gBS->ExitBootServices(gImageHandle, map_key);
    if (status != EFI_SUCCESS) {
        /* Memory map may have changed, try again */
        map_size = 0;
        gBS->GetMemoryMap(&map_size, NULL, &map_key, &desc_size, &desc_version);
        map_size += desc_size * 4;
        gBS->GetMemoryMap(&map_size, efi_map, &map_key, &desc_size, &desc_version);
        status = gBS->ExitBootServices(gImageHandle, map_key);
    }

    return status;
}

/* Kernel entry point type */
typedef void (*KernelEntry)(BootInfo *boot_info);

/* EFI entry point */
EFI_STATUS EFIAPI efi_main(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable) {
    EFI_STATUS status;
    VOID *kernel_base;
    UINTN kernel_size;

    /* Save global pointers */
    gST = SystemTable;
    gBS = SystemTable->BootServices;
    gImageHandle = ImageHandle;

    /* Clear screen */
    gST->ConOut->ClearScreen(gST->ConOut);

    print(L"Kurios2 UEFI Bootloader\r\n");
    print(L"=======================\r\n\r\n");

    /* Allocate boot info structures */
    status = gBS->AllocatePool(EfiLoaderData, sizeof(BootInfo), (VOID**)&gBootInfo);
    if (status != EFI_SUCCESS) {
        print(L"Failed to allocate boot info\r\n");
        return status;
    }

    status = gBS->AllocatePool(EfiLoaderData, sizeof(MemoryMapEntry) * 64, (VOID**)&gMemoryMap);
    if (status != EFI_SUCCESS) {
        print(L"Failed to allocate memory map\r\n");
        return status;
    }

    status = gBS->AllocatePool(EfiLoaderData, sizeof(FramebufferInfo), (VOID**)&gFramebufferInfo);
    if (status != EFI_SUCCESS) {
        print(L"Failed to allocate framebuffer info\r\n");
        return status;
    }

    /* Initialize boot info */
    gBootInfo->magic = KURIOS_BOOT_MAGIC;
    gBootInfo->version = BOOT_PROTOCOL_VERSION;
    gBootInfo->flags = BOOT_FLAG_UEFI;
    gBootInfo->memory_map = (UINT64)gMemoryMap;
    gBootInfo->framebuffer = (UINT64)gFramebufferInfo;
    gBootInfo->cmdline = 0;
    gBootInfo->boot_drive = 0;

    /* Find ACPI RSDP */
    gBootInfo->acpi_rsdp = (UINT64)find_acpi_rsdp();
    if (gBootInfo->acpi_rsdp) {
        gBootInfo->flags |= BOOT_FLAG_ACPI;
        print(L"ACPI RSDP found at 0x");
        print_hex(gBootInfo->acpi_rsdp);
        print(L"\r\n");
    }

    /* Set up graphics */
    status = setup_graphics();
    if (status == EFI_SUCCESS) {
        gBootInfo->flags |= BOOT_FLAG_FRAMEBUFFER;
    }

    /* Load kernel */
    status = load_kernel(&kernel_base, &kernel_size);
    if (status != EFI_SUCCESS) {
        print(L"Failed to load kernel!\r\n");
        return status;
    }

    gBootInfo->kernel_phys = (UINT64)kernel_base;
    gBootInfo->kernel_size = kernel_size;

    print(L"\r\nExiting boot services...\r\n");

    /* Get memory map and exit boot services */
    status = get_memory_map_and_exit();
    if (status != EFI_SUCCESS) {
        /* Can't print anymore - boot services are gone or failed */
        return status;
    }

    /* Jump to kernel */
    KernelEntry kernel_entry = (KernelEntry)kernel_base;
    kernel_entry(gBootInfo);

    /* Should never return */
    while (1) {
        __asm__ volatile("hlt");
    }

    return EFI_SUCCESS;
}
