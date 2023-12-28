#include <physical.h>
#include <virtual.h>
#include <heap.h>
#include <cpu.h>
#include <log.h>
#include <debug.h>
#include <assert.h>
#include <timer.h>
#include <irql.h>
#include <thread.h>
#include <panic.h>
#include <stdlib.h>
#include <process.h>
#include <dev.h>
#include <vfs.h>
#include <transfer.h>
#include <fcntl.h>
#include <console.h>
#include <swapfile.h>
#include <diskutil.h>
#include <string.h>
#include <filesystem.h>
#include <driver.h>

/*
 * Next steps:
 * - running any old ring 3 program
 * - dynamic linker
 * - system call interface (KRNLAPI.LIB) 
 * - C standard library
 * - complete-enough CLI OS
 *          terminal that supports pipes, redirection and background processes
 *          cd, ls/dir, type, mkdir, rm, more, rename, copy, tree, mkfifo, pause, rmtree, rmdir, cls, copytree, link, 
 *                  ...ttyname, sleep, exit
 *          port zlib, nasm
 * - floppy driver
 * - FAT32 driver
 */

 void DummyAppThread(void*) {
	PutsConsole("drv0:/> ");

    struct open_file* con;
    OpenFile("con:", O_RDONLY, 0, &con);

    while (true) {
        char bf[302];
		inline_memset(bf, 0, 302);
        struct transfer tr = CreateKernelTransfer(bf, 301, 0, TRANSFER_READ);
		ReadFile(con, &tr);
		PutsConsole("Command not found: ");
		PutsConsole(bf);
        PutsConsole("\n");

        if (GetFreePhysKilobytes() < 16) {
            PutsConsole("Less than 16 KB left\n");
            MapVirt(0, 0, 4096, VM_LOCK | VM_READ, NULL, 0);

        } else {
            MapVirt(0, 0, 8 * 4096, VM_LOCK | VM_READ, NULL, 0);
        }

		PutsConsole("drv0:/> ");

    }
}

void InitUserspace(void) {
    size_t free = GetFreePhysKilobytes();
    size_t total = GetTotalPhysKilobytes();
    DbgScreenPrintf("NOS Kernel\nCopyright Alex Boxall 2022-2023\n\n%d / %d KB used (%d%% free)\n\n", total - free, total, 100 * (free) / total);
    CreateThread(DummyAppThread, NULL, GetVas(), "dummy app");
}

void InitThread(void*) {
    InitFilesystemTable();
    ArchInitDev(false);

    struct open_file* sys_folder;
    int res = OpenFile("drv0:/System", O_RDONLY, 0, &sys_folder);
    if (res != 0) {
        PanicEx(PANIC_NO_FILESYSTEM, "sys A");
    }
    res = AddVfsMount(sys_folder->node, "sys");
    if (res != 0) {
        PanicEx(PANIC_NO_FILESYSTEM, "sys B");
    }

    struct open_file* swapfile;
    res = OpenFile("raw-hd0:/part1", O_RDWR, 0, &swapfile);
    if (res != 0) {
        PanicEx(PANIC_NO_FILESYSTEM, "swapfile A");
    }
    res = AddVfsMount(swapfile->node, "swap");
    if (res != 0) {
        PanicEx(PANIC_NO_FILESYSTEM, "swapfile B");
    }

    InitSwapfile();
    InitSymbolTable();
    ArchInitDev(true);
    InitUserspace();

    MarkTfwStartPoint(TFW_SP_ALL_CLEAR);

    while (true) {
        /*
         * We crash in strange and rare conditions if this thread's stack gets removed, so we will
         * ensure we don't terminate it.
         */
        SleepMilli(100000);
    }
}

void KernelMain(void) {
    LogWriteSerial("KernelMain: kernel is initialising...\n");

    /*
     * Allows us to call GetCpu(), which allows IRQL code to work. Anything which uses
     * IRQL (i.e. the whole system) relies on this, so this must be done first.
     */
    InitCpuTable();
    assert(GetIrql() == IRQL_STANDARD);

    /*
     * Initialise the testing framework if we're in debug mode.
     */
    InitTfw();
    MarkTfwStartPoint(TFW_SP_INITIAL);

    InitPhys();
    MarkTfwStartPoint(TFW_SP_AFTER_PHYS);

    InitHeap();

    /*
     * Allows deferments of functions to actually happen. IRQL is still usable beforehand though.
     */
    InitIrql();
    InitVfs();
    InitTimer();
    InitScheduler();
    InitDiskUtil();

    MarkTfwStartPoint(TFW_SP_AFTER_HEAP);

    InitBootstrapCpu();
    MarkTfwStartPoint(TFW_SP_AFTER_BOOTSTRAP_CPU);

    InitVirt();
    MarkTfwStartPoint(TFW_SP_AFTER_VIRT);

    ReinitPhys();
    MarkTfwStartPoint(TFW_SP_AFTER_PHYS_REINIT);

    InitOtherCpu();
    MarkTfwStartPoint(TFW_SP_AFTER_ALL_CPU);
    InitRandomDevice();
    InitNullDevice();
    InitConsole();
    InitProcess();

    CreateThreadEx(InitThread, NULL, GetVas(), "init", NULL, SCHEDULE_POLICY_FIXED, FIXED_PRIORITY_KERNEL_NORMAL, 0);
    StartMultitasking();
}