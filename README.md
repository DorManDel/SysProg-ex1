
---

##  Requirements Decomposition

* REQ-ARG-01
    - Program shall require argc == 4.

* REQ-ARG-02
    - argv[1] shall represent the source pathname.

* REQ-ARG-03
    - argv[2] shall represent the destination pathname.

* REQ-ARG-04
    - argv[3] shall be converted from text to a positive integer.
    
    ---

* REQ-MEM-01
    - Buffer shall be allocated dynamically.
    
    ---

* REQ-IO-01
    - Source shall be opened using open() + O_RDONLY.

* REQ-IO-02
    - Destination shall use:
    - O_WRONLY | O_CREAT | O_TRUNC
    
    ---

* REQ-ROBUST-01
    - read/write interrupted by EINTR shall be retried.

* REQ-ROBUST-02
    - Partial writes shall be completed.

    ---

* REQ-TIME-01
    - Timing starts after successful opens.

* REQ-TIME-02
    - Timing ends before close().

* **Every successful acquisition must have a matching release.**
    - for ex. : 
    - - ```malloc() <-> free()```
    - -   ```open() <-> close()```

---

## Architecture

```c
main
 |
 +-- validateArguments
 |
 +-- allocateBuffer
 |
 +-- openSource
 |
 +-- openDestination
 |
 +-- startTimer
 |
 +-- copyFile
 |      |
 |      +-- robustRead
 |      |
 |      +-- robustWrite
 |
 +-- stopTimer
 |
 +-- printTiming
 |
 +-- cleanup
 ```

 ---

 ## sysCall LifeCycle

```c
open()
read()
write()
close()
```
for each one: 

- WHAT does it do?
- WHY do we need it?
- WHAT arguments does it receive?
- WHAT does it return?
- WHAT can fail?
- WHAT does errno mean here?
- WHAT happens in User Space?
- WHEN do we enter Kernel Space?
- WHAT kernel structures participate?
- WHAT changes after the call?
- HOW does it relate to our assignment?

### `open()` — Source File

#### Purpose
Open the source file for reading and establish a kernel-managed
open-file state before the copy loop begins.

#### Source `open()` lifecycle

```text
argv[1]
  ↓
pathname string
  ↓
open(path, O_RDONLY)
  ↓
SYSTEM CALL BOUNDARY
  ↓
Kernel path resolution
  ↓
dentry
  ↓
in-core inode
  ↓
permission validation
  ↓
struct file
  ↓
process FD table
  ↓
return file descriptor
```

#### Assignment call

```c
src_fd = open(argv[1], O_RDONLY);
```

#### Inputs
 - ```argv[1]``` — source pathname.
 - ```O_RDONLY``` — request read-only access.

#### Success

Returns a non-negative file descriptor.

***Example:***

```c
src_fd = 3
```

The FD is not the file itself. It is a process-local integer used
to locate the corresponding kernel open-file object.

#### Failure

Returns:
```-1```

```errno``` contains the failure reason, which can be displayed using
```perror()```.

#### Kernel-side conceptual flow

```
pathname
   ↓
path resolution
   ↓
dentry
   ↓
in-core inode
   ↓
struct file
   ↓
process FD table
   ↓
file descriptor
```

---

## Memory

***[in terms of category]*** -
```malloc + free =/= read()``` 

```js
granularity
     ↓
malloc()
     ↓
buffer in process virtual memory
     ↓
read()
     ↓
Kernel copies bytes into that buffer
```

---

## Robust I/O

- EOF
- read errors
- EINTR
- partial writes
- write failures

---

## Timing

- struct timespec
- clock_gettime()
- CLOCK_MONOTONIC
- tv_sec
- tv_nsec

---

## Resource Ownership

- buffer
- source FD
- destination FD

    ### resource lifetime model:
```
malloc  ------------------------------ free
open src ------------------------- close src
open dst ------------------------- close dst
```

---

## Pseudo Code
```
PROGRAM my_cp

validate arguments

convert granularity
validate granularity

allocate buffer

open source
if failure:
    cleanup and exit

open destination
if failure:
    cleanup and exit

record start time

LOOP:
    robustly read chunk

    if EOF:
        break

    write entire chunk
    handling:
        partial writes
        EINTR
        real errors

record end time

calculate elapsed time

close destination
close source
free buffer

exit success
```