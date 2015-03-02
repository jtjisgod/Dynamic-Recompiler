/*
 * CodeSegments.c
 *
 *  Created on: 16 Apr 2014
 *      Author: rjhender
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "CodeSegments.h"
#include "InstructionSetMIPS4.h"
#include "InstructionSetARM6hf.h"
#include "InstructionSet.h"
#include "Translate.h"

#include "memory.h"
#include "DebugDefines.h"

#include "mem_state.h"
#include "callers.h"

//-------------------------------------------------------------------

code_segment_data_t segmentData;
//static uint8_t* CodeSegBounds;
uint32_t showPrintSegmentDelete = 1;

int32_t UpdateCodeBlockValidity(code_seg_t** const Block, const uint32_t* const address, const uint32_t length, const uint32_t upperAddress);

//==================================================================


/*
 * Function to overwirte the Branch Statement at the end of ARM code so that it points to
 * 	code generated by Generate_BranchStubCode()
 */
void invalidateBranch(code_seg_t* codeSegment)
{
	if (NULL == codeSegment->ARMEntryPoint) return;

	uint32_t* 		out 			= (uint32_t*)codeSegment->ARMcode + codeSegment->ARMcodeLen -1;
	size_t 			targetAddress 	= *((size_t*)(MMAP_FP_BASE + FUNC_GEN_BRANCH_UNKNOWN));
	Instruction_t*	ins = newEmptyInstr();

#ifdef SHOW_CALLER
	printf("invalidateBranch(0x%08x) at 0x%08x\n", (uint32_t)codeSegment, (uint32_t)out);
#endif
	printf_arm((uint32_t)out, *out);

	//Get MIPS condition code for branch
	mips_decode(*(codeSegment->MIPScode + codeSegment->MIPScodeLen -1), ins);
	printf_Intermediate(ins, 1);

	//Set instruction to ARM_BRANCH for new target
	InstrB(ins, ins->cond, targetAddress, 1);

	//emit the arm code
	*out = arm_encode(ins, (size_t)out);

	InstrFree(NULL, ins);
}

//================== Searching ========================================

#if 0
static code_seg_t* Search_MIPS(uint32_t address)
{
	code_seg_t* seg;

	switch ((((uint32_t)address)>>23) & 0xFF)
		{
		case 0x80:
		case 0xA0:
			seg = segmentData.DynamicSegments;
			break;
		case 0x88:
			seg = segmentData.StaticSegments;
			break;
		case 0x90:
			printf("PIF boot ROM: ScanForCode()\n");
			return 0;
		default:
			break;
		}


	while (seg)
	{
		if (address == (uint32_t)seg->MIPScode) return seg;

		seg = seg->next;
	}
	return NULL;
}
#endif



//=================== Intermediate Code ===============================

void freeIntermediateInstructions(code_seg_t* const codeSegment)
{
	Instruction_t *prevInstruction;
	Instruction_t *nextInstruction;

	//remove any existing Intermediate code
	if (codeSegment->Intermcode)
	{
		prevInstruction = codeSegment->Intermcode;

		while (prevInstruction)
		{
			nextInstruction = prevInstruction->nextInstruction;
			free(prevInstruction);
			prevInstruction = nextInstruction;
		}
	}
	codeSegment->Intermcode = NULL;

	freeLiterals(codeSegment);
}

//=================== Segment Linked List =============================
#if 0
static void AddSegmentToLinkedList(code_seg_t* const newSeg)
{
	code_seg_t* seg;
	code_seg_t** pseg;

	static code_seg_t* lastStaticSeg = NULL;

	newSeg->next = NULL;

	//TODO dynamic once DMA is sorted
	seg = segmentData.StaticSegments;
	pseg = &segmentData.StaticSegments;

	if (seg == NULL)
	{
		*pseg = lastStaticSeg = newSeg;
	}
	else if (seg->next == NULL)
	{
		if ((*pseg)->MIPScode < newSeg->MIPScode)
		{
			(*pseg)->next = newSeg;
			newSeg->prev = *pseg;
			lastStaticSeg = newSeg;
		}else
		{
			newSeg->next = *pseg;
			(*pseg)->prev = newSeg;
			*pseg= newSeg;
		}
	}
	else
	{
		//fast forward to last segment
		if (lastStaticSeg->MIPScode < newSeg->MIPScode) seg = lastStaticSeg;

		//TODO could rewind from last ...
		while ((seg->next) && (seg->next->MIPScode < newSeg->MIPScode))
		{
			seg = seg->next;
		}

		if (NULL == seg->next) lastStaticSeg = newSeg;

		// seg->next will either be NULL or seg->next->MIPScode is greater than newSeg->MIPScode
		newSeg->next = seg->next;
		newSeg->prev = seg;

		if (seg->next) seg->next->prev = newSeg;
		seg->next = newSeg;
	}
}

static void RemoveSegmentFromLinkedList(code_seg_t* const codeSegment)
{
	if (codeSegment->prev) codeSegment->prev->next = codeSegment->next;
	if (codeSegment->next) codeSegment->next->prev = codeSegment->prev;
}
#endif
//================ Segment Generation/Linking =========================

code_seg_t* newSegment()
{
	code_seg_t* newSeg = malloc(sizeof(code_seg_t));
	memset(newSeg, 0,sizeof(code_seg_t));

	return newSeg;
}

uint32_t delSegment(code_seg_t* codeSegment)
{
	uint32_t ret = 0;

#if SHOW_PRINT_SEGMENT_DELETE == 2
	{
#elif SHOW_PRINT_SEGMENT_DELETE == 1
	if (showPrintSegmentDelete)
	{
#else
	if (0)
	{
#endif
	printf("deleting Segment 0x%08x for mips code at 0x%08x\n", (uint32_t)codeSegment, (uint32_t)codeSegment->MIPScode);
	}

	freeIntermediateInstructions(codeSegment);	// free memory used for Intermediate Instructions
	freeLiterals(codeSegment);					// free memory used by literals

	updateCallers(codeSegment);					// Update all segments that branch to this one so that they point to stub
	freeCallers(codeSegment);					// free memory used by callers

	//RemoveSegmentFromLinkedList(codeSegment);

	free(codeSegment);

	return ret;
}

int CompileCodeAt(const uint32_t* const address)
{
	int 			x;
	Instruction_e 	op;
	uint32_t 		uiMIPScodeLen = 0;
	code_seg_t* 	newSeg;
	code_seg_t* 	prevSeg = NULL;

	// Find the shortest length of contiguous blocks (super block) of MIPS code at 'address'
	// Contiguous blocks should end with an OPS_JUMP && !OPS_LINK
	while (1) // (index + uiMIPScodeLen < upperAddress/sizeof(code_seg_t*))
	{
		op = ops_type(address[uiMIPScodeLen]);

		if (INVALID == op)
		{
			uiMIPScodeLen = 0;
			break;
		}

		uiMIPScodeLen++;

		if ((op & OPS_JUMP) == OPS_JUMP
				&& (op & OPS_LINK) != OPS_LINK)	//unconditional jump or function return
						break;
	}

	//Now expire the segments within the super block ande remove the segments
	for (x = 0; x < uiMIPScodeLen; x++)
	{
		code_seg_t* toDelete;
		toDelete = getSegmentAt((size_t)(address + x));
		if (toDelete)
		{
			setMemState((size_t)toDelete->MIPScode, toDelete->MIPScodeLen, NULL);
			delSegment(toDelete);

			//we can skip next few words as we have already cleared them.
			x+=toDelete->MIPScodeLen-1;
		}
	}

	int segmentStartIndex = 0;

	// Create new segments
	for (x = 0; x < uiMIPScodeLen; x++)
	{
		op = ops_type(address[x]);

		if ((op & OPS_JUMP) == OPS_JUMP)
		{
			uint32_t uiAddress = ops_JumpAddress(&address[x]);

			newSeg = newSegment();
			newSeg->MIPScode = (uint32_t*)(address + x);
			newSeg->MIPScodeLen = x - segmentStartIndex + 1;

			if (op == JR) //only JR can set PC to the Link Register (or other register!)
			{
				newSeg->MIPSReturnRegister = (*address>>21)&0x1f;
			}

			if (prevSeg)
				prevSeg->pContinueNext = newSeg;
			prevSeg = newSeg;

			if (!segmentStartIndex)
				newSeg->Type = SEG_START;
			else
				newSeg->Type = SEG_END;

			setMemState((size_t)(address + segmentStartIndex), newSeg->MIPScodeLen, newSeg);

			segmentStartIndex = x+1;

			// we should have got to the end of the super block
			if (!(op & OPS_LINK)) break;
		}
		else if((op & OPS_BRANCH) == OPS_BRANCH)	//MIPS does not have an unconditional branch
		{
			int32_t offset =  ops_BranchOffset(&address[x]);

			//Is this an internal branch - need to create two segments
			// if use x<= y + offset then may throw SIGSEGV if offset is -1!
			if (offset < 0 && x + offset >= segmentStartIndex )
			{
				newSeg = newSegment();
				newSeg->MIPScode = (uint32_t*)(address + x);
				newSeg->MIPScodeLen = x + offset - segmentStartIndex + 1;

				if (prevSeg)
					prevSeg->pContinueNext = newSeg;
				prevSeg = newSeg;

				if (!segmentStartIndex)
					newSeg->Type = SEG_START;
				else
					newSeg->Type = SEG_SANDWICH;

				setMemState((size_t)(address + segmentStartIndex), newSeg->MIPScodeLen, newSeg);
				segmentStartIndex = x+1;

				newSeg = newSegment();
				newSeg->MIPScode = (uint32_t*)(address + segmentStartIndex + offset + 1);
				newSeg->MIPScodeLen = -offset;
				newSeg->Type = SEG_SANDWICH;

				prevSeg->pContinueNext = newSeg;
				prevSeg = newSeg;

				setMemState((size_t)(address + segmentStartIndex), newSeg->MIPScodeLen, newSeg);
				segmentStartIndex = x+1;
			}
			else // if we are branching external to the block?
			{

				newSeg = newSegment();
				newSeg->MIPScode = (uint32_t*)(address + x);
				newSeg->MIPScodeLen = x - segmentStartIndex + 1;

				if (prevSeg)
					prevSeg->pContinueNext = newSeg;
				prevSeg = newSeg;

				if (!segmentStartIndex)
					newSeg->Type = SEG_START;
				else
					newSeg->Type = SEG_SANDWICH;

				setMemState((size_t)(address + segmentStartIndex), newSeg->MIPScodeLen, newSeg);
				segmentStartIndex = x+1;
			}
		}
	} // for (x = 0; x < uiMIPScodeLen; x++)

	newSeg = getSegmentAt((size_t)address);

	if (newSeg)
	{
		// Now we can translate and emit code to the next 'break' in instructions
		while (newSeg->pContinueNext)
		{
			segmentData.dbgCurrentSegment = newSeg;
			Translate(newSeg);
			emit_arm_code(newSeg);

			newSeg = newSeg->pContinueNext;
		}

		segmentData.dbgCurrentSegment = newSeg;
		Translate(newSeg);
		emit_arm_code(newSeg);
	}
	else
	{
		printf("CompileCodeAt() failed for adress 0x%08x\n", address);
	}
	return 0;
}

#if 0
/*
 * Generate Code block validity
 *
 * Scan memory for code segments.
 * */
int32_t UpdateCodeBlockValidity(code_seg_t** const Block, const uint32_t* const address, const uint32_t length, const uint32_t upperAddress)
{
	code_seg_t* 	newSeg;

	int32_t 		x, y, z;
	uint32_t 		prevWordCode 	= 0;
	int32_t 		SegmentsCreated = 0;
	int32_t 		percentDone 	= 0;
	int32_t 		percentDone2 	= 0;
	int32_t 		countJumps 		= 0;
	Instruction_e 	op;

	for (x=0; x < length/4; x++)
	{
		percentDone = (400*x/length);

		if ((percentDone%5) == 0 && (percentDone != percentDone2))
		{
			printf("%2d%% done. x %d / %d, jumps %d\n", percentDone, x, length/4, countJumps);
			percentDone2 = percentDone;
		}

		op = ops_type(address[x]);

		if (INVALID == op) continue;

		for (y = x+1; y < length/4; y++)
		{
			op = ops_type(address[y]);

			//we are not in valid code
			if (INVALID == op)
			{
				prevWordCode = 0;
				break;
			}

			if ((op & OPS_JUMP) == OPS_JUMP)
			{
				uint32_t uiAddress = ops_JumpAddress(&address[y]);

				if (op & OPS_LINK)
				{
					if ((y+1 >= length/4) || INVALID == ops_type(address[y+1]))
					{
						prevWordCode = 0;
						break;
					}
				}
				if ( uiAddress >= upperAddress)	// bad offset
				{
					prevWordCode = 0;
					break;
				}

				countJumps++;
				newSeg = newSegment();
				newSeg->MIPScode = (uint32_t*)(address + x);
				newSeg->MIPScodeLen = y - x + 1;

				if (op == JR) //only JR can set PC to the Link Register (or other register!)
				{
					newSeg->MIPSReturnRegister = (*address>>21)&0x1f;
				}

				if (!prevWordCode)
					newSeg->Type = SEG_ALONE;
				else
					newSeg->Type = SEG_END;

				for (z = x; z < y + 1; z++)
				{
					Block[z] = newSeg;
				}

				SegmentsCreated++;
				AddSegmentToLinkedList(newSeg);

				if (op & OPS_LINK) prevWordCode = 1;
				else prevWordCode = 0;
				break;
			}
			else if((op & OPS_BRANCH) == OPS_BRANCH)	//MIPS does not have an unconditional branch
			{
				if ((y+1 >= length/4) || INVALID == ops_type(address[y+1]))
				{
					prevWordCode = 0;
					break;
				}

				int32_t offset =  ops_BranchOffset(&address[y]);

				if ((y + offset >= length/4) || INVALID == ops_type(address[y + offset]))
				{
					prevWordCode = 0;
					break;
				}

				countJumps++;
				newSeg = newSegment();

				//Is this an internal branch - need to create two segments
				// if use x<= y + offset then may throw SIGSEGV if offset is -1!
				if (offset < 0 && x < y + offset)
				{
					newSeg->MIPScode = (uint32_t*)(address + x);
					newSeg->MIPScodeLen = y - x + offset + 1;

					if (!prevWordCode)
						newSeg->Type = SEG_START;
					else
						newSeg->Type = SEG_SANDWICH;

					for (z = x; z < y + offset + 1; z++)
					{
						Block[z] = newSeg;
					}
					SegmentsCreated++;
					AddSegmentToLinkedList(newSeg);

					newSeg = newSegment();
					newSeg->MIPScode = (uint32_t*)(address + y + offset + 1);
					newSeg->MIPScodeLen = -offset;
					newSeg->Type = SEG_SANDWICH;

					for (z = y + offset + 1; z < y + 1; z++)
					{
						Block[z] = newSeg;
					}
					SegmentsCreated++;
					AddSegmentToLinkedList(newSeg);

				}
				else // if we are branching external to the block?
				{
					newSeg->MIPScode = (uint32_t*)(address + x);
					newSeg->MIPScodeLen = y - x + 1;

					if (!prevWordCode)
						newSeg->Type = SEG_START;
					else
						newSeg->Type = SEG_SANDWICH;

					for (z = x; z < y + 1; z++)
					{
						Block[z] = newSeg;
					}
					SegmentsCreated++;
					AddSegmentToLinkedList(newSeg);
				}

				prevWordCode = 1;
				break;
			}
		} // for (y = x; y < length/4; y++)
		x = y;
	} // for (x=0; x < length/4; x++)
	return SegmentsCreated;
}



/*
 * TODO check if linking to an instruction that is NOT the first in a segment
 */
static void LinkStaticSegments()
{
	code_seg_t* 	seg;
	code_seg_t* 	searchSeg;
	Instruction_e 	op;
	uint32_t* 		pMIPSinstuction;
	uint32_t 		uiCountSegsProcessed = 0;

	seg = segmentData.StaticSegments;

	while (seg)
	{
		segmentData.dbgCurrentSegment = seg;

		//if (uiCountSegsProcessed%2000 == 0) printf("linking segment %d (%d%%)\n", uiCountSegsProcessed, uiCountSegsProcessed*100/segmentData.count);
		uiCountSegsProcessed++;
		pMIPSinstuction = (seg->MIPScode + seg->MIPScodeLen - 1);
		
		op = ops_type(*pMIPSinstuction);
		//This segment could branch to itself or another

		if ((op & OPS_JUMP) == OPS_JUMP)
		{
			uint32_t uiAddress = ops_JumpAddress(pMIPSinstuction);

			if ((uint32_t)seg->MIPScode >= 0x881230e4 && (uint32_t)seg->MIPScode < 0x881232e4+100) printf("OPS_JUMP: insAddr 0x%X, Link %d, addr %u (0x%X)\n", (uint32_t)pMIPSinstuction, op & OPS_LINK? 1:0, uiAddress, uiAddress);

			//if (ops_type(*word) == JR) //only JR can set PC to the Link Register (or other register!)
					//(*(seg->MIPScode + seg->MIPScodeLen-1)>>21)&0x1f;
			searchSeg = (segmentData.StaticBounds[uiAddress/4]);

			if (searchSeg)
			{
				seg->pBranchNext = searchSeg;
				addToCallers(seg, searchSeg);
			}
		}
		//TODO use StaticBounds map to get segment for linking
		else if((op & OPS_BRANCH) == OPS_BRANCH)	//MIPS does not have an unconditional branch
		{
			int32_t offset =  ops_BranchOffset(pMIPSinstuction);

			searchSeg = (segmentData.StaticBounds[((uint32_t)seg->MIPScode - MMAP_STATIC_REGION)/4 + offset]);
			if (searchSeg)
			{
				seg->pBranchNext = searchSeg;
				addToCallers(seg, searchSeg);
			}

			/*if (-offset == seg->MIPScodeLen)
			{
				seg->pBranchNext = seg;
				addToCallers(seg, seg);
			}
			else if (offset < 0)
			{
				searchSeg = seg->prev;
				while (searchSeg)
				{
					if (pMIPSinstuction + offset == searchSeg->MIPScode)
					{
						seg->pBranchNext = searchSeg;
						addToCallers(seg, searchSeg);
						break;
					}

					if (pMIPSinstuction + offset < searchSeg->MIPScode) break;

					searchSeg = searchSeg->prev;
				}
			}
			else
			{
				searchSeg = seg->next;
				while (searchSeg)
				{
					if (pMIPSinstuction + offset == searchSeg->MIPScode)
					{
						seg->pBranchNext = searchSeg;
						addToCallers(seg, searchSeg);
						break;
					}

					if (pMIPSinstuction + offset < searchSeg->MIPScode) break;

					searchSeg = searchSeg->next;
				}
			}*/

			seg->pContinueNext = seg->next;
			addToCallers(seg, seg->next);
		}
		else // this must be a continue only segment
		{
			seg->pContinueNext = seg->next;
			addToCallers(seg, seg->next);
		}
		seg = seg->next;
	}
}
#endif
/*
 * Function to Generate a code_segment_data structure
 *
 * It assumes memory has been mapped (at 0x80000000) and the ROM suitably copied into 0x88000000
 */
code_segment_data_t* GenerateCodeSegmentData(const int32_t ROMsize)
{
	//segmentData.StaticSegments = NULL;
	//segmentData.DynamicSegments = NULL;

	memMap_t Blocks[2];

	Blocks[0].address	= 0x88000000;
	Blocks[0].size		= ROMsize;

	Blocks[1].address	= 0x80000000;
	Blocks[1].size		= RD_RAM_SIZE;

	//Initialize the target memory mapping
	initMemState(Blocks, sizeof(Blocks)/sizeof(memMap_t));

	CompileCodeAt(0x88000040);

	segmentData.segStart = Generate_CodeStart(&segmentData);
	emit_arm_code(segmentData.segStart);
	*((uint32_t*)(MMAP_FP_BASE + FUNC_GEN_START)) = (uint32_t)segmentData.segStart->ARMEntryPoint;

	segmentData.segStop = Generate_CodeStop(&segmentData);
	emit_arm_code(segmentData.segStop);
	*((uint32_t*)(MMAP_FP_BASE + FUNC_GEN_STOP)) = (uint32_t)segmentData.segStop->ARMEntryPoint;

	segmentData.segBranchUnknown = Generate_BranchUnknown(&segmentData);
	emit_arm_code(segmentData.segBranchUnknown);
	*((uint32_t*)(MMAP_FP_BASE + FUNC_GEN_BRANCH_UNKNOWN)) = (uint32_t)segmentData.segBranchUnknown->ARMEntryPoint;

	// Compile the First contiguous block of Segments
	code_seg_t* seg = getSegmentAt(0x88000040);

	while (seg->pContinueNext)
	{
		segmentData.dbgCurrentSegment = seg;
		Translate(seg);

		seg = seg->next;
	}

	segmentData.dbgCurrentSegment = seg;
	Translate(seg);

	*((uint32_t*)(MMAP_FP_BASE + RECOMPILED_CODE_START)) = (uint32_t)getSegmentAt(0x88000040)->ARMEntryPoint;

#if 1
	printf("FUNC_GEN_START                   0x%x\n", (uint32_t)segmentData.segStart->ARMEntryPoint);
	printf("FUNC_GEN_STOP                    0x%x\n", (uint32_t)segmentData.segStop->ARMEntryPoint);
	//printf("FUNC_GEN_LOOKUP_VIRTUAL_ADDRESS  0x%x\n", (uint32_t)segmentData.segMem->ARMEntryPoint);
	//printf("FUNC_GEN_INTERRUPT               0x%x\n", (uint32_t)segmentData.segInterrupt->ARMEntryPoint);
	printf("FUNC_GEN_BRANCH_UNKNOWN          0x%x\n", (uint32_t)segmentData.segBranchUnknown->ARMEntryPoint);
	//printf("FUNC_GEN_TRAP                    0x%x\n", (uint32_t)segmentData.segTrap->ARMEntryPoint);
	printf("RECOMPILED_CODE_START            0x%x\n", (uint32_t)getSegmentAt(0x88000040)->ARMEntryPoint);
#endif

	segmentData.dbgCurrentSegment = getSegmentAt(0x88000040);


	return &segmentData;

}
