/* elf_loader.h - ELF Loader Interface */
#ifndef _LOADER_ELF_LOADER_H
#define _LOADER_ELF_LOADER_H

#include <stdint.h>
#include "../mm/as.h"

/*
 * ELF load result structure
 */
typedef struct {
    uint64_t entry_point;       /* Program entry point (or interpreter entry if present) */
    uint64_t base_addr;         /* Lowest loaded address */
    uint64_t end_addr;          /* Highest loaded address */
    uint64_t phdr_addr;         /* Program header address (for aux vector) */
    uint16_t phdr_num;          /* Number of program headers */
    uint16_t phdr_size;         /* Size of each program header */

    /* Dynamic linking support */
    bool has_interp;            /* True if PT_INTERP segment exists */
    char interp_path[256];      /* Path to interpreter (e.g., /lib64/ld-linux-x86-64.so.2) */
    uint64_t interp_base;       /* Base address where interpreter was loaded */
    uint64_t interp_entry;      /* Entry point of interpreter */
    uint64_t program_entry;     /* Original program entry point (before interp override) */

    /* PT_DYNAMIC segment info */
    bool has_dynamic;           /* True if PT_DYNAMIC segment exists */
    uint64_t dynamic_addr;      /* Virtual address of PT_DYNAMIC segment */
    uint64_t dynamic_size;      /* Size of PT_DYNAMIC segment */
} elf_load_result_t;

/*
 * Load an ELF executable into an address space
 *
 * @param as        Target address space
 * @param elf_data  Pointer to ELF file in memory
 * @param elf_size  Size of ELF file
 * @param result    Output: load result with entry point etc.
 * @return 0 on success, negative error code on failure
 */
int elf_load(address_space_t *as, const void *elf_data, uint64_t elf_size,
             elf_load_result_t *result);

/*
 * Load an ELF executable from a file path (using VFS)
 *
 * @param as        Target address space
 * @param path      Path to ELF file
 * @param result    Output: load result
 * @return 0 on success, negative error code on failure
 */
int elf_load_file(address_space_t *as, const char *path,
                  elf_load_result_t *result);

/*
 * Load the ELF interpreter (dynamic linker) if PT_INTERP exists
 *
 * @param as        Target address space (already has main program loaded)
 * @param result    ELF load result from main program (will be updated with interp info)
 * @return 0 on success, negative error code on failure
 */
int elf_load_interpreter(address_space_t *as, elf_load_result_t *result);

#ifdef DEBUG_TESTS
/*
 * Run ELF loader tests
 */
void elf_loader_run_tests(void);
#endif

#endif /* _LOADER_ELF_LOADER_H */
