#include <machine/dev.h>
#include <machine/portio.h>
#include <driver.h>
#include <thread.h>
#include <panic.h>
#include <log.h>
#include <virtual.h>

static size_t Loadx86Driver(const char* filename, const char* init) {
    int res = RequireDriver(filename);
    if (res != 0) {
        PanicEx(PANIC_REQUIRED_DRIVER_NOT_FOUND, filename);
    }

    size_t addr = GetSymbolAddress(init);
    if (addr == 0) {
        PanicEx(PANIC_REQUIRED_DRIVER_MISSING_SYMBOL, filename);
    }

    return addr;
}

static void LoadSlowDriversInBackground(void*) {
    /*
     * To make the OS boot faster, we'll load the less critical, and slower, drivers in
     * in a new thread. This means we can continue initialising the rest of the OS while
     * drivers load.
     */ 
    ((void(*)()) (Loadx86Driver("sys:/acpica.sys", "InitAcpica")))();
}

void ArchInitDev(bool fs) {
    if (!fs) {
        InitIde();

    } else {
        ((void(*)()) (Loadx86Driver("sys:/vesa.sys", "InitVesa")))();
        ((void(*)()) (Loadx86Driver("sys:/ps2.sys", "InitPs2")))();
        //((void(*)()) (Loadx86Driver("sys:/vga.sys", "InitVga")))();
        CreateThread(LoadSlowDriversInBackground, NULL, GetVas(), "drvloader");
    }
}