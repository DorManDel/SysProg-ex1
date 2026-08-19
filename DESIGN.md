# Systems Programming — Assignment 1
## `my_cp` — File Copy with Configurable I/O Granularity

**Student:** Dor Mandel  

---

## 1. Purpose and Requirements

The purpose of `my_cp` is to implement a simplified file-copy utility using low-level POSIX file operations.

```POSIX = Portable OS Interface```

The program receives three command-line arguments:

```bash
./my_cp <source_file> <destination_file> <granularity_bytes>
```

The arguments are:

```text
argv[1] -> source file path
argv[2] -> destination file path
argv[3] -> I/O granularity in bytes
```

`argc` must therefore equal `4`, because `argv[0]` contains the executable name.

The program performs the following operations:

```text
validate arguments
        ↓
parse granularity
        ↓
allocate dynamic buffer
        ↓
open source file
        ↓
open/create destination file
        ↓
start timer
        ↓
copy file using read()/write()
        ↓
stop timer
        ↓
calculate elapsed time
        ↓
close files
        ↓
free buffer
        ↓
exit
```

### Main requirements implemented

| Requirement | Implementation |
|---|---|
| Exactly three user arguments | `argc == 4` |
| Positive integer granularity | `parse_granularity()` using `strtol()` |
| Dynamic buffer | `malloc(granularity)` |
| Source opened read-only | `O_RDONLY` |
| Destination opened for writing | `O_WRONLY` |
| Create missing destination | `O_CREAT` |
| Replace existing destination contents | `O_TRUNC` |
| Retry interrupted reads | `errno == EINTR` |
| Retry interrupted writes | `errno == EINTR` |
| Handle partial writes | `write_all()` |
| Measure only copy I/O | timer starts after `open()` and ends before `close()` |
| Release resources | `close()` and `free()` |

---

## 2. System Calls Analysis

The program uses the following main operating-system interfaces:

```text
open()
read()
write()
close()
clock_gettime()
```

The first four form the file I/O lifecycle:

```text
open
 ↓
read / write
 ↓
close
```

---

## 2.1 `open()`

### Purpose

`open()` opens a file and returns a **file descriptor** that the process can later use with operations such as `read()`, `write()`, and `close()`.

Conceptually:

```text
pathname
   ↓
open()
   ↓
system-call boundary
   ↓
kernel pathname resolution
   ↓
filesystem / inode information
   ↓
kernel open-file state
   ↓
process file-descriptor table
   ↓
integer file descriptor
```

A file descriptor is not the file itself.

It is a process-local integer that refers to kernel-managed open-file state.

Example:

```text
src_fd = 3
dst_fd = 4
```

### Source file

The source is opened using:

```c
open(path, O_RDONLY);
```

#### `O_RDONLY`

Requests read-only access.

This is sufficient because the program never modifies the source file.

If `open()` succeeds, it returns a non-negative file descriptor.

If it fails:

```text
return value = -1
errno        = reason for failure
```

The program reports the error using:

```c
perror(...)
```

### Destination file

The destination is opened using:

```c
open(
    path,
    O_WRONLY | O_CREAT | O_TRUNC,
    0644
);
```

#### `O_WRONLY`

Opens the destination for writing.

#### `O_CREAT`

Creates the destination if it does not already exist.

When `O_CREAT` is used, `open()` also receives a file-mode argument:

```text
0644
```

which requests:

```text
Owner  -> read + write
Group  -> read
Others -> read
```

The final permissions may be affected by the process `umask`.

#### `O_TRUNC`

If the destination already exists, its logical size is reduced to zero before copying begins.

This prevents old destination data from remaining after a shorter source file is copied.

### Why `|` is used

The flags are combined with the bitwise OR operator:

```c
O_WRONLY | O_CREAT | O_TRUNC
```

This allows several independent `open()` options to be supplied through one flags argument.

The program relies on the symbolic constants rather than assuming specific numeric flag values.

---

## 2.2 `read()`

`read()` transfers bytes from an open source file into the program's user-space buffer.

Conceptually:

```text
source FD
   ↓
read()
   ↓
kernel
   ↓
filesystem / page cache
   ↓
copy bytes into user-space buffer
```

The call used by the program is conceptually:

```c
read(src_fd, buffer, granularity);
```

Its return type is:

```c
ssize_t
```

because it must represent both byte counts and a negative error value.

Possible results:

```text
> 0 -> number of bytes read
  0 -> EOF
 -1 -> error
```

### EOF

A return value of:

```text
0
```

does not represent an error.

It means the end of the source file has been reached and the copy completed normally.

### `EINTR`

A read operation may fail with:

```c
errno == EINTR
```

if the system call was interrupted by a signal before completion.

The program handles this inside:

```c
read_retry()
```

by retrying the same read instead of treating the interruption as a fatal error.

---

## 2.3 `write()`

`write()` transfers bytes from the user-space buffer to the destination file.

Conceptually:

```text
buffer
   ↓
write()
   ↓
kernel
   ↓
destination open-file state
   ↓
page cache / filesystem
```

A critical property of `write()` is that a successful call is allowed to write **fewer bytes than requested**.

For example:

```text
requested = 4096 bytes
written   = 1500 bytes
```

This is not automatically an error.

The program must continue writing the remaining:

```text
4096 - 1500 = 2596 bytes
```

For this reason, the program implements:

```c
write_all()
```

which maintains:

```text
total_written
```

and repeatedly writes from:

```c
buffer + total_written
```

while requesting:

```c
count - total_written
```

until the entire chunk has been written.

Example:

```text
4096-byte chunk

[........................................]
 ^
 total_written = 0

write() -> 1500 bytes

[###############.........................]
                ^
                total_written = 1500

next write:
start     = buffer + 1500
remaining = 2596
```

### `EINTR`

Just like `read()`, `write()` may return `-1` with:

```c
errno == EINTR
```

The program retries that write attempt without advancing `total_written`.

---

## 2.4 `close()`

`close()` releases the process's reference to an open file descriptor.

The program closes both:

```text
destination FD
source FD
```

after the timed copy operation completes.

If `close()` returns:

```text
-1
```

the program reports the failure using `perror()` and changes the final program status to failure.

Resources are released in reverse acquisition order:

```text
malloc(buffer)
open(source)
open(destination)

       ↓

close(destination)
close(source)
free(buffer)
```

This ensures every successful acquisition has a matching release.

---

## 2.5 `clock_gettime()`

The program measures copy duration using:

```c
clock_gettime(CLOCK_MONOTONIC, ...);
```

Two timestamps are recorded:

```text
start_time
end_time
```

Each `struct timespec` contains:

```text
tv_sec  -> whole seconds
tv_nsec -> nanosecond component
```

`CLOCK_MONOTONIC` is used because the program measures elapsed duration rather than wall-clock time.

The timing boundary is:

```text
open source
open destination

      START
        ↓
    copy_file()
        ↓
       STOP

close destination
close source
```

Therefore setup and cleanup operations are excluded from the required I/O measurement.

---

## 3. Program Flow

The program keeps `main()` primarily as an orchestrator.

Detailed I/O behavior is delegated to helper functions.

```text
main()
│
├── parse_granularity()
├── allocate_buffer()
├── open_source()
├── open_destination()
│
├── clock_gettime(START)
│
├── copy_file()
│   │
│   ├── read_retry()
│   └── write_all()
│
├── clock_gettime(STOP)
├── elapsed_milliseconds()
│
├── close_file(destination)
├── close_file(source)
└── free(buffer)
```

---

## 3.1 Argument handling

First:

```c
argc == 4
```

is verified.

If the number of arguments is invalid, the program prints:

```text
Usage: ./my_cp <source_file> <destination_file> <granularity_bytes>
```

and exits with failure.

---

## 3.2 Granularity validation

`argv[3]` arrives as text.

The program uses:

```c
strtol()
```

instead of `atoi()` because `strtol()` allows validation of:

```text
no numeric digits
trailing invalid characters
numeric overflow
```

The final value must satisfy:

```text
granularity > 0
granularity <= INT_MAX
```

Only after validation is the parsed `long` converted to `int`.

---

## 3.3 Dynamic buffer allocation

The buffer size depends on the runtime granularity:

```text
granularity
     ↓
malloc()
     ↓
buffer
```

The allocation is checked before any file copy begins.

---

## 3.4 Opening the files

The source is opened first.

If source opening fails:

```text
free buffer
exit failure
```

The destination is opened second.

If destination opening fails:

```text
close source
free buffer
exit failure
```

---

## 3.5 Copy loop

The core loop is implemented in:

```c
copy_file()
```

Flow:

```text
              ┌────────────────────┐
              │    read_retry()    │
              └─────────┬──────────┘
                        │
          ┌─────────────┼─────────────┐
          │             │             │
         -1             0            > 0
          │             │             │
          ▼             ▼             ▼
       FAILURE      EOF/SUCCESS   write_all()
                                      │
                                ┌─────┴─────┐
                                │           │
                             failure      success
                                │           │
                                ▼           │
                             FAILURE        │
                                            │
                                            └── next read
```

Example for a 10-byte file with granularity `4`:

```text
read -> 4 bytes
write -> 4 bytes

read -> 4 bytes
write -> 4 bytes

read -> 2 bytes
write -> 2 bytes

read -> 0
EOF
```

An important rule is that the write count is:

```text
bytes_read
```

not always:

```text
granularity
```

because the final read may return fewer bytes than requested.

---

## 3.6 Timing

Once both files are open:

```text
start timestamp
      ↓
copy_file()
      ↓
end timestamp
```

Elapsed milliseconds are calculated from the two `timespec` values.

If:

```text
end.tv_nsec < start.tv_nsec
```

one second is borrowed and converted to:

```text
1,000,000,000 nanoseconds
```

before the final conversion to milliseconds.

---

## 3.7 Cleanup

Even if the copy operation fails, the program avoids immediately returning from the middle of the process.

Instead:

```text
exit_status = failure
```

is recorded and cleanup still executes.

This ensures owned resources are not abandoned.

---

## 4. Error Handling

The program checks the return value of every important operation before continuing.

### Argument and conversion errors

Examples include:

```text
missing arguments
too many arguments
non-numeric granularity
trailing characters
zero granularity
negative granularity
integer overflow
```

These are program-generated validation errors and are printed to `stderr` using `fprintf()`.

### `malloc()` failure

Detected when:

```c
buffer == NULL
```

The program prints an error and exits because copying cannot proceed without the I/O buffer.

### `open()` failures

Detected when:

```c
fd == -1
```

`perror()` is used so that the message includes the reason stored in `errno`.

If destination opening fails after the source was already opened, the source descriptor and allocated buffer are released before exiting.

### `read()` failures

If:

```c
read() == -1
```

the program checks:

```c
errno == EINTR
```

If true, the same read is retried.

Otherwise:

```text
perror()
propagate failure
```

### `write()` failures

The same `EINTR` strategy is used.

Additionally, partial successful writes are not treated as failure.

The remaining bytes are written before another source chunk is read.

The implementation also guards against a zero-byte write while bytes remain, preventing a no-progress infinite loop.

### `clock_gettime()` failures

Both timing calls are checked.

If either returns `-1`:

```text
perror()
exit_status = failure
```

### `close()` failures

Each `close()` operation is checked independently.

A close failure changes the final status to failure while allowing the remaining cleanup operations to continue.

---

## 5. Summary

The implementation satisfies the assignment by combining:

```text
validated CLI input
+
dynamic memory
+
POSIX file descriptors
+
robust read/write loops
+
system-call error checking
+
controlled resource cleanup
+
CLOCK_MONOTONIC timing
```

The main design principle is separation of responsibility:

```text
main()
    -> orchestration

read_retry()
    -> robust reading

write_all()
    -> robust complete writes

copy_file()
    -> copy-loop control

elapsed_milliseconds()
    -> timing calculation
```

This keeps the top-level program flow readable while isolating the system-level edge cases inside the helpers that own them.
