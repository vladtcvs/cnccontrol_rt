MEMORY
{
        rom (rx) : ORIGIN  = 0x08000000, LENGTH = 64K
        ram (rwx) : ORIGIN = 0x20000000, LENGTH = 20K
}

INCLUDE libopencm3/lib/cortex-m-generic.ld

