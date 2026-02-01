# Kurios2 User-Space Development Checklist

Comprehensive checklist for libc, libm, and utility suite development.

---

## Table of Contents
1. [C Library (libc)](#c-library-libc)
2. [Math Library (libm)](#math-library-libm)
3. [Utility Suite](#utility-suite)
4. [Development Tools](#development-tools)

---

## C Library (libc)

### Headers (Freestanding - No OS Required)
- [ ] `stddef.h` - size_t, ptrdiff_t, NULL, offsetof
- [ ] `stdint.h` - int8_t, uint8_t, int16_t, uint16_t, int32_t, uint32_t, int64_t, uint64_t, intptr_t, uintptr_t, INT_MAX, etc.
- [ ] `stdbool.h` - bool, true, false
- [ ] `stdarg.h` - va_list, va_start, va_arg, va_end, va_copy
- [ ] `limits.h` - CHAR_BIT, CHAR_MAX, INT_MAX, LONG_MAX, etc.
- [ ] `float.h` - FLT_MAX, DBL_MAX, FLT_EPSILON, etc.
- [ ] `stdalign.h` - alignas, alignof
- [ ] `stdnoreturn.h` - noreturn

### String Functions (string.h)
#### Copying
- [ ] `memcpy` - Copy memory block
- [ ] `memmove` - Copy memory block (overlapping safe)
- [ ] `strcpy` - Copy string
- [ ] `strncpy` - Copy string with length limit
- [ ] `strdup` - Duplicate string (malloc)
- [ ] `strndup` - Duplicate string with length limit

#### Concatenation
- [ ] `strcat` - Concatenate strings
- [ ] `strncat` - Concatenate strings with length limit

#### Comparison
- [ ] `memcmp` - Compare memory blocks
- [ ] `strcmp` - Compare strings
- [ ] `strncmp` - Compare strings with length limit
- [ ] `strcasecmp` - Compare strings (case-insensitive)
- [ ] `strncasecmp` - Compare strings (case-insensitive, length limit)
- [ ] `strcoll` - Compare strings using locale

#### Searching
- [ ] `memchr` - Find byte in memory block
- [ ] `memrchr` - Find byte in memory block (reverse)
- [ ] `strchr` - Find character in string
- [ ] `strrchr` - Find character in string (reverse)
- [ ] `strstr` - Find substring
- [ ] `strcasestr` - Find substring (case-insensitive)
- [ ] `strpbrk` - Find any of set of characters
- [ ] `strspn` - Get span of character set
- [ ] `strcspn` - Get span until character set
- [ ] `strtok` - Tokenize string
- [ ] `strtok_r` - Tokenize string (reentrant)

#### Other
- [ ] `memset` - Fill memory block
- [ ] `strlen` - Get string length
- [ ] `strnlen` - Get string length with limit
- [ ] `strerror` - Get error message string
- [ ] `strerror_r` - Get error message string (reentrant)
- [ ] `strsignal` - Get signal description string

### Standard I/O (stdio.h)
#### Types and Macros
- [ ] `FILE` type
- [ ] `fpos_t` type
- [ ] `EOF`, `NULL`, `BUFSIZ`, `FILENAME_MAX`, `FOPEN_MAX`
- [ ] `stdin`, `stdout`, `stderr`
- [ ] `SEEK_SET`, `SEEK_CUR`, `SEEK_END`
- [ ] `_IOFBF`, `_IOLBF`, `_IONBF`

#### File Operations
- [ ] `fopen` - Open file
- [ ] `freopen` - Reopen file with different mode
- [ ] `fclose` - Close file
- [ ] `fflush` - Flush stream
- [ ] `fwide` - Set stream orientation
- [ ] `setbuf` - Set stream buffer
- [ ] `setvbuf` - Set stream buffer with mode

#### Direct I/O
- [ ] `fread` - Read block of data
- [ ] `fwrite` - Write block of data

#### Character I/O
- [ ] `fgetc` - Get character from stream
- [ ] `getc` - Get character (macro)
- [ ] `getchar` - Get character from stdin
- [ ] `fputc` - Put character to stream
- [ ] `putc` - Put character (macro)
- [ ] `putchar` - Put character to stdout
- [ ] `ungetc` - Push character back to stream

#### String I/O
- [ ] `fgets` - Get string from stream
- [ ] `gets` - Get string from stdin (deprecated, but for compat)
- [ ] `fputs` - Put string to stream
- [ ] `puts` - Put string to stdout

#### Formatted I/O
- [ ] `printf` - Print formatted to stdout
- [ ] `fprintf` - Print formatted to stream
- [ ] `sprintf` - Print formatted to string
- [ ] `snprintf` - Print formatted to string (length limit)
- [ ] `vprintf` - Print formatted (va_list)
- [ ] `vfprintf` - Print formatted to stream (va_list)
- [ ] `vsprintf` - Print formatted to string (va_list)
- [ ] `vsnprintf` - Print formatted to string (va_list, length limit)
- [ ] `scanf` - Read formatted from stdin
- [ ] `fscanf` - Read formatted from stream
- [ ] `sscanf` - Read formatted from string
- [ ] `vscanf` - Read formatted (va_list)
- [ ] `vfscanf` - Read formatted from stream (va_list)
- [ ] `vsscanf` - Read formatted from string (va_list)

#### Positioning
- [ ] `fseek` - Seek to position
- [ ] `ftell` - Get current position
- [ ] `rewind` - Rewind to beginning
- [ ] `fgetpos` - Get position (fpos_t)
- [ ] `fsetpos` - Set position (fpos_t)

#### Error Handling
- [ ] `feof` - Check end-of-file
- [ ] `ferror` - Check error indicator
- [ ] `clearerr` - Clear error indicators
- [ ] `perror` - Print error message

#### File Operations
- [ ] `remove` - Remove file
- [ ] `rename` - Rename file
- [ ] `tmpfile` - Create temporary file
- [ ] `tmpnam` - Generate temporary filename

### Standard Library (stdlib.h)
#### String Conversion
- [ ] `atof` - String to double
- [ ] `atoi` - String to int
- [ ] `atol` - String to long
- [ ] `atoll` - String to long long
- [ ] `strtod` - String to double (with end pointer)
- [ ] `strtof` - String to float
- [ ] `strtold` - String to long double
- [ ] `strtol` - String to long
- [ ] `strtoll` - String to long long
- [ ] `strtoul` - String to unsigned long
- [ ] `strtoull` - String to unsigned long long

#### Memory Allocation
- [ ] `malloc` - Allocate memory
- [ ] `calloc` - Allocate and zero memory
- [ ] `realloc` - Reallocate memory
- [ ] `free` - Free memory
- [ ] `aligned_alloc` - Allocate aligned memory
- [ ] `posix_memalign` - Allocate aligned memory (POSIX)

#### Random Numbers
- [ ] `rand` - Generate random number
- [ ] `srand` - Seed random number generator
- [ ] `random` - Generate random number (better)
- [ ] `srandom` - Seed random (better)
- [ ] `rand_r` - Generate random (reentrant)

#### Environment
- [ ] `getenv` - Get environment variable
- [ ] `setenv` - Set environment variable
- [ ] `unsetenv` - Unset environment variable
- [ ] `putenv` - Put environment string
- [ ] `clearenv` - Clear environment

#### Program Execution
- [ ] `system` - Execute shell command
- [ ] `exit` - Terminate program
- [ ] `_Exit` - Terminate program (no cleanup)
- [ ] `atexit` - Register exit function
- [ ] `at_quick_exit` - Register quick exit function
- [ ] `quick_exit` - Quick exit
- [ ] `abort` - Abort program

#### Searching and Sorting
- [ ] `qsort` - Quick sort
- [ ] `qsort_r` - Quick sort (reentrant)
- [ ] `bsearch` - Binary search
- [ ] `bsearch_r` - Binary search (reentrant)

#### Integer Arithmetic
- [ ] `abs` - Absolute value (int)
- [ ] `labs` - Absolute value (long)
- [ ] `llabs` - Absolute value (long long)
- [ ] `div` - Division with remainder (int)
- [ ] `ldiv` - Division with remainder (long)
- [ ] `lldiv` - Division with remainder (long long)

#### Multibyte/Wide Characters
- [ ] `mblen` - Get multibyte character length
- [ ] `mbtowc` - Convert multibyte to wide char
- [ ] `wctomb` - Convert wide char to multibyte
- [ ] `mbstowcs` - Convert multibyte string to wide
- [ ] `wcstombs` - Convert wide string to multibyte

### Character Classification (ctype.h)
- [ ] `isalnum` - Alphanumeric
- [ ] `isalpha` - Alphabetic
- [ ] `isblank` - Blank (space/tab)
- [ ] `iscntrl` - Control character
- [ ] `isdigit` - Decimal digit
- [ ] `isgraph` - Printable (not space)
- [ ] `islower` - Lowercase
- [ ] `isprint` - Printable
- [ ] `ispunct` - Punctuation
- [ ] `isspace` - Whitespace
- [ ] `isupper` - Uppercase
- [ ] `isxdigit` - Hexadecimal digit
- [ ] `tolower` - Convert to lowercase
- [ ] `toupper` - Convert to uppercase
- [ ] `isascii` - ASCII character (0-127)
- [ ] `toascii` - Convert to ASCII

### Wide Characters (wchar.h)
- [ ] `wint_t`, `wchar_t`, `mbstate_t` types
- [ ] `WEOF`, `WCHAR_MAX`, `WCHAR_MIN`
- [ ] Wide string functions (wcscpy, wcslen, etc.)
- [ ] Wide I/O functions (fgetwc, fputwc, etc.)
- [ ] Wide formatting (wprintf, wscanf, etc.)
- [ ] Multibyte conversion (mbrtowc, wcrtomb, etc.)

### Wide Character Classification (wctype.h)
- [ ] `iswalnum`, `iswalpha`, `iswdigit`, etc.
- [ ] `towlower`, `towupper`
- [ ] `wctype`, `iswctype`
- [ ] `wctrans`, `towctrans`

### Error Handling (errno.h)
- [ ] `errno` - Error number variable
- [ ] Error codes:
  - [ ] `EPERM` (1) - Operation not permitted
  - [ ] `ENOENT` (2) - No such file or directory
  - [ ] `ESRCH` (3) - No such process
  - [ ] `EINTR` (4) - Interrupted system call
  - [ ] `EIO` (5) - I/O error
  - [ ] `ENXIO` (6) - No such device or address
  - [ ] `E2BIG` (7) - Argument list too long
  - [ ] `ENOEXEC` (8) - Exec format error
  - [ ] `EBADF` (9) - Bad file descriptor
  - [ ] `ECHILD` (10) - No child processes
  - [ ] `EAGAIN` (11) - Try again
  - [ ] `ENOMEM` (12) - Out of memory
  - [ ] `EACCES` (13) - Permission denied
  - [ ] `EFAULT` (14) - Bad address
  - [ ] `ENOTBLK` (15) - Block device required
  - [ ] `EBUSY` (16) - Device or resource busy
  - [ ] `EEXIST` (17) - File exists
  - [ ] `EXDEV` (18) - Cross-device link
  - [ ] `ENODEV` (19) - No such device
  - [ ] `ENOTDIR` (20) - Not a directory
  - [ ] `EISDIR` (21) - Is a directory
  - [ ] `EINVAL` (22) - Invalid argument
  - [ ] `ENFILE` (23) - File table overflow
  - [ ] `EMFILE` (24) - Too many open files
  - [ ] `ENOTTY` (25) - Not a typewriter
  - [ ] `ETXTBSY` (26) - Text file busy
  - [ ] `EFBIG` (27) - File too large
  - [ ] `ENOSPC` (28) - No space left on device
  - [ ] `ESPIPE` (29) - Illegal seek
  - [ ] `EROFS` (30) - Read-only file system
  - [ ] `EMLINK` (31) - Too many links
  - [ ] `EPIPE` (32) - Broken pipe
  - [ ] `EDOM` (33) - Math argument out of domain
  - [ ] `ERANGE` (34) - Math result not representable
  - [ ] `EDEADLK` (35) - Resource deadlock would occur
  - [ ] `ENAMETOOLONG` (36) - File name too long
  - [ ] `ENOLCK` (37) - No record locks available
  - [ ] `ENOSYS` (38) - Function not implemented
  - [ ] `ENOTEMPTY` (39) - Directory not empty
  - [ ] `ELOOP` (40) - Too many symbolic links
  - [ ] `EWOULDBLOCK` (=EAGAIN) - Operation would block
  - [ ] `ENOMSG` (42) - No message of desired type
  - [ ] `EIDRM` (43) - Identifier removed
  - [ ] `ENOSTR` (60) - Device not a stream
  - [ ] `ENODATA` (61) - No data available
  - [ ] `ETIME` (62) - Timer expired
  - [ ] `ENOSR` (63) - Out of streams resources
  - [ ] `ENOLINK` (67) - Link has been severed
  - [ ] `EPROTO` (71) - Protocol error
  - [ ] `EBADMSG` (74) - Not a data message
  - [ ] `EOVERFLOW` (75) - Value too large
  - [ ] `EILSEQ` (84) - Illegal byte sequence
  - [ ] `ENOTSOCK` (88) - Socket operation on non-socket
  - [ ] `EDESTADDRREQ` (89) - Destination address required
  - [ ] `EMSGSIZE` (90) - Message too long
  - [ ] `EPROTOTYPE` (91) - Protocol wrong type
  - [ ] `ENOPROTOOPT` (92) - Protocol not available
  - [ ] `EPROTONOSUPPORT` (93) - Protocol not supported
  - [ ] `ESOCKTNOSUPPORT` (94) - Socket type not supported
  - [ ] `EOPNOTSUPP` (95) - Operation not supported
  - [ ] `EPFNOSUPPORT` (96) - Protocol family not supported
  - [ ] `EAFNOSUPPORT` (97) - Address family not supported
  - [ ] `EADDRINUSE` (98) - Address already in use
  - [ ] `EADDRNOTAVAIL` (99) - Cannot assign requested address
  - [ ] `ENETDOWN` (100) - Network is down
  - [ ] `ENETUNREACH` (101) - Network is unreachable
  - [ ] `ENETRESET` (102) - Network dropped connection
  - [ ] `ECONNABORTED` (103) - Connection aborted
  - [ ] `ECONNRESET` (104) - Connection reset by peer
  - [ ] `ENOBUFS` (105) - No buffer space available
  - [ ] `EISCONN` (106) - Already connected
  - [ ] `ENOTCONN` (107) - Not connected
  - [ ] `ESHUTDOWN` (108) - Cannot send after shutdown
  - [ ] `ETIMEDOUT` (110) - Connection timed out
  - [ ] `ECONNREFUSED` (111) - Connection refused
  - [ ] `EHOSTDOWN` (112) - Host is down
  - [ ] `EHOSTUNREACH` (113) - No route to host
  - [ ] `EALREADY` (114) - Operation already in progress
  - [ ] `EINPROGRESS` (115) - Operation now in progress
  - [ ] `ESTALE` (116) - Stale file handle
  - [ ] `ECANCELED` (125) - Operation canceled
  - [ ] `EOWNERDEAD` (130) - Owner died
  - [ ] `ENOTRECOVERABLE` (131) - State not recoverable

### Assertions (assert.h)
- [ ] `assert` - Assert condition
- [ ] `static_assert` - Compile-time assert (C11)
- [ ] `NDEBUG` handling

### Signal Handling (signal.h)
#### Signal Numbers
- [ ] `SIGHUP` (1) - Hangup
- [ ] `SIGINT` (2) - Interrupt (Ctrl+C)
- [ ] `SIGQUIT` (3) - Quit (Ctrl+\)
- [ ] `SIGILL` (4) - Illegal instruction
- [ ] `SIGTRAP` (5) - Trace trap
- [ ] `SIGABRT` (6) - Abort
- [ ] `SIGBUS` (7) - Bus error
- [ ] `SIGFPE` (8) - Floating point exception
- [ ] `SIGKILL` (9) - Kill (unblockable)
- [ ] `SIGUSR1` (10) - User signal 1
- [ ] `SIGSEGV` (11) - Segmentation fault
- [ ] `SIGUSR2` (12) - User signal 2
- [ ] `SIGPIPE` (13) - Broken pipe
- [ ] `SIGALRM` (14) - Alarm clock
- [ ] `SIGTERM` (15) - Termination
- [ ] `SIGSTKFLT` (16) - Stack fault
- [ ] `SIGCHLD` (17) - Child status changed
- [ ] `SIGCONT` (18) - Continue
- [ ] `SIGSTOP` (19) - Stop (unblockable)
- [ ] `SIGTSTP` (20) - Stop (Ctrl+Z)
- [ ] `SIGTTIN` (21) - Background read from tty
- [ ] `SIGTTOU` (22) - Background write to tty
- [ ] `SIGURG` (23) - Urgent data on socket
- [ ] `SIGXCPU` (24) - CPU time limit exceeded
- [ ] `SIGXFSZ` (25) - File size limit exceeded
- [ ] `SIGVTALRM` (26) - Virtual alarm clock
- [ ] `SIGPROF` (27) - Profiling alarm clock
- [ ] `SIGWINCH` (28) - Window size change
- [ ] `SIGIO` (29) - I/O now possible
- [ ] `SIGPWR` (30) - Power failure
- [ ] `SIGSYS` (31) - Bad system call

#### Functions
- [ ] `signal` - Set signal handler (simple)
- [ ] `raise` - Send signal to self
- [ ] `sigaction` - Set signal handler (full)
- [ ] `sigprocmask` - Block/unblock signals
- [ ] `sigpending` - Get pending signals
- [ ] `sigsuspend` - Wait for signal
- [ ] `sigwait` - Wait for signal (synchronous)
- [ ] `sigwaitinfo` - Wait with info
- [ ] `sigtimedwait` - Wait with timeout
- [ ] `kill` - Send signal to process
- [ ] `killpg` - Send signal to process group
- [ ] `sigqueue` - Queue signal with data
- [ ] `sigemptyset` - Empty signal set
- [ ] `sigfillset` - Fill signal set
- [ ] `sigaddset` - Add signal to set
- [ ] `sigdelset` - Remove signal from set
- [ ] `sigismember` - Test signal in set

### Time (time.h)
#### Types
- [ ] `time_t` - Time type
- [ ] `clock_t` - Clock type
- [ ] `struct tm` - Broken-down time
- [ ] `struct timespec` - Time with nanoseconds

#### Functions
- [ ] `time` - Get current time
- [ ] `difftime` - Difference between times
- [ ] `mktime` - Convert tm to time_t
- [ ] `strftime` - Format time to string
- [ ] `strptime` - Parse time from string
- [ ] `gmtime` - Convert to UTC tm
- [ ] `gmtime_r` - Convert to UTC tm (reentrant)
- [ ] `localtime` - Convert to local tm
- [ ] `localtime_r` - Convert to local tm (reentrant)
- [ ] `asctime` - Convert tm to string
- [ ] `asctime_r` - Convert tm to string (reentrant)
- [ ] `ctime` - Convert time_t to string
- [ ] `ctime_r` - Convert time_t to string (reentrant)
- [ ] `clock` - Get processor time
- [ ] `clock_gettime` - Get clock time
- [ ] `clock_settime` - Set clock time
- [ ] `clock_getres` - Get clock resolution
- [ ] `nanosleep` - High-resolution sleep
- [ ] `clock_nanosleep` - Sleep on specific clock
- [ ] `timespec_get` - Get timespec (C11)

### POSIX/Unix Functions (unistd.h)
#### Process
- [ ] `fork` - Create child process
- [ ] `vfork` - Create child (shared memory)
- [ ] `execve` - Execute program
- [ ] `execl`, `execle`, `execlp` - Execute variants
- [ ] `execv`, `execvp`, `execvpe` - Execute variants
- [ ] `_exit` - Exit immediately
- [ ] `getpid` - Get process ID
- [ ] `getppid` - Get parent process ID
- [ ] `getpgid` - Get process group ID
- [ ] `setpgid` - Set process group ID
- [ ] `getpgrp` - Get process group
- [ ] `setpgrp` - Set process group
- [ ] `getsid` - Get session ID
- [ ] `setsid` - Create session
- [ ] `getuid` - Get user ID
- [ ] `geteuid` - Get effective user ID
- [ ] `setuid` - Set user ID
- [ ] `seteuid` - Set effective user ID
- [ ] `setreuid` - Set real and effective UID
- [ ] `getgid` - Get group ID
- [ ] `getegid` - Get effective group ID
- [ ] `setgid` - Set group ID
- [ ] `setegid` - Set effective group ID
- [ ] `setregid` - Set real and effective GID
- [ ] `getgroups` - Get supplementary groups
- [ ] `setgroups` - Set supplementary groups

#### File Descriptors
- [ ] `read` - Read from fd
- [ ] `write` - Write to fd
- [ ] `close` - Close fd
- [ ] `lseek` - Seek in fd
- [ ] `dup` - Duplicate fd
- [ ] `dup2` - Duplicate fd to specific number
- [ ] `dup3` - Duplicate fd with flags
- [ ] `pipe` - Create pipe
- [ ] `pipe2` - Create pipe with flags
- [ ] `pread` - Read at offset
- [ ] `pwrite` - Write at offset
- [ ] `readv` - Read to multiple buffers
- [ ] `writev` - Write from multiple buffers
- [ ] `fsync` - Sync fd to disk
- [ ] `fdatasync` - Sync fd data to disk
- [ ] `ftruncate` - Truncate fd
- [ ] `isatty` - Is fd a tty

#### File System
- [ ] `access` - Check file access
- [ ] `faccessat` - Check file access (relative)
- [ ] `chdir` - Change directory
- [ ] `fchdir` - Change directory (by fd)
- [ ] `getcwd` - Get current directory
- [ ] `chown` - Change file owner
- [ ] `fchown` - Change file owner (by fd)
- [ ] `fchownat` - Change file owner (relative)
- [ ] `lchown` - Change symlink owner
- [ ] `link` - Create hard link
- [ ] `linkat` - Create hard link (relative)
- [ ] `symlink` - Create symbolic link
- [ ] `symlinkat` - Create symbolic link (relative)
- [ ] `readlink` - Read symbolic link
- [ ] `readlinkat` - Read symbolic link (relative)
- [ ] `unlink` - Remove file
- [ ] `unlinkat` - Remove file (relative)
- [ ] `rmdir` - Remove directory
- [ ] `truncate` - Truncate file

#### Misc
- [ ] `sleep` - Sleep seconds
- [ ] `usleep` - Sleep microseconds
- [ ] `alarm` - Set alarm
- [ ] `pause` - Wait for signal
- [ ] `sysconf` - Get system configuration
- [ ] `pathconf` - Get path configuration
- [ ] `fpathconf` - Get path configuration (by fd)
- [ ] `confstr` - Get configuration string
- [ ] `gethostname` - Get hostname
- [ ] `sethostname` - Set hostname
- [ ] `getlogin` - Get login name
- [ ] `getlogin_r` - Get login name (reentrant)
- [ ] `ttyname` - Get tty name
- [ ] `ttyname_r` - Get tty name (reentrant)
- [ ] `ctermid` - Get controlling terminal
- [ ] `sync` - Sync all filesystems

### File Control (fcntl.h)
- [ ] `open` - Open file
- [ ] `openat` - Open file (relative)
- [ ] `creat` - Create file
- [ ] `fcntl` - File control operations
  - [ ] `F_DUPFD` - Duplicate fd
  - [ ] `F_DUPFD_CLOEXEC` - Duplicate with close-on-exec
  - [ ] `F_GETFD` - Get fd flags
  - [ ] `F_SETFD` - Set fd flags
  - [ ] `F_GETFL` - Get file status flags
  - [ ] `F_SETFL` - Set file status flags
  - [ ] `F_GETLK` - Get record lock
  - [ ] `F_SETLK` - Set record lock
  - [ ] `F_SETLKW` - Set record lock (wait)
- [ ] Open flags: `O_RDONLY`, `O_WRONLY`, `O_RDWR`, `O_CREAT`, `O_EXCL`, `O_TRUNC`, `O_APPEND`, `O_NONBLOCK`, `O_SYNC`, `O_CLOEXEC`, `O_DIRECTORY`, `O_NOFOLLOW`

### File Status (sys/stat.h)
- [ ] `stat` - Get file status
- [ ] `fstat` - Get file status (by fd)
- [ ] `lstat` - Get file status (no follow)
- [ ] `fstatat` - Get file status (relative)
- [ ] `chmod` - Change file mode
- [ ] `fchmod` - Change file mode (by fd)
- [ ] `fchmodat` - Change file mode (relative)
- [ ] `mkdir` - Create directory
- [ ] `mkdirat` - Create directory (relative)
- [ ] `mkfifo` - Create FIFO
- [ ] `mkfifoat` - Create FIFO (relative)
- [ ] `mknod` - Create special file
- [ ] `mknodat` - Create special file (relative)
- [ ] `umask` - Set file mode mask
- [ ] Mode macros: `S_ISREG`, `S_ISDIR`, `S_ISCHR`, `S_ISBLK`, `S_ISFIFO`, `S_ISLNK`, `S_ISSOCK`
- [ ] Permission macros: `S_IRWXU`, `S_IRUSR`, `S_IWUSR`, `S_IXUSR`, `S_IRWXG`, `S_IRGRP`, `S_IWGRP`, `S_IXGRP`, `S_IRWXO`, `S_IROTH`, `S_IWOTH`, `S_IXOTH`, `S_ISUID`, `S_ISGID`, `S_ISVTX`

### Directory Operations (dirent.h)
- [ ] `DIR` type
- [ ] `struct dirent` - d_ino, d_name, d_type
- [ ] `opendir` - Open directory
- [ ] `fdopendir` - Open directory (from fd)
- [ ] `readdir` - Read directory entry
- [ ] `readdir_r` - Read directory entry (reentrant)
- [ ] `rewinddir` - Rewind directory
- [ ] `closedir` - Close directory
- [ ] `telldir` - Get directory position
- [ ] `seekdir` - Set directory position
- [ ] `scandir` - Scan directory
- [ ] `alphasort` - Alphabetical sort for scandir

### Process Wait (sys/wait.h)
- [ ] `wait` - Wait for any child
- [ ] `waitpid` - Wait for specific child
- [ ] `waitid` - Wait with options
- [ ] `wait3` - Wait with rusage
- [ ] `wait4` - Wait with rusage (specific)
- [ ] Status macros: `WIFEXITED`, `WEXITSTATUS`, `WIFSIGNALED`, `WTERMSIG`, `WCOREDUMP`, `WIFSTOPPED`, `WSTOPSIG`, `WIFCONTINUED`
- [ ] Option flags: `WNOHANG`, `WUNTRACED`, `WCONTINUED`

### Memory Mapping (sys/mman.h)
- [ ] `mmap` - Map memory
- [ ] `munmap` - Unmap memory
- [ ] `mprotect` - Change memory protection
- [ ] `msync` - Sync mapped memory
- [ ] `mlock` - Lock memory
- [ ] `munlock` - Unlock memory
- [ ] `mlockall` - Lock all memory
- [ ] `munlockall` - Unlock all memory
- [ ] `madvise` - Memory advice
- [ ] `posix_madvise` - Memory advice (POSIX)
- [ ] `shm_open` - Open shared memory
- [ ] `shm_unlink` - Remove shared memory
- [ ] Protection flags: `PROT_NONE`, `PROT_READ`, `PROT_WRITE`, `PROT_EXEC`
- [ ] Map flags: `MAP_SHARED`, `MAP_PRIVATE`, `MAP_FIXED`, `MAP_ANONYMOUS`, `MAP_FAILED`

### I/O Control (sys/ioctl.h)
- [ ] `ioctl` - Device control
- [ ] Terminal ioctls (TCGETS, TCSETS, TIOCGWINSZ, TIOCSCTTY, etc.)

### Terminal I/O (termios.h)
- [ ] `struct termios` - Terminal attributes
- [ ] `struct winsize` - Window size
- [ ] `tcgetattr` - Get terminal attributes
- [ ] `tcsetattr` - Set terminal attributes
- [ ] `tcsendbreak` - Send break
- [ ] `tcdrain` - Wait for output
- [ ] `tcflush` - Flush I/O
- [ ] `tcflow` - Flow control
- [ ] `cfgetispeed` - Get input speed
- [ ] `cfgetospeed` - Get output speed
- [ ] `cfsetispeed` - Set input speed
- [ ] `cfsetospeed` - Set output speed
- [ ] `cfmakeraw` - Set raw mode
- [ ] `tcgetsid` - Get session ID
- [ ] Control characters: `VEOF`, `VEOL`, `VERASE`, `VINTR`, `VKILL`, `VMIN`, `VQUIT`, `VSTART`, `VSTOP`, `VSUSP`, `VTIME`
- [ ] Input flags: `IGNBRK`, `BRKINT`, `IGNPAR`, `PARMRK`, `INPCK`, `ISTRIP`, `INLCR`, `IGNCR`, `ICRNL`, `IXON`, `IXOFF`, `IXANY`
- [ ] Output flags: `OPOST`, `ONLCR`, `OCRNL`, `ONOCR`, `ONLRET`
- [ ] Control flags: `CSIZE`, `CS5`, `CS6`, `CS7`, `CS8`, `CSTOPB`, `CREAD`, `PARENB`, `PARODD`, `HUPCL`, `CLOCAL`
- [ ] Local flags: `ISIG`, `ICANON`, `ECHO`, `ECHOE`, `ECHOK`, `ECHONL`, `NOFLSH`, `TOSTOP`, `IEXTEN`

### Resource Limits (sys/resource.h)
- [ ] `struct rlimit` - Resource limit
- [ ] `struct rusage` - Resource usage
- [ ] `getrlimit` - Get resource limit
- [ ] `setrlimit` - Set resource limit
- [ ] `getrusage` - Get resource usage
- [ ] `getpriority` - Get scheduling priority
- [ ] `setpriority` - Set scheduling priority
- [ ] Limits: `RLIMIT_AS`, `RLIMIT_CORE`, `RLIMIT_CPU`, `RLIMIT_DATA`, `RLIMIT_FSIZE`, `RLIMIT_NOFILE`, `RLIMIT_STACK`, `RLIMIT_NPROC`

### System Information (sys/utsname.h)
- [ ] `struct utsname` - System name structure
- [ ] `uname` - Get system name

### Socket Programming (sys/socket.h, netinet/in.h, arpa/inet.h)
#### Socket Functions
- [ ] `socket` - Create socket
- [ ] `bind` - Bind socket to address
- [ ] `listen` - Listen for connections
- [ ] `accept` - Accept connection
- [ ] `accept4` - Accept with flags
- [ ] `connect` - Connect to server
- [ ] `send` - Send data
- [ ] `recv` - Receive data
- [ ] `sendto` - Send to address
- [ ] `recvfrom` - Receive from address
- [ ] `sendmsg` - Send message
- [ ] `recvmsg` - Receive message
- [ ] `shutdown` - Shutdown socket
- [ ] `getsockopt` - Get socket option
- [ ] `setsockopt` - Set socket option
- [ ] `getsockname` - Get socket address
- [ ] `getpeername` - Get peer address
- [ ] `socketpair` - Create socket pair

#### Address Functions
- [ ] `inet_addr` - String to network address
- [ ] `inet_ntoa` - Network address to string
- [ ] `inet_pton` - String to network (protocol)
- [ ] `inet_ntop` - Network to string (protocol)
- [ ] `htons` - Host to network short
- [ ] `htonl` - Host to network long
- [ ] `ntohs` - Network to host short
- [ ] `ntohl` - Network to host long

#### Name Resolution (netdb.h)
- [ ] `gethostbyname` - Get host by name
- [ ] `gethostbyaddr` - Get host by address
- [ ] `getaddrinfo` - Get address info
- [ ] `freeaddrinfo` - Free address info
- [ ] `getnameinfo` - Get name info
- [ ] `gai_strerror` - Address info error string
- [ ] `getservbyname` - Get service by name
- [ ] `getservbyport` - Get service by port

### I/O Multiplexing (sys/select.h, poll.h)
- [ ] `select` - Synchronous I/O multiplexing
- [ ] `pselect` - Select with signal mask
- [ ] `poll` - Poll file descriptors
- [ ] `ppoll` - Poll with signal mask
- [ ] `FD_CLR`, `FD_ISSET`, `FD_SET`, `FD_ZERO` macros
- [ ] `struct pollfd`, `POLLIN`, `POLLOUT`, `POLLERR`, `POLLHUP`

### Locale (locale.h)
- [ ] `struct lconv` - Locale conventions
- [ ] `setlocale` - Set locale
- [ ] `localeconv` - Get locale conventions
- [ ] Categories: `LC_ALL`, `LC_COLLATE`, `LC_CTYPE`, `LC_MESSAGES`, `LC_MONETARY`, `LC_NUMERIC`, `LC_TIME`

### Setjmp/Longjmp (setjmp.h)
- [ ] `jmp_buf` type
- [ ] `sigjmp_buf` type
- [ ] `setjmp` - Save environment
- [ ] `longjmp` - Restore environment
- [ ] `sigsetjmp` - Save environment with signals
- [ ] `siglongjmp` - Restore environment with signals

### Regular Expressions (regex.h)
- [ ] `regex_t` type
- [ ] `regmatch_t` type
- [ ] `regcomp` - Compile regex
- [ ] `regexec` - Execute regex
- [ ] `regerror` - Get error message
- [ ] `regfree` - Free regex
- [ ] Flags: `REG_EXTENDED`, `REG_ICASE`, `REG_NOSUB`, `REG_NEWLINE`

### Threads (pthread.h) - Optional
- [ ] `pthread_t` type
- [ ] `pthread_create` - Create thread
- [ ] `pthread_exit` - Exit thread
- [ ] `pthread_join` - Join thread
- [ ] `pthread_detach` - Detach thread
- [ ] `pthread_self` - Get thread ID
- [ ] `pthread_equal` - Compare thread IDs
- [ ] `pthread_mutex_*` - Mutex operations
- [ ] `pthread_cond_*` - Condition variable operations
- [ ] `pthread_rwlock_*` - Read-write lock operations
- [ ] `pthread_key_*` - Thread-local storage
- [ ] `pthread_once` - One-time initialization

---

## Math Library (libm)

### Basic Operations
- [ ] `fabs` / `fabsf` / `fabsl` - Absolute value
- [ ] `fmod` / `fmodf` / `fmodl` - Floating-point remainder
- [ ] `remainder` / `remainderf` / `remainderl` - IEEE remainder
- [ ] `remquo` / `remquof` / `remquol` - Remainder and quotient
- [ ] `fma` / `fmaf` / `fmal` - Fused multiply-add
- [ ] `fmax` / `fmaxf` / `fmaxl` - Maximum
- [ ] `fmin` / `fminf` / `fminl` - Minimum
- [ ] `fdim` / `fdimf` / `fdiml` - Positive difference
- [ ] `nan` / `nanf` / `nanl` - Generate NaN

### Exponential Functions
- [ ] `exp` / `expf` / `expl` - e^x
- [ ] `exp2` / `exp2f` / `exp2l` - 2^x
- [ ] `expm1` / `expm1f` / `expm1l` - e^x - 1
- [ ] `log` / `logf` / `logl` - Natural logarithm
- [ ] `log2` / `log2f` / `log2l` - Base-2 logarithm
- [ ] `log10` / `log10f` / `log10l` - Base-10 logarithm
- [ ] `log1p` / `log1pf` / `log1pl` - ln(1 + x)
- [ ] `ilogb` / `ilogbf` / `ilogbl` - Extract exponent
- [ ] `logb` / `logbf` / `logbl` - Extract exponent (float)

### Power Functions
- [ ] `pow` / `powf` / `powl` - x^y
- [ ] `sqrt` / `sqrtf` / `sqrtl` - Square root
- [ ] `cbrt` / `cbrtf` / `cbrtl` - Cube root
- [ ] `hypot` / `hypotf` / `hypotl` - Hypotenuse

### Trigonometric Functions
- [ ] `sin` / `sinf` / `sinl` - Sine
- [ ] `cos` / `cosf` / `cosl` - Cosine
- [ ] `tan` / `tanf` / `tanl` - Tangent
- [ ] `asin` / `asinf` / `asinl` - Arc sine
- [ ] `acos` / `acosf` / `acosl` - Arc cosine
- [ ] `atan` / `atanf` / `atanl` - Arc tangent
- [ ] `atan2` / `atan2f` / `atan2l` - Arc tangent (y/x)
- [ ] `sincos` / `sincosf` / `sincosl` - Sine and cosine (GNU extension)

### Hyperbolic Functions
- [ ] `sinh` / `sinhf` / `sinhl` - Hyperbolic sine
- [ ] `cosh` / `coshf` / `coshl` - Hyperbolic cosine
- [ ] `tanh` / `tanhf` / `tanhl` - Hyperbolic tangent
- [ ] `asinh` / `asinhf` / `asinhl` - Inverse hyperbolic sine
- [ ] `acosh` / `acoshf` / `acoshl` - Inverse hyperbolic cosine
- [ ] `atanh` / `atanhf` / `atanhl` - Inverse hyperbolic tangent

### Error and Gamma Functions
- [ ] `erf` / `erff` / `erfl` - Error function
- [ ] `erfc` / `erfcf` / `erfcl` - Complementary error function
- [ ] `lgamma` / `lgammaf` / `lgammal` - Log gamma
- [ ] `tgamma` / `tgammaf` / `tgammal` - Gamma function

### Rounding Functions
- [ ] `ceil` / `ceilf` / `ceill` - Round up
- [ ] `floor` / `floorf` / `floorl` - Round down
- [ ] `trunc` / `truncf` / `truncl` - Round toward zero
- [ ] `round` / `roundf` / `roundl` - Round to nearest
- [ ] `lround` / `lroundf` / `lroundl` - Round to long
- [ ] `llround` / `llroundf` / `llroundl` - Round to long long
- [ ] `nearbyint` / `nearbyintf` / `nearbyintl` - Round to nearest (no FE_INEXACT)
- [ ] `rint` / `rintf` / `rintl` - Round to nearest integer
- [ ] `lrint` / `lrintf` / `lrintl` - Round to long
- [ ] `llrint` / `llrintf` / `llrintl` - Round to long long

### Manipulation Functions
- [ ] `frexp` / `frexpf` / `frexpl` - Extract mantissa and exponent
- [ ] `ldexp` / `ldexpf` / `ldexpl` - Load exponent
- [ ] `modf` / `modff` / `modfl` - Extract integer and fractional parts
- [ ] `scalbn` / `scalbnf` / `scalbnl` - Scale by power of radix
- [ ] `scalbln` / `scalblnf` / `scalblnl` - Scale by power of radix (long)
- [ ] `copysign` / `copysignf` / `copysignl` - Copy sign
- [ ] `nextafter` / `nextafterf` / `nextafterl` - Next representable value
- [ ] `nexttoward` / `nexttowardf` / `nexttowardl` - Next toward value

### Classification Functions
- [ ] `fpclassify` - Classify floating-point value
- [ ] `isfinite` - Is finite
- [ ] `isinf` - Is infinite
- [ ] `isnan` - Is NaN
- [ ] `isnormal` - Is normal
- [ ] `signbit` - Sign bit

### Comparison Functions
- [ ] `isgreater` - Greater than (no exceptions)
- [ ] `isgreaterequal` - Greater or equal
- [ ] `isless` - Less than
- [ ] `islessequal` - Less or equal
- [ ] `islessgreater` - Less or greater
- [ ] `isunordered` - Unordered comparison

### Bessel Functions (Optional)
- [ ] `j0` / `j0f` - Bessel J0
- [ ] `j1` / `j1f` - Bessel J1
- [ ] `jn` / `jnf` - Bessel Jn
- [ ] `y0` / `y0f` - Bessel Y0
- [ ] `y1` / `y1f` - Bessel Y1
- [ ] `yn` / `ynf` - Bessel Yn

### Constants (math.h)
- [ ] `M_E` - e
- [ ] `M_LOG2E` - log2(e)
- [ ] `M_LOG10E` - log10(e)
- [ ] `M_LN2` - ln(2)
- [ ] `M_LN10` - ln(10)
- [ ] `M_PI` - pi
- [ ] `M_PI_2` - pi/2
- [ ] `M_PI_4` - pi/4
- [ ] `M_1_PI` - 1/pi
- [ ] `M_2_PI` - 2/pi
- [ ] `M_2_SQRTPI` - 2/sqrt(pi)
- [ ] `M_SQRT2` - sqrt(2)
- [ ] `M_SQRT1_2` - 1/sqrt(2)
- [ ] `HUGE_VAL` / `HUGE_VALF` / `HUGE_VALL`
- [ ] `INFINITY`
- [ ] `NAN`

### Complex Numbers (complex.h) - Optional
- [ ] `_Complex` type support
- [ ] `creal` / `crealf` / `creall` - Real part
- [ ] `cimag` / `cimagf` / `cimagl` - Imaginary part
- [ ] `cabs` / `cabsf` / `cabsl` - Absolute value
- [ ] `carg` / `cargf` / `cargl` - Argument
- [ ] `conj` / `conjf` / `conjl` - Conjugate
- [ ] `cproj` / `cprojf` / `cprojl` - Projection
- [ ] `cexp` / `cexpf` / `cexpl` - Complex exponential
- [ ] `clog` / `clogf` / `clogl` - Complex logarithm
- [ ] `cpow` / `cpowf` / `cpowl` - Complex power
- [ ] `csqrt` / `csqrtf` / `csqrtl` - Complex square root
- [ ] `csin` / `csinf` / `csinl` - Complex sine
- [ ] `ccos` / `ccosf` / `ccosl` - Complex cosine
- [ ] `ctan` / `ctanf` / `ctanl` - Complex tangent
- [ ] `casin` / `casinf` / `casinl` - Complex arc sine
- [ ] `cacos` / `cacosf` / `cacosl` - Complex arc cosine
- [ ] `catan` / `catanf` / `catanl` - Complex arc tangent
- [ ] `csinh` / `csinhf` / `csinhl` - Complex hyperbolic sine
- [ ] `ccosh` / `ccoshf` / `ccoshl` - Complex hyperbolic cosine
- [ ] `ctanh` / `ctanhf` / `ctanhl` - Complex hyperbolic tangent
- [ ] `casinh` / `casinhf` / `casinhl` - Complex inverse hyperbolic sine
- [ ] `cacosh` / `cacoshf` / `cacoshl` - Complex inverse hyperbolic cosine
- [ ] `catanh` / `catanhf` / `catanhl` - Complex inverse hyperbolic tangent

---

## Utility Suite

### Coreutils - File Operations
- [ ] `cp` - Copy files and directories
  - [ ] `-r` recursive
  - [ ] `-i` interactive
  - [ ] `-f` force
  - [ ] `-p` preserve attributes
  - [ ] `-v` verbose
- [ ] `mv` - Move/rename files
  - [ ] `-i` interactive
  - [ ] `-f` force
  - [ ] `-v` verbose
- [ ] `rm` - Remove files
  - [ ] `-r` recursive
  - [ ] `-f` force
  - [ ] `-i` interactive
  - [ ] `-v` verbose
- [ ] `ln` - Create links
  - [ ] `-s` symbolic
  - [ ] `-f` force
- [ ] `mkdir` - Create directories
  - [ ] `-p` parents
  - [ ] `-m` mode
- [ ] `rmdir` - Remove empty directories
  - [ ] `-p` parents
- [ ] `touch` - Update timestamps / create file
  - [ ] `-a` access time only
  - [ ] `-m` modification time only
  - [ ] `-t` specific time
- [ ] `install` - Copy files and set attributes

### Coreutils - File Information
- [ ] `ls` - List directory contents
  - [ ] `-l` long format
  - [ ] `-a` all (including hidden)
  - [ ] `-h` human-readable sizes
  - [ ] `-R` recursive
  - [ ] `-t` sort by time
  - [ ] `-S` sort by size
  - [ ] `-r` reverse
  - [ ] `-i` inode
  - [ ] `-d` directory only
  - [ ] `--color` colorized output
- [ ] `stat` - Display file status
- [ ] `file` - Determine file type
- [ ] `du` - Disk usage
  - [ ] `-h` human-readable
  - [ ] `-s` summary
  - [ ] `-a` all files
- [ ] `df` - Filesystem disk space
  - [ ] `-h` human-readable
  - [ ] `-i` inodes
- [ ] `pwd` - Print working directory
- [ ] `readlink` - Print resolved symbolic link
- [ ] `realpath` - Print resolved absolute path
- [ ] `basename` - Strip directory from path
- [ ] `dirname` - Strip filename from path

### Coreutils - Text Output
- [ ] `echo` - Display text
  - [ ] `-n` no newline
  - [ ] `-e` escape sequences
- [ ] `printf` - Formatted output
- [ ] `yes` - Output string repeatedly
- [ ] `cat` - Concatenate files
  - [ ] `-n` number lines
  - [ ] `-b` number non-blank lines
  - [ ] `-s` squeeze blank lines
- [ ] `tac` - Concatenate in reverse
- [ ] `head` - Output first part
  - [ ] `-n` number of lines
  - [ ] `-c` number of bytes
- [ ] `tail` - Output last part
  - [ ] `-n` number of lines
  - [ ] `-c` number of bytes
  - [ ] `-f` follow
- [ ] `nl` - Number lines
- [ ] `wc` - Word/line/char count
  - [ ] `-l` lines
  - [ ] `-w` words
  - [ ] `-c` bytes
  - [ ] `-m` characters

### Coreutils - Text Processing
- [ ] `sort` - Sort lines
  - [ ] `-r` reverse
  - [ ] `-n` numeric
  - [ ] `-k` key field
  - [ ] `-t` field separator
  - [ ] `-u` unique
- [ ] `uniq` - Report/omit repeated lines
  - [ ] `-c` count
  - [ ] `-d` duplicates only
  - [ ] `-u` unique only
- [ ] `cut` - Remove sections from lines
  - [ ] `-f` fields
  - [ ] `-d` delimiter
  - [ ] `-c` characters
  - [ ] `-b` bytes
- [ ] `paste` - Merge lines of files
  - [ ] `-d` delimiter
- [ ] `join` - Join lines on common field
- [ ] `tr` - Translate characters
  - [ ] `-d` delete
  - [ ] `-s` squeeze
  - [ ] `-c` complement
- [ ] `fold` - Wrap lines to width
  - [ ] `-w` width
  - [ ] `-s` break at spaces
- [ ] `fmt` - Simple text formatter
- [ ] `expand` - Convert tabs to spaces
- [ ] `unexpand` - Convert spaces to tabs
- [ ] `rev` - Reverse lines
- [ ] `tee` - Read stdin, write stdout and files
  - [ ] `-a` append
- [ ] `split` - Split file into pieces
- [ ] `csplit` - Split file at context

### Coreutils - Search
- [ ] `grep` - Search for patterns
  - [ ] `-i` ignore case
  - [ ] `-v` invert match
  - [ ] `-n` line numbers
  - [ ] `-c` count
  - [ ] `-l` files with matches
  - [ ] `-r` recursive
  - [ ] `-E` extended regex
  - [ ] `-F` fixed strings
  - [ ] `-w` word match
  - [ ] `-o` only matching
  - [ ] `-A` after context
  - [ ] `-B` before context
  - [ ] `-C` context
- [ ] `egrep` - Extended grep (grep -E)
- [ ] `fgrep` - Fixed string grep (grep -F)
- [ ] `find` - Search for files
  - [ ] `-name` name pattern
  - [ ] `-type` file type
  - [ ] `-size` file size
  - [ ] `-mtime` modification time
  - [ ] `-exec` execute command
  - [ ] `-print` print path
  - [ ] `-maxdepth` max depth
- [ ] `xargs` - Build command lines from stdin
  - [ ] `-n` max args
  - [ ] `-I` replace string
  - [ ] `-0` null separator
- [ ] `locate` - Find files by name (requires database)
- [ ] `which` - Locate command
- [ ] `whereis` - Locate binary/source/manual

### Coreutils - Comparison
- [ ] `cmp` - Compare bytes
  - [ ] `-l` verbose
  - [ ] `-s` silent
- [ ] `diff` - Compare files line by line
  - [ ] `-u` unified format
  - [ ] `-r` recursive
  - [ ] `-q` brief
- [ ] `comm` - Compare sorted files
- [ ] `patch` - Apply diff file

### Coreutils - Permissions
- [ ] `chmod` - Change file mode
  - [ ] `-R` recursive
  - [ ] Symbolic mode (u+x, g-w, etc.)
  - [ ] Octal mode (755, 644, etc.)
- [ ] `chown` - Change file owner
  - [ ] `-R` recursive
- [ ] `chgrp` - Change file group
  - [ ] `-R` recursive
- [ ] `umask` - Set file mode mask

### Coreutils - System Information
- [ ] `uname` - System information
  - [ ] `-a` all
  - [ ] `-s` kernel name
  - [ ] `-r` kernel release
  - [ ] `-m` machine
- [ ] `hostname` - System hostname
- [ ] `uptime` - System uptime
- [ ] `date` - Display/set date
  - [ ] `+FORMAT` custom format
  - [ ] `-s` set date
- [ ] `cal` - Display calendar
- [ ] `id` - User identity
- [ ] `whoami` - Current username
- [ ] `groups` - User groups
- [ ] `logname` - Login name
- [ ] `users` - Logged in users
- [ ] `who` - Who is logged in
- [ ] `w` - Who and what they're doing

### Coreutils - Misc
- [ ] `sleep` - Delay execution
- [ ] `true` - Exit with success
- [ ] `false` - Exit with failure
- [ ] `test` / `[` - Evaluate expression
- [ ] `expr` - Evaluate expression
- [ ] `seq` - Print number sequence
- [ ] `env` - Run with modified environment
- [ ] `printenv` - Print environment
- [ ] `nohup` - Run immune to hangups
- [ ] `timeout` - Run with time limit
- [ ] `nice` - Run with modified priority
- [ ] `nproc` - Number of processors
- [ ] `arch` - Print machine architecture
- [ ] `md5sum` - Compute MD5 checksum
- [ ] `sha1sum` - Compute SHA1 checksum
- [ ] `sha256sum` - Compute SHA256 checksum
- [ ] `base64` - Base64 encode/decode
- [ ] `od` - Octal dump
- [ ] `hexdump` - Hex dump
- [ ] `xxd` - Hex dump (vi style)

### Fileutils (Low-Level)
- [ ] `dd` - Convert and copy file
  - [ ] `if=` input file
  - [ ] `of=` output file
  - [ ] `bs=` block size
  - [ ] `count=` blocks
  - [ ] `skip=` skip input blocks
  - [ ] `seek=` skip output blocks
- [ ] `sync` - Sync filesystems
- [ ] `mkfifo` - Create named pipe
- [ ] `mknod` - Create special file

### Process Utilities
- [ ] `ps` - Process status
  - [ ] `-e` all processes
  - [ ] `-f` full format
  - [ ] `-u` user format
  - [ ] `-a` all with tty
  - [ ] `aux` BSD style
- [ ] `top` - Dynamic process viewer
- [ ] `kill` - Send signal to process
  - [ ] `-l` list signals
  - [ ] `-9` SIGKILL
  - [ ] `-15` SIGTERM
- [ ] `killall` - Kill by name
- [ ] `pkill` - Kill by pattern
- [ ] `pgrep` - Find by pattern
- [ ] `pidof` - Find PID by name
- [ ] `jobs` - List jobs (shell builtin)
- [ ] `fg` - Foreground job (shell builtin)
- [ ] `bg` - Background job (shell builtin)
- [ ] `wait` - Wait for process
- [ ] `time` - Time command execution
- [ ] `watch` - Execute periodically

### Shell (/bin/sh)
#### Basic Features
- [ ] Command execution
- [ ] PATH searching
- [ ] Exit status ($?)
- [ ] Comments (#)

#### Quoting
- [ ] Single quotes (literal)
- [ ] Double quotes (variable expansion)
- [ ] Backslash escaping
- [ ] $'...' (ANSI-C quoting)

#### Variables
- [ ] Assignment (VAR=value)
- [ ] Expansion ($VAR, ${VAR})
- [ ] Default values (${VAR:-default}, ${VAR:=default})
- [ ] Substring (${VAR:offset:length})
- [ ] Length (${#VAR})
- [ ] Pattern removal (${VAR#pattern}, ${VAR%pattern})
- [ ] Pattern replacement (${VAR/pattern/replacement})
- [ ] Special variables ($0, $1-$9, $@, $*, $#, $$, $!, $?)
- [ ] export (environment variables)
- [ ] readonly
- [ ] unset
- [ ] local (function-local)

#### Redirection
- [ ] Output (>, >>)
- [ ] Input (<)
- [ ] Here-document (<<)
- [ ] Here-string (<<<)
- [ ] File descriptor duplication (>&, <&)
- [ ] Stderr redirect (2>, 2>&1)
- [ ] Pipe (|)
- [ ] Process substitution (<(), >()) - optional

#### Control Flow
- [ ] if/then/elif/else/fi
- [ ] case/esac
- [ ] for/do/done
- [ ] while/do/done
- [ ] until/do/done
- [ ] break, continue
- [ ] && (and), || (or)
- [ ] ! (not)
- [ ] ; (sequential)
- [ ] & (background)

#### Test Expressions
- [ ] File tests (-e, -f, -d, -r, -w, -x, -s, -L, etc.)
- [ ] String tests (-z, -n, =, !=)
- [ ] Integer tests (-eq, -ne, -lt, -le, -gt, -ge)
- [ ] Logical operators (-a, -o, !)
- [ ] [[ ]] extended test (optional)

#### Functions
- [ ] Function definition (name() { ... })
- [ ] Function calls
- [ ] return
- [ ] Local variables

#### Built-in Commands
- [ ] : (null command)
- [ ] . / source
- [ ] alias / unalias
- [ ] break
- [ ] cd
- [ ] continue
- [ ] echo
- [ ] eval
- [ ] exec
- [ ] exit
- [ ] export
- [ ] false
- [ ] getopts
- [ ] hash
- [ ] help
- [ ] history (optional)
- [ ] jobs
- [ ] kill
- [ ] let (optional)
- [ ] local
- [ ] printf
- [ ] pwd
- [ ] read
- [ ] readonly
- [ ] return
- [ ] set
- [ ] shift
- [ ] test / [
- [ ] times
- [ ] trap
- [ ] true
- [ ] type
- [ ] ulimit
- [ ] umask
- [ ] unalias
- [ ] unset
- [ ] wait

#### Job Control
- [ ] Background (&)
- [ ] Foreground (fg)
- [ ] Background (bg)
- [ ] jobs listing
- [ ] Ctrl+Z (SIGTSTP)
- [ ] Ctrl+C (SIGINT)
- [ ] disown

#### Globbing
- [ ] * (any characters)
- [ ] ? (single character)
- [ ] [...] (character class)
- [ ] [!...] / [^...] (negated class)
- [ ] ** (recursive) - optional

#### Command Substitution
- [ ] $(command)
- [ ] `command` (deprecated)

#### Arithmetic
- [ ] $((expression))
- [ ] let (optional)

#### Scripts
- [ ] Shebang (#!)
- [ ] Script arguments ($1, $2, etc.)
- [ ] Script name ($0)

### Binutils
- [ ] `as` - Assembler
  - [ ] x86_64 instruction encoding
  - [ ] Labels and symbols
  - [ ] Directives (.text, .data, .bss, .global, etc.)
  - [ ] ELF output
- [ ] `ld` - Linker
  - [ ] ELF object file linking
  - [ ] Symbol resolution
  - [ ] Relocation
  - [ ] Linker scripts
  - [ ] Static libraries
  - [ ] Shared libraries (optional)
- [ ] `ar` - Archive manager
  - [ ] Create archive (ar rcs)
  - [ ] List contents (ar t)
  - [ ] Extract (ar x)
- [ ] `ranlib` - Generate archive index
- [ ] `nm` - List symbols
  - [ ] Symbol types (T, U, D, B, etc.)
- [ ] `objdump` - Object file dumper
  - [ ] `-d` disassemble
  - [ ] `-h` section headers
  - [ ] `-t` symbol table
  - [ ] `-r` relocations
- [ ] `objcopy` - Copy and translate object files
- [ ] `readelf` - Display ELF information
  - [ ] `-h` ELF header
  - [ ] `-S` section headers
  - [ ] `-l` program headers
  - [ ] `-s` symbol table
  - [ ] `-r` relocations
  - [ ] `-d` dynamic section
- [ ] `size` - Section sizes
- [ ] `strings` - Print printable strings
- [ ] `strip` - Discard symbols
- [ ] `addr2line` - Address to file/line

### Text Editors
#### ed (Line Editor)
- [ ] Address modes (., $, n, /pattern/, ?pattern?)
- [ ] Commands: a, c, d, i, j, m, p, q, r, s, w
- [ ] Global commands (g/pattern/command)
- [ ] Undo (u)

#### vi/vim (Visual Editor)
##### Modes
- [ ] Normal mode
- [ ] Insert mode (i, a, o, O, I, A)
- [ ] Visual mode (v, V, Ctrl+V)
- [ ] Command-line mode (:)

##### Motion
- [ ] h, j, k, l (character)
- [ ] w, W, e, E, b, B (word)
- [ ] 0, ^, $ (line)
- [ ] gg, G (file)
- [ ] f, F, t, T (find char)
- [ ] /, ? (search)
- [ ] n, N (next/prev match)
- [ ] %, [[, ]], {, } (blocks)

##### Editing
- [ ] x, X (delete char)
- [ ] d + motion (delete)
- [ ] dd (delete line)
- [ ] c + motion (change)
- [ ] cc (change line)
- [ ] y + motion (yank)
- [ ] yy (yank line)
- [ ] p, P (paste)
- [ ] r (replace char)
- [ ] J (join lines)
- [ ] u, Ctrl+R (undo/redo)
- [ ] . (repeat)

##### Commands
- [ ] :w (write)
- [ ] :q (quit)
- [ ] :wq, :x, ZZ (write and quit)
- [ ] :e (edit file)
- [ ] :r (read file)
- [ ] :! (shell command)
- [ ] :s/pattern/replace/ (substitute)
- [ ] :%s (global substitute)
- [ ] :set (options)
- [ ] :help

### Init System
- [ ] `/sbin/init` - PID 1 process
  - [ ] Read configuration (/etc/inittab or /etc/init.d/)
  - [ ] Run startup scripts
  - [ ] Spawn getty on terminals
  - [ ] Reap zombie processes
  - [ ] Handle shutdown signals
- [ ] `/sbin/shutdown` - System shutdown
- [ ] `/sbin/reboot` - System reboot
- [ ] `/sbin/halt` - Halt system
- [ ] `/sbin/poweroff` - Power off
- [ ] `/bin/getty` - Terminal setup
- [ ] `/bin/login` - User login
- [ ] `/etc/passwd` - User database
- [ ] `/etc/group` - Group database
- [ ] `/etc/shadow` - Password hashes (optional)

### Network Utilities (After Kernel Networking)
- [ ] `ping` - ICMP echo
- [ ] `ifconfig` - Configure network interface
- [ ] `ip` - Network configuration (modern)
- [ ] `route` - Routing table
- [ ] `netstat` - Network statistics
- [ ] `ss` - Socket statistics
- [ ] `nc` / `netcat` - Network utility
- [ ] `telnet` - Telnet client
- [ ] `ftp` - FTP client
- [ ] `wget` - HTTP download
- [ ] `curl` - URL transfer
- [ ] `host` / `nslookup` - DNS lookup
- [ ] `traceroute` - Trace packet route

---

## Development Tools

### Compiler
- [ ] Port GCC (complex) OR
- [ ] Port TCC (Tiny C Compiler - simpler) OR
- [ ] Write minimal C compiler
  - [ ] Preprocessor (#include, #define, #ifdef, etc.)
  - [ ] Lexer
  - [ ] Parser
  - [ ] Code generator (x86_64)
  - [ ] Optimization (optional)

### Build System
- [ ] `make` - Build automation
  - [ ] Rules and targets
  - [ ] Variables
  - [ ] Pattern rules
  - [ ] Automatic variables ($@, $<, $^)
  - [ ] Include files
  - [ ] Conditionals
  - [ ] Functions

### Debugger
- [ ] `gdb` - GNU Debugger (port) OR
- [ ] Simple debugger
  - [ ] Breakpoints
  - [ ] Single-stepping
  - [ ] Register/memory inspection
  - [ ] Stack traces
  - [ ] Symbol lookup

### Other Tools
- [ ] `lex` / `flex` - Lexer generator
- [ ] `yacc` / `bison` - Parser generator
- [ ] `m4` - Macro processor
- [ ] `bc` - Calculator
- [ ] `dc` - RPN calculator

---

## Implementation Notes

### Suggested Order
1. **Freestanding headers** (stddef.h, stdint.h, stdarg.h, etc.)
2. **String functions** (memcpy, strlen, strcmp, etc.)
3. **Memory allocation** (malloc, free, realloc)
4. **Basic stdio** (printf to serial/console)
5. **ctype functions**
6. **Error handling** (errno, strerror)
7. **File operations** (open, read, write, close via syscalls)
8. **Full stdio** (FILE*, fopen, fread, etc.)
9. **Process functions** (fork, exec, wait)
10. **Signal handling**
11. **Time functions**
12. **Math library**
13. **Shell**
14. **Core utilities**
15. **Development tools**

### Testing Strategy
- Unit tests for each function
- Comparison with glibc/musl behavior
- Edge cases (NULL, empty strings, overflow)
- POSIX conformance tests

### References
- POSIX.1-2017 specification
- C11 standard (ISO/IEC 9899:2011)
- Linux man pages
- musl libc source (clean, readable)
- dietlibc source (minimal)
