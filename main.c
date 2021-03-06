/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *                                                                         *
 *  Dynamic-Recompiler - Turns MIPS code into ARM code                       *
 *  Original source: http://github.com/ricrpi/Dynamic-Recompiler             *
 *  Copyright (C) 2015  Richard Hender                                       *
 *                                                                           *
 *  This program is free software: you can redistribute it and/or modify     *
 *  it under the terms of the GNU General Public License as published by     *
 *  the Free Software Foundation, either version 3 of the License, or        *
 *  (at your option) any later version.                                      *
 *                                                                           *
 *  This program is distributed in the hope that it will be useful,          *
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of           *
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the            *
 *  GNU General Public License for more details.                             *
 *                                                                           *
 *  You should have received a copy of the GNU General Public License        *
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.    *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

#define __USE_GNU

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <sys/mman.h>
#include "rom.h"
#include "memory.h"
#include "CodeSegments.h"
#include "InstructionSetMIPS4.h"
#include "Debugger.h"
#include <signal.h>
#include <sys/ucontext.h>
#include "m64p_types.h"

#ifdef TEST
#include "UT.h"
#endif

extern code_segment_data_t segmentData;

unsigned int SP_DMEM[0x1000/4*2];
unsigned int *SP_IMEM = SP_DMEM+0x1000/4;
unsigned char *SP_DMEMb = (unsigned char *)(SP_DMEM);
unsigned char *SP_IMEMb = (unsigned char*)(SP_DMEM+0x1000/4);

void r4300_reset_hard(void);
void r4300_reset_soft(void);

static void handler(int sig, siginfo_t *si, void *ptr)
{
	static int level 		= 0;
	ucontext_t *ucontext 	= (ucontext_t*)ptr;

	if (sig == SIGSEGV){
		uintptr_t ins_addr;
		uintptr_t ins_addr2;
		#if defined (__i386__) || defined(__x86_64)
			ins_addr = ucontext->uc_mcontext.gregs[14];
			ins_addr2 = ucontext->uc_mcontext.gregs[14];
		#else
			ins_addr = ucontext->uc_mcontext.arm_pc;
			ins_addr2 = ucontext->uc_mcontext.arm_lr - 4U;
		#endif

		printf("\nSegmentation detected trying to access address %p on instruction 0x%x or 0x%x\n", si->si_addr, ins_addr, ins_addr2);
	}
	else if (sig == SIGABRT)
	{
		printf("\nAbort detected\n");
	}
	else if (sig == SIGILL)
	{
		printf("\nIllegal instruction at %p\n", si->si_addr);
		printf_arm(si->si_addr, *((uint32_t*)si->si_addr));
	}
	else if (sig == SIGINT)
	{
		printf("Current segment %P\n\n", segmentData.dbgCurrentSegment);
		while (Debugger_start(&segmentData, &ucontext->uc_mcontext, NULL));
		return;
	}

	if (level > 10)
	{
		printf("Exiting, exceeded signal limit\n");
		exit(0);
	}

	level++;

	printf("Current segment %P\n\n", segmentData.dbgCurrentSegment);

	while (Debugger_start(&segmentData, &ucontext->uc_mcontext, NULL));

	exit(0);
}

// Get the system type associated to a ROM country code.
static m64p_system_type rom_country_code_to_system_type(unsigned short country_code)
{
    switch (country_code & 0xFF)
    {
        // PAL codes
        case 0x44:
        case 0x46:
        case 0x49:
        case 0x50:
        case 0x53:
        case 0x55:
        case 0x58:
        case 0x59:
            return SYSTEM_PAL;

        // NTSC codes
        case 0x37:
        case 0x41:
        case 0x45:
        case 0x4a:
        default: // Fallback for unknown codes
            return SYSTEM_NTSC;
    }
}

void interrupt()
{

}

int main(int argc, char* argv[])
{
	struct sigaction sa;

	sa.sa_flags = SA_SIGINFO | SA_NODEFER;
	sigemptyset(&sa.sa_mask);
	sa.sa_sigaction = handler;

	sigaction(SIGSEGV, &sa, NULL);
	sigaction(SIGABRT, &sa, NULL);
	sigaction(SIGILL, &sa, NULL);
	sigaction(SIGINT, &sa, NULL);
	sigaction(SIGFPE, &sa, NULL);

	printf("R4300 Recompiler\n\n");
#ifndef TEST
	printf("Opening %s\n",argv[1]);
#endif

	FILE *fPtr;
	uint32_t romlength = 0U;

#ifndef TEST
	if (argc <= 1) fPtr = fopen("m64p_test_rom.v64", "rb");
	else fPtr = fopen(argv[1], "rb");

 	if (fPtr == NULL) return 2;

	fseek(fPtr, 0L, SEEK_END);
	romlength = ftell(fPtr);
	fseek(fPtr, 0L, SEEK_SET);
#else
	romlength = 2000U;
#endif

	if (mmap((uint32_t*)(MMAP_BASE)
			, MMAP_BASE_SIZE + romlength
			, PROT_READ|PROT_WRITE|PROT_EXEC
			, MAP_PRIVATE| MAP_FIXED | MAP_ANONYMOUS
			, -1
			, 0 ) != (uint32_t*)(MMAP_BASE))
	{
		printf("Could not mmap\n");
		return 1;
	}

	if (mmap((uint32_t*)(MMAP_PIF_BOOT_ROM)
				, 4096
				, PROT_READ|PROT_WRITE|PROT_EXEC
				, MAP_PRIVATE| MAP_FIXED | MAP_ANONYMOUS
				, -1
				, 0 ) != (uint32_t*)(MMAP_PIF_BOOT_ROM))
		{
			printf("Could not mmap PIF area\n");
			return 1;
		}

#ifdef TEST
	//Generate helper functions
	GenerateCodeSegmentData(romlength);

	//Start testing
	Translation_Test(&segmentData);
#else
	unsigned char imagetype;

	m64p_rom_header ROM_HEADER;

	if (fread((uint32_t*)ROM_ADDRESS , 1, romlength, fPtr) != romlength)
	{
		printf("could not read ROM\n");
		return 3;
	}

	swap_rom((unsigned char*)ROM_ADDRESS, &imagetype, romlength);

	memcpy(&ROM_HEADER, (uint32_t*)ROM_ADDRESS, sizeof(m64p_rom_header));

    ROM_PARAMS.systemtype = rom_country_code_to_system_type(ROM_HEADER.Country_code);

	printf("Name: %s\n", ROM_HEADER.Name);
	printf("Rom size: %ld bytes (or %ld Mb or %ld Megabits)\n", romlength, romlength/1024/1024, romlength/1024/1024*8);
	printf("ClockRate = %x\n", sl(ROM_HEADER.ClockRate));
	printf("Version: %x\n", sl(ROM_HEADER.Release));
	printf("PC = 0x%x\n\n", sl((unsigned int)ROM_HEADER.PC));

	int x;

#if 1
	for (x=0; x< romlength/4; x++)
		*((uint32_t*)ROM_ADDRESS + x) = sl(*((uint32_t*)ROM_ADDRESS + x));
#endif

#if 0
	for (x=0x40/4; x< 200; x++ )
	{
		mips_print((uint32_t)((uint32_t*)ROM_ADDRESS + x), *((uint32_t*)ROM_ADDRESS + x));
	}

	printf("----------------------------\n");
#endif

	//Find all code where we don't know which registers are used
#if 0
	printf("Unknown register usage on these instructions:\n\n");
	for (x=0x40/4; x< romlength/4; x++ )
	{
		uint32_t temp;
		if (ops_regs_input(((uint32_t*)ROM_ADDRESS)[x],&temp,&temp,&temp) == 2
				|| ops_regs_output(((uint32_t*)ROM_ADDRESS)[x],&temp,&temp,&temp) == 2) mips_print((uint32_t*)ROM_ADDRESS + x, ((uint32_t*)ROM_ADDRESS)[x]);
	}

	printf("----------------------------\n");
#endif

	GenerateCodeSegmentData(romlength);

#if 0
	printf("MIPS Address            Length   Regs-cpu   fpu      sp     used Next       Block type 2=end,3=br\n");

	nextCodeSeg = segmentData->StaticSegments;
	int count =0;
	while (nextCodeSeg != NULL && count < 20)
	{
		count++;

		if (nextCodeSeg->MIPSReturnRegister)
		{
			printf("0x%08X 0x%08X %5d      0x%08X %08X %03X     %2d     r%d\n",
				(uint32_t)nextCodeSeg->MIPScode,
				(uint32_t)(nextCodeSeg->MIPScode+nextCodeSeg->MIPScodeLen),
				nextCodeSeg->MIPScodeLen,
				nextCodeSeg->MIPSRegistersUsed[0],
				nextCodeSeg->MIPSRegistersUsed[1],
				nextCodeSeg->MIPSRegistersUsed[2],
				nextCodeSeg->MIPSRegistersUsedCount,
				nextCodeSeg->MIPSReturnRegister);
		}
		else
		{
			printf("0x%08X 0x%08X %5d      0x%08X %08X %02X     %2d   0x%08X %d\n",
				(uint32_t)nextCodeSeg->MIPScode,
				(uint32_t)(nextCodeSeg->MIPScode+nextCodeSeg->MIPScodeLen),
				nextCodeSeg->MIPScodeLen,
				nextCodeSeg->MIPSRegistersUsed[0],
				nextCodeSeg->MIPSRegistersUsed[1],
				nextCodeSeg->MIPSRegistersUsed[2],
				nextCodeSeg->MIPSRegistersUsedCount,
				(uint32_t)nextCodeSeg->MIPSnextInstructionIndex,
				nextCodeSeg->Type);
		}

		nextCodeSeg = nextCodeSeg->next;
	}
	printf("----------------------------\n");
#endif

	printf("%d code segments generated\n", segmentData.count);

	printf("&segmentData.dbgCurrentSegment = 0x%08x\n", (uint32_t)&segmentData.dbgCurrentSegment);

// Instruction Counts for input ROM
#if 0
	code_seg_t* nextCodeSeg;

	uint32_t ins_count[sizeof_mips_op_t];
	uint32_t ins_count_total=0;
	memset(ins_count,0,sizeof(ins_count));

	nextCodeSeg = segmentData.StaticSegments;
	while (nextCodeSeg)
	{
		for (x=0; x < nextCodeSeg->MIPScodeLen; x++)
		{
			ins_count[STRIP(ops_type(*(nextCodeSeg->MIPScode + x)))] ++;
			ins_count_total++;
		}

		nextCodeSeg = nextCodeSeg->next;
	}

	for (x=0; x < sizeof_mips_op_t; x++)
	{
		if (ins_count[x])
		{
			printf("%-9s %7d (%2.2f%%)\n",Instruction_ascii[x], ins_count[x], (double)ins_count[x] * 100 / ins_count_total);
		}
	}
	printf("----------------------------\n");
#endif

	r4300_reset_hard();
	r4300_reset_soft();
	printf("\nFinished processing ROM\n");

#endif

	while (Debugger_start(&segmentData, NULL, NULL));


	// Clean up Recompiler
	freeCodeSegmentData();

	munmap((uint32_t*)MMAP_BASE, MMAP_BASE_SIZE + romlength);
	munmap((uint32_t*)MMAP_PIF_BOOT_ROM, 4096);

	printf("\nEND\n");
	return 0;
}


