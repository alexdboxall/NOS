## NOS - *New* Operating System Kernel ##

NOS is an operating system based on my earlier [ATOS](https://github.com/alexdboxall/ATOS), which was in turn inspired by OS/161. NOS is written in C, and is designed (relatively) easy to understand, portable and lightweight. Unlike ATOS which was aiming to be more of an "educational" OS, NOS tries to be a more full featured OS (e.g. the NOS virtual memory manager is has a lot more features), and I decided I liked the `WindowsNamingConvention()` instead of the `unix_naming_convention`.

NOS still only requires around 3MB of RAM to run, and excluding ACPICA and FAT drivers, is only 50,000 lines of commented code.

It is currently only implemented for x86, but should be easy to port to other platforms (via the arch/ folder, and arch.h).

To build it, run `./release.sh`. To run it in QEMU, use the following command: `qemu-system-i386 -soundhw pcspk -hda build/output/disk.bin -m 3M`

The TODO list:
- Writing to disk
- "Homemade" FAT driver (instead of using FatFS) - for code style consistency and better integration
- Fixing low memory crashes / terrible performance
- A virtual filesystem (VFS) to manage files, folders and devices
- Disk auto-detection
- Disk caching
- More system calls
- Fixing up all the other TODOs in the code!!

![NOS Kernel](https://github.com/alexdboxall/NOS/blob/main/docs/assets/readmeimg.png "ATOS Kernel")

Copyright Alex Boxall 2022-2024. See LICENSE and ATTRIBUTION for details.