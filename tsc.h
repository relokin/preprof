#ifndef _TSC_H
#define _TSC_H

#include <stdint.h>

#if defined(__x86_64__)

#if 0
#include <libcpuid.h>

static inline int
read_tsc_reliability()
{
    if (!cpuid_present()) {
	fprintf(stderr, "Sorry, your CPU doesn't support CPUID!\n");
        return -1;
    }

    struct cpu_raw_data_t raw;
    struct cpu_id_t data;

    if (cpuid_get_raw_data(&raw) < 0) {
        fprintf(stderr, "Sorry, cannot get the CPUID raw data.\n");
        fprintf(stderr, "Error: %s\n", cpuid_error());
        return -2;
    }

    if (cpu_identify(&raw, &data) < 0) {
        fprintf(stderr, "Sorrry, CPU identification failed.\n");
        fprintf(stderr, "Error: %s\n", cpuid_error());
        return -3;
    }

    if (data.flags[CPU_FEATURE_CONSTANT_TSC] &&
	data.flags[CPU_FEATURE_NONSTOP_TSC]) {
	return 1;
    }

    return 0;
}
#endif

/* 33 cycles per read, without interference?
 * Make sure that /proc/cpuinfo flags contains rdtscp.
 */
static inline uint64_t __attribute__((always_inline))
read_tsc_p(void)
{
   uint64_t tsc;
   __asm__ __volatile__ ("rdtscp\n"
	 "shl $32, %%rdx\n"
	 "or %%rdx, %%rax"
	 : "=a"(tsc)
	 :
	 : "%rcx", "%rdx");
   return tsc;
}


#else

#error Unsupported architecture

#endif

#endif
