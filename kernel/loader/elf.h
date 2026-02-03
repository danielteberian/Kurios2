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
 * Dynamic Section Tags
 */
#define DT_NULL         0       /* End of dynamic section */
#define DT_NEEDED       1       /* Name of needed library */
#define DT_PLTRELSZ     2       /* Size of PLT relocs */
#define DT_PLTGOT       3       /* Address of PLT/GOT */
#define DT_HASH         4       /* Address of symbol hash table */
#define DT_STRTAB       5       /* Address of string table */
#define DT_SYMTAB       6       /* Address of symbol table */
#define DT_RELA         7       /* Address of Rela relocs */
#define DT_RELASZ       8       /* Total size of Rela relocs */
#define DT_RELAENT      9       /* Size of one Rela reloc */
#define DT_STRSZ        10      /* Size of string table */
#define DT_SYMENT       11      /* Size of one symbol table entry */
#define DT_INIT         12      /* Address of init function */
#define DT_FINI         13      /* Address of fini function */
#define DT_SONAME       14      /* Name of shared object */
#define DT_RPATH        15      /* Library search path (deprecated) */
#define DT_SYMBOLIC     16      /* Start symbol search here */
#define DT_REL          17      /* Address of Rel relocs */
#define DT_RELSZ        18      /* Total size of Rel relocs */
#define DT_RELENT       19      /* Size of one Rel reloc */
#define DT_PLTREL       20      /* Type of reloc in PLT */
#define DT_DEBUG        21      /* For debugging */
#define DT_TEXTREL      22      /* Reloc might modify .text */
#define DT_JMPREL       23      /* Address of PLT relocs */
#define DT_BIND_NOW     24      /* Process relocations now */
#define DT_INIT_ARRAY   25      /* Array of init functions */
#define DT_FINI_ARRAY   26      /* Array of fini functions */
#define DT_INIT_ARRAYSZ 27      /* Size of init array */
#define DT_FINI_ARRAYSZ 28      /* Size of fini array */
#define DT_RUNPATH      29      /* Library search path */

/*
 * ELF64 Dynamic Section Entry
 */
typedef struct {
    int64_t     d_tag;          /* Dynamic entry type */
    union {
        uint64_t d_val;         /* Integer value */
        uint64_t d_ptr;         /* Address value */
    } d_un;
} __attribute__((packed)) Elf64_Dyn;

/*
 * Auxiliary Vector Types
 * These are passed on the stack to the dynamic linker/program
 */
#define AT_NULL         0       /* End of vector */
#define AT_IGNORE       1       /* Entry should be ignored */
#define AT_EXECFD       2       /* File descriptor of program */
#define AT_PHDR         3       /* Program headers for program */
#define AT_PHENT        4       /* Size of program header entry */
#define AT_PHNUM        5       /* Number of program headers */
#define AT_PAGESZ       6       /* System page size */
#define AT_BASE         7       /* Base address of interpreter */
#define AT_FLAGS        8       /* Flags */
#define AT_ENTRY        9       /* Entry point of program */
#define AT_NOTELF       10      /* Program is not ELF */
#define AT_UID          11      /* Real UID */
#define AT_EUID         12      /* Effective UID */
#define AT_GID          13      /* Real GID */
#define AT_EGID         14      /* Effective GID */
#define AT_PLATFORM     15      /* String identifying platform */
#define AT_HWCAP        16      /* Machine-dependent hints */
#define AT_CLKTCK       17      /* Frequency of times() */
#define AT_SECURE       23      /* Secure mode boolean */
#define AT_RANDOM       25      /* Address of 16 random bytes */
#define AT_EXECFN       31      /* Filename of executable */

/*
 * ELF64 Auxiliary Vector Entry
 */
typedef struct {
    uint64_t a_type;            /* Entry type */
    union {
        uint64_t a_val;         /* Integer value */
    } a_un;
} Elf64_auxv_t;

/*
 * Relocation Types for x86_64
 */
#define R_X86_64_NONE           0   /* No relocation */
#define R_X86_64_64             1   /* 64-bit absolute */
#define R_X86_64_PC32           2   /* 32-bit PC-relative */
#define R_X86_64_GOT32          3   /* 32-bit GOT entry */
#define R_X86_64_PLT32          4   /* 32-bit PLT address */
#define R_X86_64_COPY           5   /* Copy symbol at runtime */
#define R_X86_64_GLOB_DAT       6   /* Create GOT entry */
#define R_X86_64_JUMP_SLOT      7   /* Create PLT entry */
#define R_X86_64_RELATIVE       8   /* Adjust by program base */
#define R_X86_64_GOTPCREL       9   /* 32-bit PC-relative GOT */
#define R_X86_64_32             10  /* 32-bit absolute */
#define R_X86_64_32S            11  /* 32-bit sign-extended */
#define R_X86_64_16             12  /* 16-bit absolute */
#define R_X86_64_PC16           13  /* 16-bit PC-relative */
#define R_X86_64_8              14  /* 8-bit absolute */
#define R_X86_64_PC8            15  /* 8-bit PC-relative */

/*
 * ELF64 Relocation Entry (with addend)
 */
typedef struct {
    uint64_t    r_offset;       /* Address */
    uint64_t    r_info;         /* Relocation type and symbol index */
    int64_t     r_addend;       /* Addend */
} __attribute__((packed)) Elf64_Rela;

/*
 * Extract symbol and type from r_info
 */
#define ELF64_R_SYM(i)      ((i) >> 32)
#define ELF64_R_TYPE(i)     ((i) & 0xffffffffL)
#define ELF64_R_INFO(s,t)   (((s) << 32) + ((t) & 0xffffffffL))

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
