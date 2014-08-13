/*
 * CodeSegments.h
 *
 *  Created on: 16 Apr 2014
 *      Author: rjhender
 */

#ifndef CODESEGMENTS_H_
#define CODESEGMENTS_H_

#include <stdint.h>
#include "InstructionSet.h"

//-------------------------------------------------------------------

/*
 * want to know if:
 * 1. segment has any code before
 * 2. segment has any code after
 */
typedef enum
{
	BLOCK_INVALID,			// invalid code section
	BLOCK_START,			// start of a block CPU will only ever jump to this
	BLOCK_START_CONT,		// start of a block, there is a valid block before
	BLOCK_PART,				// within a block
	BLOCK_END,				// block will not continue after this point (e.g. Jump with no link)
	BLOCK_END_CONT,			// block may/will continue after end of segment e.g. conditional branch, jump/branch with link etc.
	sizeof_BLOCK_TYPE_E
} block_type_e;

static const char* block_type_s[sizeof_BLOCK_TYPE_E] = {
	"BLOCK_INVALID",
	"BLOCK_START",
	"BLOCK_START_CONT",
	"BLOCK_PART",
	"BLOCK_END",
	"BLOCK_END_CONT"
};

typedef enum
{
	SEG_SANDWICH,		// segment has code before and after - literals need to be global
	SEG_START,			// segment has no code before - literals go before
	SEG_END,			// segment has no code after - literals go after
	SEG_ALONE,			// segment has no code before or after - literals can go either side
	sizeof_SEG_TYPE_E
} seg_type_e;

static const char* seg_type_s[sizeof_SEG_TYPE_E] = {
		"SEG_SANDWICH",		// segment has code before and after - literals need to be global
		"SEG_START",			// segment has no code before - literals go before
		"SEG_END",			// segment has no code after - literals go after
		"SEG_ALONE"
};

typedef struct _literal_t
{
	struct _literal_t* next;
	int32_t value;
} literal_t;

typedef struct _caller_t
{
	struct _caller_t* next;
	struct _code_seg* codeSeg;
} caller_t;

typedef struct _code_seg
{
	struct _code_seg* next;		//next code segment in linked list
	struct _code_seg* prev;		//previous code segment in linked list
	seg_type_e Type;

	literal_t* literals;

	uint32_t* MIPScode;						// an index to mips code
	uint32_t MIPScodeLen;					// a length of mips code
	uint32_t MIPSReturnRegister;			// boolean segments returns;
	uint32_t* MIPSnextInstructionIndex;

	uint32_t MIPSRegistersUsed[3];			// The registers read/written by segment

	uint32_t MIPSRegistersUsedCount;		// Count of the registers read/written by segment

	uint32_t* ARMcode;						// a pointer to arm code
	uint32_t ARMcodeLen;					// a length to arm code

	Instruction_t* Intermcode;				// a pointer to Intermediate code
	//uint32_t IntermcodeLen;				// length to Intermediate code

	struct _code_seg* pBranchNext;		// the code segment(s) we may branch to. will need relinking after DMA moves
	struct _code_seg* pContinueNext;	// the code segment(s) we may continue to. will need relinking after DMA moves

	caller_t* callers;			// array of code segments that may call this segment

} code_seg_t;


typedef struct _code_segment_data
{
	uint32_t count;
	code_seg_t* StaticSegments;		// code run directly in ROM
	int8_t* StaticBounds;
	code_seg_t* DynamicSegments;	// code running in RDRAM (copied or DMA'd from ROM)
	int8_t* DynamicBounds;
	literal_t* literals;
} code_segment_data_t;

//-------------------------------------------------------------------

/*
 * Function to create a newSegment
 */
code_seg_t* newSegment();

/*
 * Function to destroy a codeSegment
 */
uint32_t delSegment(code_seg_t* codeSegment);

/*
 * Function to walk the Intermediate Instruction' LinkedList and free it.
 */
void freeIntermediateInstructions(code_seg_t* codeSegment);

code_segment_data_t* GenerateCodeSegmentData(const int32_t ROMsize);

uint32_t addLiteral(code_seg_t* const codeSegment, reg_t* const base, uint32_t* const offset, const uint32_t value);

int32_t ScanForCode(const uint32_t* const address, const uint32_t length);

#endif /* CODESEGMENTS_H_ */
