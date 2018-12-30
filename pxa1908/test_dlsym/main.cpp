#include <dlfcn.h>
#include <iostream>
#include <binder/MemoryHeapBase.h>

using namespace std;
using namespace android;

    // How to find an unexported function pointer :
    //  Load an exported function
    //  Then subtract its addr by its offset in .so
    //  Then add the unexported function offset in .so
    //  Also, it seems like that the offset is always 0x40245001

int main(int argc, char *argv[])
{
    (void)argc; (void)argv;

    printf("MemoryHeapBase(char*, int, int)\n");
    sp<MemoryHeapBase> memHeap = new MemoryHeapBase("/dev/ion", 9999, 0x600);

    printf("get()\n");
    memHeap.get();
    printf("getHeapID\n");
    memHeap->getHeapID();
    printf("getBase\n");
    memHeap->getBase();
    printf("getSize\n");
    memHeap->getSize();
    printf("getFlags\n");
    memHeap->getFlags();
    printf("getOffset\n");
    memHeap->getOffset();
    printf("getDevice\n");
    memHeap->getDevice();
    

    return 0;
}
