#include "types.h"
#include "stat.h"
#include "user.h"
#include "fcntl.h"
#include "wmap.h"

int main() {
    uint addr = 0x60000000;  // Example virtual address within the valid range
    int length = 4096;       // Request one page
    int flags = MAP_FIXED | MAP_SHARED | MAP_ANONYMOUS;
    int fd = -1;             // Not needed for MAP_ANONYMOUS

    // Request memory mapping
    uint result = wmap(addr, length, flags, fd);
    if (result == (uint)-1) {
        printf(1, "wmap failed\n");
        exit();
    }

    printf(1, "wmap succeeded, mapped address: 0x%x\n", result);

    // Access the mapped memory to trigger lazy allocation
    char *mapped_mem = (char *)result;
    mapped_mem[0] = 'A';  // Write to trigger page fault and lazy allocation

    printf(1, "Write to mapped memory succeeded, value: %c\n", mapped_mem[0]);

    // Check if the memory can be read correctly
    if (mapped_mem[0] == 'A') {
        printf(1, "Memory read succeeded: %c\n", mapped_mem[0]);
    } else {
        printf(1, "Memory read failed\n");
    }

    // Exit the test
    exit();
}

