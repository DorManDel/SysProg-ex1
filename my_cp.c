/*
 * Assignment 1 - System Programming Course
 * Dor Mandel
 * ID: 315313825
 * ------------------------------------------
 * Usage:
 * ./my_cp <source_file> <destination_file> <granularity_bytes>
 */

/** COMMENT TAGS:
 *  WHY[]     - explains why a non-obvious implementation choice exists.
 *  STUDY[]   - captures a C/Linux concept worth remembering.
 *  EXAMPLE[] - preserves a concrete execution trace or edge-case example.
 *  README[]  - marks material to migrate into the final README/design notes.
 *  TODO[]    - temporary development note; remove before final submission.
 *
 * The tags let this source file act as both implementation and study notes
 * while the assignment is being developed. The final cleanup can keep short
 * WHY[] comments in the code and move larger STUDY[]/EXAMPLE[]/README[] notes
 * into the README and diagrams.
 */

/*
 * README[HIGH_LEVEL_FLOW]:
 *
 * USER SPACE:
 *   my_cp.c -> src_fd -> read() -> dynamic buffer -> write() -> dst_fd
 *
 * SYSTEM-CALL BOUNDARY:
 *   open() / read() / write() / close() transfer control into the kernel.
 *
 * KERNEL-SIDE CONCEPTUAL PATH:
 *   process FD table -> VFS / struct file / inode -> page cache / filesystem
 *
 * PROGRAM LIFECYCLE:
 *   CLI -> argv validation -> malloc -> open files -> timed copy loop
 *       -> EOF -> timing result -> close files -> free buffer -> exit
 *
 * NOTE: This is a conceptual learning model. The exact kernel path can vary
 * with filesystem, cache state, device, and kernel implementation details.
 */

// README[COMMENT_STYLE]: Function contracts use Doxygen-style comments; tagged notes explain deeper behavior.
/**
 * @brief Convert and validate the granularity CLI argument.
 *
 * The command-line argument arrives as a string. strtol() is used
 * instead of atoi() because it allows detection of:
 *   - input containing no digits,
 *   - trailing non-numeric characters,
 *   - numeric overflow.
 *
 * The assignment requires the final value to be a strictly positive
 * int. The parsed long is therefore validated before narrowing.
 *
 * @param arg              Granularity string received from argv[3].
 * @param out_granularity  Receives the validated integer on success.
 *
 * @return FUNC_SUCCESS on success.
 * @return FUNC_FAILED on validation failure.
 */

/* ============================================================
 *  LIBRARIES
 * ============================================================ */

#include <stdio.h>  /* fprintf(), perror(), printf(), stderr */
#include <stdlib.h> /* malloc(), free(), strtol(), EXIT_SUCCESS / EXIT_FAILURE */
#include <fcntl.h>  /* open() flags: O_RDONLY, O_WRONLY, O_CREAT, O_TRUNC */
#include <unistd.h> /* POSIX read(), write(), close() */
#include <time.h>   /* clock_gettime(), CLOCK_MONOTONIC, struct timespec */
#include <errno.h>  /* errno, EINTR, ERANGE */
#include <limits.h> /* INT_MAX */

/* ============================================================
 *  CONSTANTS / DEFINES
 * ============================================================ */

/*
 * STUDY[FILE_MODE_0644]:
 * 0644 is an octal permission mode used when O_CREAT creates the destination:
 *
 *   Owner  = rw- = 4 + 2 = 6
 *   Group  = r-- = 4
 *   Others = r-- = 4
 *
 * The leading 0 selects octal notation. The process umask may remove some
 * permission bits from the final mode.
 *
 * README[DESTINATION_PERMISSIONS]:
 * New destination files are requested with mode 0644, subject to umask.
 */
#define DEST_FILE_MODE 0644 /* requested mode for a newly created destination */

/* STUDY[HELPER_STATUS]: Internal helpers use 0 for success and -1 for failure. */
#define FUNC_FAILED (-1)
#define FUNC_SUCCESS 0

/* STUDY[TIME_UNITS]: Constants used to normalize and convert timespec values. */
#define NANOSECONDS_PER_SECOND 1000000000L
#define NANOSECONDS_PER_MILLISECOND 1000000.0

/*
 * WHY[ERROR_CHANNEL]:
 * Program-generated validation errors go explicitly to stderr.
 */
#define ERROR_MSG(...)                \
    do                                \
    {                                 \
        fprintf(stderr, __VA_ARGS__); \
    } while (0)

/*
 * WHY[PERROR_MACRO]:
 * perror() appends the text associated with the current errno value, making it
 * appropriate immediately after a failing errno-based system/library call.
 */
#define SYSTEM_ERROR(message) \
    do                        \
    {                         \
        perror(message);      \
    } while (0)

/* ============================================================
 *  HELPER FUNCTIONS
 * ============================================================ */

/**
 * @brief Parse and validate the requested I/O granularity.
 *
 * strtol() is used instead of atoi() so malformed input and numeric
 * overflow can be detected reliably.
 *
 * @param arg              CLI string containing the requested size.
 * @param out_granularity  Output location for the validated int.
 *
 * @return FUNC_SUCCESS on success, FUNC_FAILED otherwise.
 */
static int parse_granularity(const char *arg, int *out_granularity)
{
    /*
     * WHY[STRTOL_OVER_ATOI]:
     * strtol() provides the information needed to distinguish valid input from:
     *   1. input containing no digits,
     *   2. a valid numeric prefix followed by trailing characters,
     *   3. a value outside the range supported by the conversion.
     */
    char *endptr = NULL;

    /*
     * STUDY[ERRNO_STRTOL]:
     * errno is cleared before strtol() because ERANGE is used to report
     * conversion overflow/underflow.
     */
    errno = 0;

    /*
     * STUDY[ENDPTR]:
     * endptr receives the address of the first character that strtol() did not
     * consume. It lets the validation code inspect the entire CLI argument.
     */
    long parsed_granularity = strtol(arg, &endptr, 10);

    /*
     * WHY[NO_DIGITS_GUARD]:
     * If endptr still equals arg, strtol() consumed no characters at all.
     * Therefore the argument did not begin with a valid integer representation.
     */
    if (endptr == arg)
    {
        ERROR_MSG("Error: granularity must contain a number.\n");

        return FUNC_FAILED;
    }

    /*
     * WHY[TRAILING_INPUT_GUARD]:
     * A valid granularity must be the complete argument, not only a numeric
     * prefix. Any character remaining before '\0' makes the input invalid.
     *
     * EXAMPLE[TRAILING_INPUT]:
     *   input  = "4096abc"
     *   parsed = 4096
     *   endptr -> 'a'
     *
     * Because *endptr != '\0', the argument is rejected.
     */
    if (*endptr != '\0')
    {
        ERROR_MSG("Error: granularity must contain only an integer.\n");

        return FUNC_FAILED;
    }

    /*
     * WHY[INTEGER_RANGE_GUARD]:
     * ERANGE detects overflow of the long conversion itself. The assignment
     * stores granularity as int, so values above INT_MAX are also rejected
     * before the narrowing cast.
     */
    if (errno == ERANGE || parsed_granularity > INT_MAX)
    {
        ERROR_MSG("Error: granularity is outside the supported integer range.\n");

        return FUNC_FAILED;
    }

    /*
     * WHY[POSITIVE_GRANULARITY]:
     * Granularity is a buffer size in bytes, so zero and negative values do
     * not describe a valid copy chunk and are rejected.
     */
    if (parsed_granularity <= 0)
    {
        ERROR_MSG("Error: granularity must be greater than zero (positive).\n");

        return FUNC_FAILED;
    }

    /*
     * STUDY[SAFE_NARROWING]:
     * The checks above prove the value is positive and <= INT_MAX, so converting
     * the parsed long to int cannot lose a valid value.
     */
    *out_granularity = (int)parsed_granularity;

    return FUNC_SUCCESS;
}

/**
 * @brief Allocate the runtime-sized I/O buffer.
 *
 * The buffer size is selected by the user, so its size is unknown at
 * compile time and must be allocated dynamically.
 *
 * @param granularity Number of bytes requested for each I/O chunk.
 *
 * @return Pointer to the allocated buffer on success.
 * @return NULL if allocation fails.
 */
static char *allocate_buffer(int granularity)
{
    /*
     * WHY[DYNAMIC_BUFFER]:
     * Granularity is selected at runtime, so the required buffer size is not
     * known at compile time. malloc() creates exactly that temporary user-space
     * buffer for the current run.
     *
     * README[GRANULARITY_AND_MEMORY]:
     * The granularity argument directly determines the requested buffer size.
     */
    char *buffer = malloc((size_t)granularity);

    /* WHY[MALLOC_CHECK]: malloc() returns NULL when the allocation cannot be satisfied. */
    if (buffer == NULL)
    {
        ERROR_MSG("Error: failed to allocate %d bytes for the I/O buffer.\n",
                  granularity);
    }

    return buffer;
}

/**
 * @brief Open the source file for read-only access.
 *
 * open() resolves the pathname in the kernel and, on success,
 * establishes an open-file state that is represented in user space
 * by a process-local file descriptor.
 *
 * Pipeline:
 *
 *   pathname
 *      ↓
 *   open(path, O_RDONLY)
 *      ↓
 *   kernel path resolution
 *      ↓
 *   dentry / inode
 *      ↓
 *   struct file
 *      ↓
 *   process FD table
 *      ↓
 *   file descriptor
 *
 * @param path Source pathname.
 *
 * @return Non-negative file descriptor on success.
 * @return FUNC_FAILED on failure.
 */
static int open_source(const char *path)
{
    /*
     * WHY[SOURCE_RDONLY]:
     * The source is only consumed by read(), so O_RDONLY requests the minimum
     * access required by the assignment.
     *
     * STUDY[FD_HANDLE]:
     * On success, open() returns a process-local integer file descriptor. The
     * descriptor is a handle used by later read()/close() calls; it is not the
     * file contents themselves.
     */
    int fd = open(path, O_RDONLY);

    if (fd == FUNC_FAILED)
    {
        SYSTEM_ERROR("failed to popen source file");
    }
    return fd;
}

/**
 * @brief Open or create the destination file for writing.
 *
 * Flags:
 *
 *   O_WRONLY - open for writing only.
 *   O_CREAT  - create the destination if it does not already exist.
 *   O_TRUNC  - if the destination already exists, truncate its logical size
 *              to zero so the new copy completely replaces the old contents.
 *
 * The flags are combined using bitwise OR ('|'). They form a bitmask
 * that enables several independent open() options at once.
 *
 * DEST_FILE_MODE (0644) is supplied when O_CREAT creates a new file.
 *
 * @param path Destination pathname.
 *
 * @return Non-negative file descriptor on success.
 * @return FUNC_FAILED on failure.
 */
static int open_destination(const char *path)
{
    /*
     * WHY[DESTINATION_FLAGS]:
     * O_WRONLY allows writes, O_CREAT creates a missing destination, and O_TRUNC
     * removes the previous logical contents of an existing destination.
     *
     * STUDY[OPEN_FLAG_BITMASK]:
     * open() accepts independent options through one flags bitmask. Bitwise OR
     * combines the symbolic flag constants without relying on their numeric values.
     *
     * README[O_TRUNC]:
     * O_TRUNC is what makes an existing destination behave like a replacement
     * rather than leaving stale bytes from older contents.
     */
    int fd = open(
        path,
        O_WRONLY | O_CREAT | O_TRUNC,
        DEST_FILE_MODE);

    if (fd == FUNC_FAILED)
    {
        SYSTEM_ERROR("Failed to open destination file");
    }
    return fd;
}

/**
 * @brief Close one open file descriptor.
 *
 * close() removes this process's reference to the descriptor.
 * Once close() succeeds, the FD must no longer be used.
 *
 * @param fd          File descriptor to close.
 * @param description Context string used if perror() is required.
 *
 * @return FUNC_SUCCESS on success.
 * @return FUNC_FAILED if close() fails.
 */
static int close_file(int fd, const char *description)
{
    /*
     * WHY[CLOSE_CHECK]:
     * close() can fail too, so cleanup errors are reflected in the program's
     * final status instead of being silently ignored.
     */
    if (close(fd) == FUNC_FAILED)
    {
        SYSTEM_ERROR(description);
        return FUNC_FAILED;
    }

    return FUNC_SUCCESS;
}

/**
 * @brief Read up to count bytes, retrying if read() is interrupted.
 *
 * read() may fail with errno == EINTR if the system call is interrupted
 * by a signal before it completes. In that specific case, the operation
 * is retried rather than treated as a fatal I/O error.
 *
 * A return value of 0 is not an error; it represents end-of-file (EOF).
 *
 * @param fd     File descriptor to read from.
 * @param buffer Destination buffer in user space.
 * @param count  Maximum number of bytes requested.
 *
 * @return Positive number of bytes read on success.
 * @return 0 when EOF is reached.
 * @return -1 on a non-recoverable read error.
 */
static ssize_t read_retry(int fd, char *buffer, size_t count)
{
    /*
     * STUDY[READ_RETURN_VALUE]:
     * read() returns ssize_t because one result type must represent:
     *
     *   > 0 : number of bytes actually read
     *     0 : EOF (normal end-of-file, not an error)
     *    -1 : failure, with errno describing the reason
     *
     * WHY[READ_EINTR_RETRY]:
     * EINTR means the syscall was interrupted by a signal before it completed.
     * That condition is recoverable, so the same read request is tried again.
     *
     * STUDY[READ_RETRY_PSEUDOCODE]:
     *   repeat read(fd, buffer, count) while result == -1 && errno == EINTR
     *   if result == -1 -> report the real error
     *   otherwise       -> return bytes read or 0 for EOF
     */

    ssize_t bytes_read;

    do
    {
        bytes_read = read(fd, buffer, count);
    } while (bytes_read == FUNC_FAILED && errno == EINTR);

    if (bytes_read == FUNC_FAILED)
    {
        SYSTEM_ERROR("read");
    }

    return bytes_read;
}

/**
 * @brief Write an entire buffer to a file descriptor.
 *
 * write() is allowed to write fewer bytes than requested.
 * Therefore, this function keeps track of how many bytes have
 * already been written and continues writing the remaining data.
 *
 * If write() fails because it was interrupted by a signal
 * (errno == EINTR), the write attempt is retried.
 *
 * @param fd     Destination file descriptor.
 * @param buffer Buffer containing the data to write.
 * @param count  Total number of bytes that must be written.
 *
 * @return FUNC_SUCCESS when all bytes were written successfully.
 * @return FUNC_FAILED on a non-recoverable write error.
 */
static int write_all(int fd, const char *buffer, size_t count)
{
    /*
     * STUDY[PARTIAL_WRITE]:
     * write() may successfully write fewer bytes than requested. total_written
     * tracks progress across all write() calls for the current chunk.
     *
     * EXAMPLE[PARTIAL_WRITE]:
     *   count = 4096
     *
     *   before: total_written = 0
     *           [........................................]
     *            ^
     *
     *   write() returns 1500
     *
     *   after:  total_written = 1500
     *           [###############.........................]
     *                           ^
     *
     * The next request starts at buffer + total_written and asks for only
     * count - total_written bytes. This prevents already-written bytes from
     * being written a second time.
     */
    size_t total_written = 0;

    while (total_written < count)
    {
        /*
         * STUDY[BYTES_WRITTEN_VS_TOTAL]:
         * bytes_written = progress made by THIS write() syscall.
         * total_written = accumulated progress for the ENTIRE current chunk.
         */
        ssize_t bytes_written = write(
            fd,
            buffer + total_written,
            count - total_written);

        /*
         * WHY[WRITE_EINTR_RETRY]:
         * A -1 result with errno == EINTR means no successful progress was
         * reported for this attempt. Retrying leaves total_written unchanged.
         */
        if (bytes_written == FUNC_FAILED)
        {
            if (errno == EINTR)
            {
                continue;
            }

            SYSTEM_ERROR("write");
            return FUNC_FAILED;
        }

        /*
         * WHY[WRITE_ZERO_GUARD]:
         * If write() returned 0 while bytes still remain, total_written would not
         * advance and this loop could never reach its termination condition.
         */
        if (bytes_written == 0)
        {
            ERROR_MSG("Error: write made no progress.\n");
            return FUNC_FAILED;
        }

        /*
         * STUDY[SAFE_WRITE_CAST]:
         * At this point -1 and 0 have already been handled, so bytes_written is
         * positive and can safely be converted to size_t before accumulation.
         */
        total_written += (size_t)bytes_written;
    }

    return FUNC_SUCCESS;
}

/**
 * @brief Copy all data from the source file descriptor to the destination.
 *
 * The source file is read in chunks of at most granularity bytes.
 * Each successfully-read chunk is then passed to write_all(), which
 * guarantees that the entire chunk is written before the next read.
 *
 * Responsibilities are separated between the helpers:
 *
 *   read_retry() -> handles read(), EOF, and EINTR.
 *   write_all()  -> handles partial writes and write() EINTR.
 *   copy_file()  -> controls the overall copy loop.
 *
 * @param src_fd       Source file descriptor.
 * @param dst_fd       Destination file descriptor.
 * @param buffer       Shared temporary I/O buffer.
 * @param granularity  Maximum number of bytes requested per read().
 *
 * @return FUNC_SUCCESS when EOF is reached after a successful copy.
 * @return FUNC_FAILED if a read or write operation fails.
 */
static int copy_file(
    int src_fd,
    int dst_fd,
    char *buffer,
    size_t granularity)
{
    /*
     * STUDY[SEPARATION_OF_RESPONSIBILITIES]:
     * copy_file() coordinates the copy but delegates syscall-specific robustness:
     *
     *   read_retry() -> read(), EOF, and read-side EINTR handling
     *   write_all()  -> partial writes and write-side EINTR handling
     *   copy_file()  -> chunk-by-chunk sequencing and error propagation
     */

    /*
     * EXAMPLE[CHUNKED_COPY]:
     *
     * File size = 10 bytes, granularity = 4 bytes
     *
     *   iteration 1: read 4 -> write all 4
     *   iteration 2: read 4 -> write all 4
     *   iteration 3: read 2 -> write all 2
     *   iteration 4: read 0 -> EOF -> success
     *
     * README[COPY_PIPELINE]:
     *   copy_file()
     *      -> read_retry() -> read() -> EINTR retry / EOF / real error
     *      -> write_all()  -> write() -> EINTR retry / partial-write loop
     */

    /* WHY[LOOP_UNTIL_EOF]: The loop has explicit exits for error and EOF. */
    while (1)
    {
        /*
         * STUDY[READ_CHUNK_RESULT]:
         * read_retry() asks for at most granularity bytes and returns the amount
         * actually obtained. EINTR has already been handled inside the helper.
         *
         *   > 0 -> valid bytes in buffer
         *     0 -> EOF
         *    -1 -> non-recoverable read error
         */
        ssize_t bytes_read =
            read_retry(src_fd, buffer, granularity);

        /*
         * WHY[ERROR_PROPAGATION]:
         * read_retry() reports the concrete syscall error; copy_file() only
         * propagates failure upward so the same error is not printed twice.
         */
        if (bytes_read == FUNC_FAILED)
        {
            return FUNC_FAILED;
        }

        /*
         * STUDY[EOF_IS_SUCCESS]:
         * A read result of 0 means the source has no more bytes. Reaching EOF after
         * all previous chunks were written is the normal successful end condition.
         */
        if (bytes_read == 0)
        {
            return FUNC_SUCCESS;
        }

        /*
         * WHY[WRITE_BYTES_READ]:
         * write_all() receives bytes_read, not granularity. The last read may be
         * shorter than the requested chunk size, so only the bytes returned by
         * read() are valid for this iteration.
         *
         * EXAMPLE[FINAL_SHORT_CHUNK]:
         *   granularity = 4096, bytes_read = 217 -> write exactly 217 bytes.
         */
        if (write_all(
                dst_fd,
                buffer,
                (size_t)bytes_read) == FUNC_FAILED)
        {
            return FUNC_FAILED;
        }
    }
}

/**
 * @brief Calculate elapsed time between two CLOCK_MONOTONIC timestamps.
 *
 * The timespec structure stores time using two fields:
 *
 *   tv_sec  -> whole seconds
 *   tv_nsec -> remaining nanoseconds within that second
 *
 * Because end->tv_nsec may be smaller than start->tv_nsec, the raw
 * nanosecond subtraction may become negative. In that case, one second
 * is borrowed and converted into 1,000,000,000 nanoseconds.
 *
 * @param start Pointer to the timestamp recorded before the copy.
 * @param end   Pointer to the timestamp recorded after the copy.
 *
 * @return Elapsed time in milliseconds.
 *
 *  This function performs four steps:
 *  [-] subtract whole seconds
 *  [-] subtract nanoseconds
 *  [-] normalize a negative nanosecond difference by borrowing one second
 *  [-] convert the normalized result to milliseconds
 */
static double elapsed_milliseconds(
    const struct timespec *start,
    const struct timespec *end)
{
    /*
     * STUDY[TIMESPEC]:
     *
     * struct timespec represents one timestamp as:
     *
     *      seconds + nanoseconds
     *
     * Example:
     *
     *   tv_sec  = 12
     *   tv_nsec = 250000000
     *
     * represents conceptually:
     *
     *   12.250000000 seconds
     */
    long seconds = end->tv_sec - start->tv_sec;
    long nanoseconds = end->tv_nsec - start->tv_nsec;

    /*
     * EXAMPLE[NANOSECOND_BORROW]:
     *
     * start = 10 sec + 900,000,000 ns
     * end   = 11 sec + 100,000,000 ns
     *
     * Raw subtraction:
     *
     *   seconds     = 11 - 10
     *               = 1
     *
     *   nanoseconds = 100,000,000 - 900,000,000
     *               = -800,000,000
     *
     * The negative nanosecond value means we need to borrow
     * one whole second:
     *
     *   seconds--
     *
     * and convert that borrowed second into nanoseconds:
     *
     *   nanoseconds += 1,000,000,000
     *
     * Result:
     *
     *   seconds     = 0
     *   nanoseconds = 200,000,000
     *
     * Therefore:
     *
     *   elapsed = 200 ms
     */
    if (nanoseconds < 0)
    {
        seconds--;
        nanoseconds += NANOSECONDS_PER_SECOND;
    }

    /*
     * README[TIME_CONVERSION]:
     * Convert the normalized difference to milliseconds:
     *
     *   milliseconds = seconds * 1000
     *                + nanoseconds / 1,000,000
     *
     * EXAMPLE[TIME_CONVERSION]:
     *   2 seconds + 500,000,000 ns = 2000 ms + 500 ms = 2500 ms.
     */
    return (seconds * 1000.0) +
           (nanoseconds / NANOSECONDS_PER_MILLISECOND);
}

/* ============================================================
 *  MAIN
 * ============================================================ */

/*
 * README[PROGRAM_FLOW] — MAIN AS THE ORCHESTRATOR:
 *
 * validate argc
 *      -> parse granularity
 *      -> allocate buffer
 *      -> open source
 *      -> open destination
 *      -> start timer
 *      -> copy_file()
 *      -> stop timer / print elapsed time
 *      -> close destination
 *      -> close source
 *      -> free buffer
 *      -> return final status
 *
 * WHY[SMALL_MAIN]:
 * main() coordinates the lifecycle while helper functions own detailed parsing,
 * I/O robustness, timing calculation, and close error handling.
 */
int main(int argc, char *argv[])
{
    /*
     * STUDY[ARGV_LAYOUT]:
     * argc must be 4 because argv contains the executable name plus three user
     * arguments:
     *
     *   argv[0] = executable name
     *   argv[1] = source pathname
     *   argv[2] = destination pathname
     *   argv[3] = granularity string
     *
     * CLI: ./my_cp <source_file> <destination_file> <granularity_bytes>
     */
    if (argc != 4)
    {
        ERROR_MSG(
            "Usage: %s <source_file> <destination_file> <granularity_bytes>\n",
            argv[0]);

        return EXIT_FAILURE;
    }

    int granularity = 0;

    if (parse_granularity(argv[3], &granularity) == FUNC_FAILED)
    {
        return EXIT_FAILURE;
    }

    char *buffer = allocate_buffer(granularity);

    if (buffer == NULL)
    {
        return EXIT_FAILURE;
    }

    int src_fd = open_source(argv[1]);

    if (src_fd == FUNC_FAILED)
    {
        /* WHY[PARTIAL_CLEANUP_SRC_OPEN]: Only the buffer was acquired so far. */
        free(buffer);
        return EXIT_FAILURE;
    }

    int dst_fd = open_destination(argv[2]);

    if (dst_fd == FUNC_FAILED)
    {
        /*
         * WHY[PARTIAL_CLEANUP_DST_OPEN]:
         * Destination open failed after buffer + source FD were acquired, so both
         * previously acquired resources must be released before returning.
         */
        close_file(src_fd, "Failed to close source file");
        free(buffer);
        return EXIT_FAILURE;
    }

    int exit_status = EXIT_SUCCESS;

    /*
     * STUDY[TIMESPEC]:
     * We need two timestamps:
     *
     *   start_time -> immediately before the copy operation
     *   end_time   -> immediately after the copy operation
     *
     * Each timestamp contains:
     *
     *   tv_sec  -> whole seconds
     *   tv_nsec -> nanosecond fraction of the second
     */
    struct timespec start_time;
    struct timespec end_time;

    /*
     * WHY[TIMING_BOUNDARY]:
     *
     * Both files have already been successfully opened at this point.
     *
     * The assignment requires us to measure only the read/write copy
     * operation, excluding:
     *
     *   malloc()
     *   open()
     *   close()
     *   free()
     *
     * Therefore the timer starts immediately before copy_file().
     *
     * TIMED REGION:
     *
     *      START
     *        |
     *        v
     *   copy_file()
     *      |
     *      +-- read_retry()
     *      |      |
     *      |      +-- read()
     *      |
     *      +-- write_all()
     *             |
     *             +-- write()
     *        |
     *        v
     *       STOP
     */

    /*
     * STUDY[CLOCK_MONOTONIC]:
     *
     * CLOCK_MONOTONIC is appropriate for measuring elapsed time.
     *
     * We care about:
     *
     *      "How much time passed?"
     *
     * rather than:
     *
     *      "What time is it?"
     *
     * A monotonic clock is therefore preferred for benchmarking.
     */
    if (clock_gettime(CLOCK_MONOTONIC, &start_time) == FUNC_FAILED)
    {
        SYSTEM_ERROR("clock_gettime start");
        exit_status = EXIT_FAILURE;
    }
    else
    {
        /*
         * README[TIMED_COPY]:
         *
         * This is the complete region whose performance is affected
         * by the requested granularity.
         *
         * Smaller granularity:
         *
         *   more read()/write() operations
         *
         * Larger granularity:
         *
         *   fewer read()/write() operations
         *
         * The copied contents should remain identical regardless
         * of the granularity used.
         */
        int copy_status = copy_file(
            src_fd,
            dst_fd,
            buffer,
            (size_t)granularity);

        /*
         * WHY[STOP_BEFORE_CLEANUP]:
         *
         * The end timestamp must be recorded before close(), because
         * close() time is not part of the required I/O measurement.
         */
        if (clock_gettime(CLOCK_MONOTONIC, &end_time) == FUNC_FAILED)
        {
            SYSTEM_ERROR("clock_gettime end");
            exit_status = EXIT_FAILURE;
        }
        else
        {
            /*
             * Only calculate and print an elapsed duration if both
             * timestamps were recorded successfully.
             */
            double elapsed_ms =
                elapsed_milliseconds(&start_time, &end_time);

            printf(
                "I/O operations took: %.3f ms "
                "(Granularity: %d bytes)\n",
                elapsed_ms,
                granularity);

            /*
             * README[BENCHMARK_VARIABILITY]:
             * Very small copies can produce noisy timing results because fixed
             * overhead, scheduling, filesystem caching, and page-cache state may be
             * significant relative to the copy duration itself. For a meaningful
             * comparison, use a larger file and repeat each granularity several times.
             */
        }

        /*
         * WHY[NO_EARLY_RETURN]:
         *
         * copy_file() already reports the concrete read/write error.
         *
         * main() only records that the overall program failed.
         *
         * We deliberately do NOT return here because we still own:
         *
         *   dst_fd
         *   src_fd
         *   buffer
         *
         * They must still be released during cleanup.
         */
        if (copy_status == FUNC_FAILED)
        {
            exit_status = EXIT_FAILURE;
        }
    }

    /*
     * README[RESOURCE_LIFETIME]:
     * Every successful acquisition must have a matching release. Resources are
     * cleaned up in reverse acquisition order:
     *
     *   malloc(buffer)       --------------------------> free(buffer)
     *   open(source)         -----------------------> close(source)
     *   open(destination)    ------------------> close(destination)
     *
     * WHY[REVERSE_CLEANUP]:
     * Even when copying fails, cleanup still runs so owned descriptors and memory
     * are not leaked. A close failure also changes the final exit status.
     */
    if (close_file(dst_fd, "Failed to close destination file") == FUNC_FAILED)
    {
        exit_status = EXIT_FAILURE;
    }

    if (close_file(src_fd, "Failed to close source file") == FUNC_FAILED)
    {
        exit_status = EXIT_FAILURE;
    }

    free(buffer);

    return exit_status;
}
