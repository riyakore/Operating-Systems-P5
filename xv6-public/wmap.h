// wmap's header file
// all of these values can be changed in the future as the project progresses

#ifndef WMAP_H
#define WMAP_H

// these memory locations can be changes - these values are from the hint's provided in the project description
// Flags for wmap
#define MAP_SHARED 0x0002
#define MAP_ANONYMOUS 0x0004
#define MAP_FIXED 0x0008

// When any system call fails, returns -1
#define FAILED -1
#define SUCCESS 0

// for `getwmapinfo`
#define MAX_WMMAP_INFO 16

struct wmapinfo {
    int total_mmaps;                    // Total number of wmap regions
    int addr[MAX_WMMAP_INFO];           // Starting address of mapping
    int length[MAX_WMMAP_INFO];         // Size of mapping
    int n_loaded_pages[MAX_WMMAP_INFO]; // Number of pages physically loaded into memory
};

// declared the wmap function prototype here to access it in sysproc.c
uint wmap(uint addr, int length, int flags, int fd);

// declared the wunmap function prototype here to access it
int wunmap(uint addr);

#endif
