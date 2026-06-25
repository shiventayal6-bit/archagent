// Created by st3125 on 2026/6/11
// ArchAgent IDE - Target profile generation

#include "target_profile.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/utsname.h>
#include <unistd.h>

void target_profile_free(TargetProfile *profile) {
    if (profile && profile->text) {
        free(profile->text);
        profile->text = NULL;
    }
}

bool target_profile_build(TargetProfile *profile) {
    if (!profile) return false;
    profile->text = NULL;

    // --- OS info via uname() ---
    struct utsname uts;
    if (uname(&uts) != 0) {
        fprintf(stderr, "Error: uname() failed\n");
        return false;
    }

    // --- CPU cores ---
    long cores = sysconf(_SC_NPROCESSORS_ONLN);
    if (cores < 1) cores = 1;

    // --- Memory ---
    long pages     = sysconf(_SC_PHYS_PAGES);
    long page_size = sysconf(_SC_PAGE_SIZE);
    long memory_mb = (pages > 0 && page_size > 0)
                     ? (long)((long long)pages * page_size / (1024 * 1024))
                     : -1;

    // --- Endianness ---
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    const char *endian = "little-endian";
#else
    const char *endian = "big-endian";
#endif

    // --- Architecture at compile time ---
#if defined(__aarch64__)
    const char *arch = "aarch64";
#elif defined(__x86_64__)
    const char *arch = "x86_64";
#elif defined(__arm__)
    const char *arch = "arm32";
#else
    const char *arch = "unknown";
#endif

    // --- Compiler info ---
#ifdef __VERSION__
    const char *compiler_version = __VERSION__;
#else
    const char *compiler_version = "unknown";
#endif

#if defined(__clang__)
    const char *compiler_name = "clang";
#elif defined(__GNUC__)
    const char *compiler_name = "gcc";
#else
    const char *compiler_name = "unknown";
#endif

    // --- C standard ---
#if __STDC_VERSION__ >= 201710L
    const char *c_std = "C17";
#elif __STDC_VERSION__ >= 201112L
    const char *c_std = "C11";
#else
    const char *c_std = "C99 or older";
#endif

    // --- build the profile text ---
    // allocate a big enough buffer
    char *buf = malloc(4096);
    if (!buf) return false;

    int written = snprintf(buf, 4096,
        "TARGET PROFILE\n"
        "This profile was generated automatically by the local C orchestration layer.\n"
        "Use it as optimisation guidance, not as proof of performance.\n"
        "\n"
        "HOST PLATFORM\n"
        "  OS:               %s\n"
        "  Kernel:           %s %s\n"
        "  Machine:          %s\n"
        "\n"
        "CPU AND ARCHITECTURE\n"
        "  Compile-time arch: %s\n"
        "  Runtime machine:   %s\n"
        "  Logical cores:     %ld\n"
        "  Endianness:        %s\n"
        "\n"
        "MEMORY\n"
        "  Physical memory:  %ld MB\n"
        "  Page size:        %ld bytes\n"
        "  Pointer size:     %zu bytes\n"
        "  int size:         %zu bytes\n"
        "  long size:        %zu bytes\n"
        "\n"
        "COMPILER\n"
        "  Compiler:         %s %s\n"
        "  C standard:       %s\n"
        "  Suggested flags:  -std=c17 -O2 -Wall\n"
        "\n"
        "ARCHITECTURE-AWARE GUIDANCE\n"
        "  - Prefer correctness first, portability second.\n"
        "  - Write portable C17 by default.\n"
        "  - Use fixed-width integers where binary layout matters.\n"
        "  - Respect endianness when reading/writing binary data.\n"
        "  - Benchmark before claiming speed improvements.\n",
        uts.sysname,
        uts.sysname, uts.release,
        uts.machine,
        arch,
        uts.machine,
        cores,
        endian,
        memory_mb,
        page_size,
        sizeof(void *),
        sizeof(int),
        sizeof(long),
        compiler_name, compiler_version,
        c_std
    );

    if (written < 0 || written >= 4096) {
        free(buf);
        return false;
    }

    profile->text = buf;
    return true;
}
