/******************************************************************************
 * K42: (C) Copyright IBM Corp. 2000.
 * All Rights Reserved
 *
 * This file is distributed under the GNU LGPL. You should have
 * received a copy of the license along with K42; see the file LICENSE.html
 * in the top-level directory for more details.
 *
 * $Id: kinit.C,v 1.127 2005/02/16 00:06:24 mergen Exp $
 *****************************************************************************/
#include "kernIncs.H"
#include <sys/thinwire.H>
#include <bilge/arch/powerpc/openfirm.h>
#include <bilge/arch/powerpc/BootInfo.H>
#include <bilge/arch/powerpc/simos.H>
#include <exception/HWInterrupt.H>
#include "bilge/LocalConsole.H"
#include "bilge/HWPerfMon.H"
#include <scheduler/Scheduler.H>
#include <init/kernel.H>
#include <trace/traceBase.H>
#include <init/BootPrintf.H>
#include <sys/thinwire.H>
#include <sys/IOChan.H>
#include <sys/HVChan.H>
#include <sys/GDBIO.H>

#define MSR_SIM		0x0000000020000000
#define MSR_MAMBO       0x0000000010000000

extern char bootData[BOOT_DATA_MAX];

extern uval physical_serial_port_addr;

BootPrintf::StaticStuff BootPrintf::staticStuff;

void writeCOM2(char c);
void writeCOM2Hex(uval x);
void writeCOM2Str(char *str);

// Defining instance of _BootInfo (declared in bilge/arch/powerpc/BootInfo.H).
struct BootInfo *_BootInfo;
struct BootInfo b;
#undef systemcfg
extern struct BootInfo *systemcfg;

// Defining instance of _OnSim (declared in kernel.H).
uval _OnSim;
uval _OnHV;

extern code kernVirtStart;

extern "C" void marctest();

extern "C" unsigned long lmb_alloc_base(u64 size, u64 align, u64 max_addr);

extern void scanLMBForMem(MemoryMgrPrimitiveKern* memory);

/* For debugging on HV */
#if 0
extern "C" void __hcall(uval64 r3, uval64 r4, uval64 r5, uval64 r6, uval64 r7);
void print(const char* x)
{
    int j = 0;
    int l = 0;
    while (x[l]) ++l;

    while (l - j > 16) {
	__hcall(0x58ULL, 0, 16, *(uval64*)(x + j), *(uval64*)(x + j + 8));
	j += 16;
    }
    __hcall(0x58ULL, 0, l-j, *(uval64*)(x + j), *(uval64*)(x + j + 8));
}
#endif



#define _STR(x) #x
#define STR(x) _STR(x)

#if 0
void
setHID() {

    if (_BootInfo->cpu_version == PV_970 ||
	_BootInfo->cpu_version == PV_970FX) {
	static uval hidInit = 0;
//      one ppc instruction per bundle
//	static uval hid0 = 0x8030100080000000ULL;
//      one ppc instruction per bundle, serialized issue
//	static uval hid0 = 0x8030180080000000ULL;

	static uval hid0 = 0x0030100080000000ULL;
	static uval hid1 = 0xfd3c200000000000ULL;
	static uval hid4 = 0ULL;
	static uval hid5 = 0ULL;

	// These sequences are what Book IV specifies
	// Note that Apple FW uses different settings, in particular
	// for HID5 which changes dcbz size to 32 instead of 128
	asm volatile(
	    "cmpdi	%4, 0\n\t"
	    "bne	1f\n\t"
	    "mfspr	r9, " STR(SPRN_HID0) "\n\t"
	    "or		%0, %0, r9\n\t"
	    "mfspr	r9, " STR(SPRN_HID1) "\n\t"
	    "or		%1, %1, r9\n\t"
	    "mfspr	r9, " STR(SPRN_HID4) "\n\t"
	    "or		%2, %2, r9\n\t"
	    "mfspr	r9, " STR(SPRN_HID5) "\n\t"
	    "or		%3, %3, r9\n\t"
	    "1:\n\t"
	    "sync\n\t"
	    "mtspr	" STR(SPRN_HID0) ",%0\n\t"
	    "mfspr	%0, " STR(SPRN_HID0) "\n\t"
	    "mfspr	%0, " STR(SPRN_HID0) "\n\t"
	    "mfspr	%0, " STR(SPRN_HID0) "\n\t"
	    "mfspr	%0, " STR(SPRN_HID0) "\n\t"
	    "mfspr	%0, " STR(SPRN_HID0) "\n\t"
	    "mfspr	%0, " STR(SPRN_HID0) "\n\t"
	    "isync\n\t"
	    "sync\n\t"
	    "mtspr	" STR(SPRN_HID1) ",%1\n\t"
	    "mtspr	" STR(SPRN_HID1) ",%1\n\t"
	    "isync\n\t"
	    "sync\n\t"
	    "mtspr	" STR(SPRN_HID4) ",%2\n\t"
	    "isync\n\t"
	    "sync\n\t"
	    "mtspr	" STR(SPRN_HID5) ",%3\n\t"
	    "isync\n\t"
	    : "=&r"(hid0), "=&r"(hid1), "=&r"(hid4), "=&r"(hid5)
	    : "0"(hid0), "1"(hid1), "2"(hid4), "3"(hid5), "r"(hidInit)
	    :"r9");
	hidInit = 1;
    }
}
#endif
extern uval FIXME_LOGPTES;


extern "C" void
start(BootInfo *bootInfo)
{
    /*
     * We don't want to rely on the boot program to install the MSR we want,
     * so set it ourselves.
     */
    asm volatile ("mtmsrd %0; isync" : : "r" (PSL_KERNELSET&(~PSL_ISF)));

    /*
     * Initialize CurrentThread register to catch early bogus uses.
     */
    asm volatile("mr r13,%0" : : "r" (0xdeaddeaddeadbeef) : "r13");

    /*
     * Initialize all the floating point registers.
     */
    float tmp __attribute__((unused))= 0.0;
    asm volatile (
	"lfd f0,%0\n\t"
	"fmr f1,f0\n\t"
	"fmr f2,f0\n\t"
	"fmr f3,f0\n\t"
	"fmr f4,f0\n\t"
	"fmr f5,f0\n\t"
	"fmr f6,f0\n\t"
	"fmr f7,f0\n\t"
	"fmr f8,f0\n\t"
	"fmr f9,f0\n\t"
	"fmr f10,f0\n\t"
	"fmr f11,f0\n\t"
	"fmr f12,f0\n\t"
	"fmr f13,f0\n\t"
	"fmr f14,f0\n\t"
	"fmr f15,f0\n\t"
	"fmr f16,f0\n\t"
	"fmr f17,f0\n\t"
	"fmr f18,f0\n\t"
	"fmr f19,f0\n\t"
	"fmr f20,f0\n\t"
	"fmr f21,f0\n\t"
	"fmr f22,f0\n\t"
	"fmr f23,f0\n\t"
	"fmr f24,f0\n\t"
	"fmr f25,f0\n\t"
	"fmr f26,f0\n\t"
	"fmr f27,f0\n\t"
	"fmr f28,f0\n\t"
	"fmr f29,f0\n\t"
	"fmr f30,f0\n\t"
	"fmr f31,f0\n\t"
	"mtfsf 0xff,f0\n\t" : : "m" (tmp));

    struct KernelInitArgs kernelInitArgs;
    MemoryMgrPrimitiveKern *memory = &kernelInitArgs.memory;
    _BootInfo = bootInfo;
    _OnSim = _BootInfo->onSim;
    _OnHV  = _BootInfo->onHV;

    //We rely on Linux's __970_cpu_preinit to deal with this for us.
#if 0
    if (!_OnHV) setHID();
#endif

    kernelInitArgs.vp = 0;
    kernelInitArgs.barrierP = 0;
    kernelInitArgs.sharedIPT = 0;

    /*
     * We want to map all of physical memory into a V->R region.  We choose a
     * base for the V->R region (virtBase) that makes the kernel land correctly
     * at its link origin, &kernVirtStart.  This link origin must wind up
     * mapped to the physical location at which the kernel was loaded
     * (_BootInfo->kernelImage).
     */
    uval virtBase = (uval) (&kernVirtStart - _BootInfo->kernelImage);

#if 0
    if (_BootInfo->platform & PLATFORM_PSERIES) {
	rtas_init(virtBase);
    }
#endif

    /*
     * Not all of physical memory is addressable yet.  The boot program maps
     * some amount of memory beyond the kernel which we can use as an initial
     * page pool for early allocation.
     */
#if 0
    extern int _end[];

    uval allocStart = PAGE_ROUND_UP(uval(_end));
    uval allocEnd = PAGE_ROUND_DOWN(virtBase + _BootInfo->kernelImage +
						_BootInfo->kernelImageSize);
#else
    uval spill = 35 * 1024 * 1024;
    uval allocStart = virtBase + lmb_alloc_base(spill, 8, SEGMENT_SIZE);
    allocStart = PAGE_ROUND_UP(allocStart);
    uval allocEnd = allocStart + spill;
    allocEnd = PAGE_ROUND_DOWN(allocEnd);
#endif

    uval physStart = 0;
    uval physEnd = _BootInfo->physEnd;

    memory->init(physStart, physEnd, virtBase, allocStart, allocEnd);

    BootPrintf::Init(memory);

    BootPrintf::Printf("MemoryMgrPrimitive allocStart %lx allocEnd %lx\n",
		       allocStart, allocEnd);


    //Read the LMB structures to figure out what memory should be
    //given to the allocator. allocEnd tells this code where the free
    //memory really begins.  The LMB may be out of date, due to a
    //download of a new kernel.
    scanLMBForMem(memory);


    HWInterrupt::Init();

    if (_BootInfo->platform == PLATFORM_PSERIES) {
	/*
	 * Initialize Performance Monitoring Hardware to a known state
	 */
	HWPerfMon::VPInit();
    }

    InvertedPageTable::ClassInit(memory);

    InitKernelMappings(kernelInitArgs);

    /*
     * Hardware interrupts have been disabled up this point.  We enable now
     * to avoid assertions in subsequent boot code.  We're not yet ready for
     * timer or external interrupts, so null handlers have been established
     * for them.
     */
    enableHardwareInterrupts();

    if (_OnSim && !_OnHV) simosThinIPInit(memory);
    ThinWireChan::ClassInit(0, memory);

    {
	IOChan* console = NULL;
	IOChan* gdb = NULL;
	uval useVty = 0;
	if ((useVty || KernelInfo::OnSim()) && KernelInfo::OnHV()) {
	    console = new(memory) HVChannel(CONSOLE_CHANNEL);
	    gdb = new(memory) HVChannel(GDB_CHANNEL);
	} else if (KernelInfo::OnSim()) {
	    console = getSimConChan(memory);
	} else {
	    /* hardware only uses thinwire */
	    console = new(memory) ThinWireChan(CONSOLE_CHANNEL);
	    gdb = new(memory) ThinWireChan(GDB_CHANNEL);
	}


	GDBIO::GDBChan = gdb;
	LocalConsole::Init(0, memory, console);
    }

    BootPrintf::PrintAll(memory);
    err_printf("Calling KernelInit.C (first real printf) %p\n", _BootInfo);
    err_printf("Initial page pool: %lx %lx\n",allocStart, allocEnd);
    KernelInit(kernelInitArgs);
    /* NOTREACHED */
}

/* This is our entry point from the Linux boot program.  This should be
 * the first K42 code executed in the process of bringing a machine up.
 */
extern "C" void start_kernel(void)
{
  long msr;

  /* Fill out the first part from Linux's systemcfg structure.  */
  b.eye_catcher[0] = systemcfg->eye_catcher[0];
  b.version.major = systemcfg->version.major;
  b.version.minor = systemcfg->version.minor;
  b.platform = systemcfg->platform;
  b.processor = systemcfg->processor;
  b.processorCount = systemcfg->processorCount;
  b.tb_orig_stamp = systemcfg->tb_orig_stamp;
  b.tb_ticks_per_sec = systemcfg->tb_ticks_per_sec;
  b.tb_to_xs = systemcfg->tb_to_xs;
  b.stamp_xsec = systemcfg->stamp_xsec;
  b.tb_update_count = systemcfg->tb_update_count;
  b.tz_minuteswest = systemcfg->tz_minuteswest;
  b.tz_dsttime = systemcfg->tz_dsttime;
  b.dCacheL1Size = systemcfg->dCacheL1Size;
  b.dCacheL1LineSize = systemcfg->dCacheL1LineSize;
  b.iCacheL1Size = systemcfg->iCacheL1Size;
  b.iCacheL1LineSize = systemcfg->iCacheL1LineSize;
  b.physicalMemorySize = systemcfg->physicalMemorySize;

  extern unsigned long lmb_end_of_DRAM(void);
  b.physEnd = lmb_end_of_DRAM();

  asm volatile("mfmsr %0" : "=r" (msr));
  if (msr & MSR_SIM) {
    b.onSim = (msr & MSR_MAMBO) ? SIM_MAMBO : SIM_SIMOSPPC;
  } else {
    b.onSim = SIM_NONE;
  }

  if (b.platform == PLATFORM_POWERMAC) {
    b.naca.serialPortAddr = 0x80013020;
  } else if (b.platform == PLATFORM_MAPLE) {
    b.naca.serialPortAddr = physical_serial_port_addr;
  } else {
    b.naca.serialPortAddr = 0x0;
  }

  /* Should be 0 for a cold boot, 1 for a fast reboot.  */
  b.wireInit = 0;

  /* Fill in the rest with K42-specific values.  */
  b.rtas.entry = 0x1ff4000;
  b.rtas.base = 0x1ff4000;
  b.rtas.size = 0x8000;
  b.rtas.lock[0] = 0x0;
  b.rtas.lock[1] = 0x0;
  b.rtas.dev = 0x0;
  b.prom.entry = 0xf0000000;
  b.prom.root = 0x1;
  b.prom.chosen = 0x1b;
  b.prom.cpu = 0x0;
  b.prom.stdout = 0x8;
  b.prom.disp_node = 0x0;
  b.onHV = 0x0;
  b.wireChanOffset = 0x0;

  b.argString = 0x0;
  b.argLength = 0x0;
  b.controlFlags = 0x0;
  b.availCPUs = 0x1;
  b.masterCPU = 0x0;
  b.cpu_version = b.processor >> 16;
  b.clock_frequency = 0x77359400;
  b.bus_frequency = 0x77359400;
  b.timebase_frequency = 0x77359400;
  b.kernelImage = 0x0;
  b.kernelImageSize = 0x3000000;
  b.rebootImage = 0x6000000;
  b.rebootImageSize = 0x2000000, 
  b.L2cachesize = 0x400000;
  b.L2linesize = 0x80;
  b.hwData = 0x5f00000;
  b.hwDataSize = 0x18a8;
  b.get_time_of_day = 0x0;
  b.set_time_of_day = 0x0;
  b.display_character = 0x0;
  b.set_indicator = 0x0;
  b.power_off = 0x0;
  b.system_reboot = 0x0;
  b.read_pci_config = 0x0;
  b.write_pci_config = 0x0;
  b.freeze_time_base = 0x1e;
  b.thaw_time_base = 0x1f;
  b.event_scan = 0x0;
  b.check_exception = 0x0;
  b.rtas_last_error = 0x0;
  b.ibm_scan_log_dump = 0xffffffff;

  for (int i = 0; i < BOOT_DATA_MAX; i++) {
    b.boot_data[i] = bootData[i];
  }
  
  start(&b);
}
