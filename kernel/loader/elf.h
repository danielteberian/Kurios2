/* elf.h - ELF64 Format Definitions */
#ifndef _LOADER_ELF_H
#define _LOADER_ELF_H

#include <stdint.h>

/*
 * ELF Magic Number
 */
#define ELF_MAGIC       0x464C457F  /* "\x7FELF" in little-endian */

/*
 * ELF Identification Indices
 */
#define EI_MAG0         0       /* Magic number byte 0 */
#define EI_MAG1         1       /* Magic number byte 1 */
#define EI_MAG2         2       /* Magic number byte 2 */
#define EI_MAG3         3       /* Magic number byte 3 */
#define EI_CLASS        4       /* File class */
#define EI_DATA         5       /* Data encoding */
#define EI_VERSION      6       /* File version */
#define EI_OSABI        7       /* OS/ABI identification */
#define EI_ABIVERSION   8       /* ABI version */
#define EI_PAD          9       /* Start of padding bytes */
#define EI_NIDENT       16      /* Size of e_ident[] */

/*
 * ELF Class (32-bit vs 64-bit)
 */
#define ELFCLASSNONE    0       /* Invalid class */
#define ELFCLASS32      1       /* 32-bit objects */
#define ELFCLASS64      2       /* 64-bit objects */

/*
 * ELF Data Encoding (endianness)
 */
#define ELFDATANONE     0       /* Invalid encoding */
#define ELFDATA2LSB     1       /* Little-endian */
#define ELFDATA2MSB     2       /* Big-endian */

/*
 * ELF File Types
 */
#define ET_NONE         0       /* No file type */
#define ET_REL          1       /* Relocatable file */
#define ET_EXEC         2       /* Executable file */
#define ET_DYN          3       /* Shared object file */
#define ET_CORE         4       /* Core file */

/*
 * ELF Machine Types
 */
#define EM_NONE         0       /* No machine */
#define EM_386          3       /* Intel 80386 */
#define EM_X86_64       62      /* AMD x86-64 */

/*
 * ELF64 Header
 */
typedef struct {
    uint8_t     e_ident[EI_NIDENT]; /* ELF identification */
    uint16_t    e_type;             /* Object file type */
    uint16_t    e_machine;          /* Machine type */
    uint32_t    e_version;          /* Object file version */
    uint64_t    e_entry;            /* Entry point address */
    uint64_t    e_phoff;            /* Program header offset */
    uint64_t    e_shoff;            /* Section header offset */
    uint32_t    e_flags;            /* Processor-specific flags */
    uint16_t    e_ehsize;           /* ELF header size */
    uint16_t    e_phentsize;        /* Program header entry size */
    uint16_t    e_phnum;            /* Number of program headers */
    uint16_t    e_shentsize;        /* Section header entry size */
    uint16_t    e_shnum;            /* Number of section headers */
    uint16_t    e_shstrndx;         /* Section name string table index */
} __attribute__((packed)) Elf64_Ehdr;

/*
 * Program Header Types
 */
#define PT_NULL         0       /* Unused entry */
#define PT_LOAD         1       /* Loadable segment */
#define PT_DYNAMIC      2       /* Dynamic linking info */
#define PT_INTERP       3       /* Interpreter path */
#define PT_NOTE         4       /* Auxiliary information */
#define PT_SHLIB        5       /* Reserved */
#define PT_PHDR         6       /* Program header table */
#define PT_TLS          7       /* Thread-local storage */

/*
 * Program Header Flags
 */
#define PF_X            0x1     /* Execute permission */
#define PF_W            0x2     /* Write permission */
#define PF_R            0x4     /* Read permission */

/*
 * ELF64 Program Header
 */
typedef struct {
    uint32_t    p_type;         /* Segment type */
    uint32_t    p_flags;        /* Segment flags */
    uint64_t    p_offset;       /* File offset */
    uint64_t    p_vaddr;        /* Virtual address */
    uint64_t    p_paddr;        /* Physical address (unused) */
    uint64_t    p_filesz;       /* Size in file */
    uint64_t    p_memsz;        /* Size in memory */
    uint64_t    p_align;        /* Alignment */
} __attribute__((packed)) Elf64_Phdr;

/*
 * Section Header Types
 */
#define SHT_NULL        0       /* Inactive */
#define SHT_PROGBITS    1       /* Program data */
#define SHT_SYMTAB      2       /* Symbol table */
#define SHT_STRTAB      3       /* String table */
#define SHT_RELA        4       /* Relocation with addend */
#define SHT_HASH        5       /* Symbol hash table */
#define SHT_DYNAMIC     6       /* Dynamic linking info */
#define SHT_NOTE        7       /* Notes */
#define SHT_NOBITS      8       /* No file data (bss) */
#define SHT_REL         9       /* Relocation without addend */

/*
 * Section Header Flags
 */
#define SHF_WRITE       0x1     /* Writable */
#define SHF_ALLOC       0x2     /* Occupies memory */
#define SHF_EXECINSTR   0x4     /* Executable */

/*
 * ELF64 Section Header
 */
typedef struct {
    uint32_t    sh_name;        /* Section name (string table index) */
    uint32_t    sh_type;        /* Section type */
    uint64_t    sh_flags;       /* Section flags */
    uint64_t    sh_addr;        /* Virtual address */
    uint64_t    sh_offset;      /* File offset */
    uint64_t    sh_size;        /* Section size */
    uint32_t    sh_link;        /* Link to another section */
    uint32_t    sh_info;        /* Additional info */
    uint64_t    sh_addralign;   /* Alignment */
    uint64_t    sh_entsize;     /* Entry size if table */
} __attribute__((packed)) Elf64_Shdr;

/*
 * Validate ELF header
 * Returns 0 if valid, -1 if invalid
 */
static inline int elf64_validate(const Elf64_Ehdr *ehdr) {
    /* Check magic number */
    if (ehdr->e_ident[EI_MAG0] != 0x7F ||
        ehdr->e_ident[EI_MAG1] != 'E' ||
        ehdr->e_ident[EI_MAG2] != 'L' ||
        ehdr->e_ident[EI_MAG3] != 'F') {
        return -1;
    }

    /* Check class (must be 64-bit) */
    if (ehdr->e_ident[EI_CLASS] != ELFCLASS64) {
        return -1;
    }

    /* Check endianness (must be little-endian for x86_64) */
    if (ehdr->e_ident[EI_DATA] != ELFDATA2LSB) {
        return -1;
    }

    /* Check machine type (must be x86_64) */
    if (ehdr->e_machine != EM_X86_64) {
        return -1;
    }

    /* Check file type (must be executable) */
    if (ehdr->e_type != ET_EXEC && ehdr->e_type != ET_DYN) {
        return -1;
    }

    return 0;
}

#endif /* _LOADER_ELF_H */
