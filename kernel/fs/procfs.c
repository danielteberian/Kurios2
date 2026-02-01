/* procfs.c - /proc Filesystem Implementation */

#include "procfs.h"
#include "vfs.h"
#include "../include/types.h"
#include "../debug/debug.h"
#include "../mm/pmm.h"
#include "../mm/slab.h"
#include "../process/process.h"
#include "../drivers/pit.h"
#include "../smp/percpu.h"
#include "../lib/string.h"
#include "../acpi/acpi.h"

/*
 * Procfs file content generators
 * These functions generate content dynamically when files are read.
 */

/* Maximum buffer size for procfs content */
#define PROCFS_MAX_CONTENT  4096

/*
 * Generate /proc/version content
 */
static int procfs_gen_version(char *buf, size_t size)
{
    return snprintf(buf, size,
        "Kurios2 version 0.1.0 (gcc)\n"
        "Compiled: " __DATE__ " " __TIME__ "\n");
}

/*
 * Generate /proc/meminfo content
 */
static int procfs_gen_meminfo(char *buf, size_t size)
{
    uint64_t total_pages = mem_info.total_pages;
    uint64_t free_pages = mem_info.free_pages;
    uint64_t used_pages = total_pages - free_pages;

    return snprintf(buf, size,
        "MemTotal:       %8llu kB\n"
        "MemFree:        %8llu kB\n"
        "MemUsed:        %8llu kB\n"
        "Buffers:        %8u kB\n"
        "Cached:         %8u kB\n",
        (unsigned long long)(total_pages * 4),
        (unsigned long long)(free_pages * 4),
        (unsigned long long)(used_pages * 4),
        0, 0);
}

/*
 * Generate /proc/uptime content
 */
static int procfs_gen_uptime(char *buf, size_t size)
{
    uint64_t ticks = pit_get_ticks();
    uint32_t freq = pit_get_frequency();

    if (freq == 0) freq = 100;  /* Default fallback */

    uint64_t seconds = ticks / freq;
    uint64_t centisecs = (ticks * 100 / freq) % 100;

    return snprintf(buf, size,
        "%llu.%02llu 0.00\n",
        (unsigned long long)seconds,
        (unsigned long long)centisecs);
}

/*
 * Generate /proc/cpuinfo content
 */
static int procfs_gen_cpuinfo(char *buf, size_t size)
{
    const acpi_info_t *acpi = acpi_get_info();
    uint32_t cpu_count = acpi ? acpi->cpu_count : 1;

    int offset = 0;

    for (uint32_t i = 0; i < cpu_count && (size_t)offset < size - 100; i++) {
        uint8_t apic_id = (acpi && i < MAX_CPUS) ? acpi->cpus[i].apic_id : i;

        offset += snprintf(buf + offset, size - offset,
            "processor\t: %u\n"
            "apicid\t\t: %u\n"
            "vendor_id\t: GenuineIntel\n"
            "cpu family\t: 6\n"
            "model\t\t: 0\n"
            "model name\t: Kurios2 Virtual CPU\n"
            "stepping\t: 0\n"
            "cpu MHz\t\t: 1000.000\n"
            "bogomips\t: 2000.00\n"
            "\n",
            i, apic_id);
    }

    return offset;
}

/*
 * Generate /proc/stat content
 */
static int procfs_gen_stat(char *buf, size_t size)
{
    return snprintf(buf, size,
        "cpu  0 0 0 0 0 0 0 0 0 0\n"
        "cpu0 0 0 0 0 0 0 0 0 0 0\n"
        "intr 0\n"
        "ctxt 0\n"
        "btime 0\n"
        "processes %u\n"
        "procs_running 1\n"
        "procs_blocked 0\n",
        process_count());
}

/*
 * Generate /proc/[pid]/status content
 */
static int procfs_gen_pid_status(char *buf, size_t size, process_t *proc)
{
    if (!proc) {
        return snprintf(buf, size, "Process not found\n");
    }

    return snprintf(buf, size,
        "Name:\t%s\n"
        "State:\t%s\n"
        "Pid:\t%u\n"
        "PPid:\t%u\n"
        "Threads:\t1\n",
        proc->name,
        process_state_name(proc->state),
        proc->pid,
        proc->parent_pid);
}

/*
 * Generate /proc/[pid]/cmdline content
 */
static int procfs_gen_pid_cmdline(char *buf, size_t size, process_t *proc)
{
    if (!proc) {
        return 0;
    }
    return snprintf(buf, size, "%s", proc->name);
}

/*
 * Procfs file types
 */
typedef enum {
    PROCFS_VERSION,
    PROCFS_MEMINFO,
    PROCFS_UPTIME,
    PROCFS_CPUINFO,
    PROCFS_STAT,
    PROCFS_PID_STATUS,
    PROCFS_PID_CMDLINE,
} procfs_file_type_t;

/*
 * Procfs file node data
 */
typedef struct {
    procfs_file_type_t type;
    uint32_t pid;  /* For per-process files */
} procfs_node_data_t;

/*
 * VFS operations for procfs
 */

static int procfs_open(vfs_node_t *node, uint32_t flags)
{
    (void)flags;
    /* Nothing special to do on open */
    return VFS_OK;
}

static void procfs_close(vfs_node_t *node)
{
    /* Nothing to clean up */
    (void)node;
}

static ssize_t procfs_read(vfs_node_t *node, void *buffer, size_t size, uint64_t offset)
{
    if (!node || !node->private) {
        return -ENOENT;
    }

    procfs_node_data_t *data = (procfs_node_data_t *)node->private;

    /* Generate content based on file type */
    char content[PROCFS_MAX_CONTENT];
    int content_len = 0;

    switch (data->type) {
    case PROCFS_VERSION:
        content_len = procfs_gen_version(content, sizeof(content));
        break;
    case PROCFS_MEMINFO:
        content_len = procfs_gen_meminfo(content, sizeof(content));
        break;
    case PROCFS_UPTIME:
        content_len = procfs_gen_uptime(content, sizeof(content));
        break;
    case PROCFS_CPUINFO:
        content_len = procfs_gen_cpuinfo(content, sizeof(content));
        break;
    case PROCFS_STAT:
        content_len = procfs_gen_stat(content, sizeof(content));
        break;
    case PROCFS_PID_STATUS:
        {
            process_t *proc = process_get_by_pid(data->pid);
            content_len = procfs_gen_pid_status(content, sizeof(content), proc);
        }
        break;
    case PROCFS_PID_CMDLINE:
        {
            process_t *proc = process_get_by_pid(data->pid);
            content_len = procfs_gen_pid_cmdline(content, sizeof(content), proc);
        }
        break;
    default:
        return 0;
    }

    if (content_len <= 0) {
        return 0;
    }

    /* Handle offset */
    if (offset >= (uint64_t)content_len) {
        return 0;  /* EOF */
    }

    size_t available = content_len - offset;
    size_t to_copy = (size < available) ? size : available;

    memcpy(buffer, content + offset, to_copy);
    return to_copy;
}

static ssize_t procfs_write(vfs_node_t *node, const void *buffer, size_t size, uint64_t offset)
{
    (void)node; (void)buffer; (void)size; (void)offset;
    return -EROFS;  /* Procfs is read-only */
}

/*
 * VFS node operations for procfs files
 */
static node_ops_t procfs_node_ops = {
    .open = procfs_open,
    .close = procfs_close,
    .read = procfs_read,
    .write = procfs_write,
    .readdir = NULL,
    .finddir = NULL,
    .create = NULL,
    .unlink = NULL,
    .mkdir = NULL,
    .rmdir = NULL,
    .truncate = NULL,
    .stat = NULL,
};

/*
 * Create a procfs file node
 */
static vfs_node_t *procfs_create_file(const char *name, procfs_file_type_t type, uint32_t pid)
{
    vfs_node_t *node = kmalloc(sizeof(vfs_node_t));
    if (!node) {
        return NULL;
    }

    memset(node, 0, sizeof(vfs_node_t));

    size_t name_len = strlen(name);
    if (name_len >= sizeof(node->name)) {
        name_len = sizeof(node->name) - 1;
    }
    memcpy(node->name, name, name_len);
    node->name[name_len] = '\0';

    node->type = VFS_FILE;
    node->permissions = VFS_PERM_READ;  /* Read-only */
    node->ops = &procfs_node_ops;
    node->ref_count = 1;

    /* Allocate private data */
    procfs_node_data_t *data = kmalloc(sizeof(procfs_node_data_t));
    if (!data) {
        kfree(node);
        return NULL;
    }
    data->type = type;
    data->pid = pid;
    node->private = data;

    return node;
}

/*
 * Procfs directory operations
 */
static vfs_node_t *procfs_root = NULL;

/*
 * Mount procfs at /proc
 * Creates the static file entries.
 */
int procfs_mount(void)
{
    /* Create /proc directory */
    int ret = vfs_mkdir("/proc");
    if (ret != VFS_OK && ret != -EEXIST) {
        ERROR("procfs: Failed to create /proc directory: %d", ret);
        return ret;
    }

    /* Get the /proc directory node */
    vfs_node_t *proc_dir = vfs_lookup("/proc");
    if (!proc_dir) {
        ERROR("procfs: Failed to lookup /proc");
        return -ENOENT;
    }

    procfs_root = proc_dir;

    /* Create static files in /proc */
    struct {
        const char *name;
        procfs_file_type_t type;
    } static_files[] = {
        { "version", PROCFS_VERSION },
        { "meminfo", PROCFS_MEMINFO },
        { "uptime", PROCFS_UPTIME },
        { "cpuinfo", PROCFS_CPUINFO },
        { "stat", PROCFS_STAT },
    };

    for (size_t i = 0; i < sizeof(static_files) / sizeof(static_files[0]); i++) {
        char path[64];
        snprintf(path, sizeof(path), "/proc/%s", static_files[i].name);

        vfs_node_t *node = procfs_create_file(static_files[i].name,
                                               static_files[i].type, 0);
        if (!node) {
            WARN("procfs: Failed to create %s", path);
            continue;
        }

        /* Add to /proc directory */
        if (proc_dir->children) {
            /* Find the last child */
            vfs_node_t *last = proc_dir->children;
            while (last->next) {
                last = last->next;
            }
            last->next = node;
            node->prev = last;
        } else {
            proc_dir->children = node;
        }
        node->parent = proc_dir;
    }

    INFO("procfs: Mounted at /proc");
    return VFS_OK;
}

/*
 * Initialize procfs
 */
void procfs_init(void)
{
    INFO("procfs: Initializing...");
    /* Nothing to do here - mounting happens later */
}

#ifdef DEBUG_TESTS
/*
 * Run procfs tests
 */
void procfs_run_tests(void)
{
    kprintf("\n=== Procfs Tests ===\n");

    /* Test 1: Read /proc/version */
    int fd = vfs_open("/proc/version", O_RDONLY);
    if (fd >= 0) {
        char buf[256];
        ssize_t n = vfs_read(fd, buf, sizeof(buf) - 1);
        if (n > 0) {
            buf[n] = '\0';
            kprintf("  Test 1 - /proc/version:\n    %s", buf);
        } else {
            kprintf("  Test 1 - /proc/version: FAIL (read returned %d)\n", (int)n);
        }
        vfs_close(fd);
    } else {
        kprintf("  Test 1 - /proc/version: FAIL (open returned %d)\n", fd);
    }

    /* Test 2: Read /proc/meminfo */
    fd = vfs_open("/proc/meminfo", O_RDONLY);
    if (fd >= 0) {
        char buf[512];
        ssize_t n = vfs_read(fd, buf, sizeof(buf) - 1);
        if (n > 0) {
            buf[n] = '\0';
            kprintf("  Test 2 - /proc/meminfo:\n%s", buf);
        }
        vfs_close(fd);
        kprintf("  Test 2 - /proc/meminfo: OK\n");
    } else {
        kprintf("  Test 2 - /proc/meminfo: FAIL\n");
    }

    /* Test 3: Read /proc/uptime */
    fd = vfs_open("/proc/uptime", O_RDONLY);
    if (fd >= 0) {
        char buf[64];
        ssize_t n = vfs_read(fd, buf, sizeof(buf) - 1);
        if (n > 0) {
            buf[n] = '\0';
            kprintf("  Test 3 - /proc/uptime: %s", buf);
        }
        vfs_close(fd);
    } else {
        kprintf("  Test 3 - /proc/uptime: FAIL\n");
    }

    /* Test 4: Read /proc/cpuinfo */
    fd = vfs_open("/proc/cpuinfo", O_RDONLY);
    if (fd >= 0) {
        char buf[1024];
        ssize_t n = vfs_read(fd, buf, sizeof(buf) - 1);
        if (n > 0) {
            buf[n] = '\0';
            kprintf("  Test 4 - /proc/cpuinfo: %d bytes read\n", (int)n);
        }
        vfs_close(fd);
    } else {
        kprintf("  Test 4 - /proc/cpuinfo: FAIL\n");
    }

    kprintf("\n  Procfs tests complete.\n\n");
}
#endif /* DEBUG_TESTS */
