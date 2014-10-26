/*
 * CountReg.c
 *
 *  Created on: 23 Oct 2014
 *      Author: rjhender
 */

#include "Translate.h"
#include "memory.h"


uint32_t bCountSaturates 	= 0;

/*
 * MIPS4300 has a COUNT register that is decremented every instruction
 * when it underflows, an interrupt is triggered.
 */
void Translate_CountRegister(code_seg_t* const codeSegment)
{
	Instruction_t*ins = codeSegment->Intermcode;
	uint32_t instrCount =0;
	uint32_t instrCountRemaining = codeSegment->MIPScodeLen;

	if (ins == NULL)
	{
		printf("Not initialized this code segment. Please run 'optimize intermediate'\n");
		return;
	}

	if (bCountSaturates)
	{
		printf("Optimize_CountRegister failed. Not implemented QADD \n");
		abort();
	}

#if 0
	//loop through the instructions and update COUNT every countFrequency

	while (ins->nextInstruction->nextInstruction)
	{
		instrCount++;

		if (instrCount >= uiCountFrequency && instrCountRemaining >= uiCountFrequency)
		{
			//add COUNT update
			Instruction_t* newInstruction 	= newEmptyInstr();

			if (bCountSaturates)
			{
				//TODO QADD
			}
			else
			{
				newInstruction = newInstrIS(ARM_ADD,AL,REG_COUNT,REG_COUNT,REG_NOT_USED, uiCountFrequency);
				ADD_LL_NEXT(newInstruction, ins);

				instrCountRemaining -= uiCountFrequency;

				ins = insertCall_To_C(ins, PL, FUNC_GEN_INTERRUPT);
				instrCount = 0;
			}
		}

		ins = ins->nextInstruction;
	}
	//now add a final update before end of function
#endif

	if (instrCount && instrCountRemaining)
	{
		Instruction_t* newInstruction 	= newEmptyInstr();

		//create COUNT update instructions
		if (bCountSaturates)
		{
			//TODO QSUB
		}
		else
		{
			newInstruction = newInstrI(ARM_ADD, AL, REG_COUNT, REG_COUNT, REG_NOT_USED, instrCountRemaining&0xff);
			newInstruction->nextInstruction = ins->nextInstruction;
			ADD_LL_NEXT(newInstruction, ins);

			if (instrCountRemaining > 255)
			{
				newInstruction = newInstrI(ARM_ADD, AL, REG_COUNT, REG_COUNT, REG_NOT_USED, instrCountRemaining&0xff00);
				ADD_LL_NEXT(newInstruction, ins);
			}

			newInstruction = newInstrS(ARM_CMP, AL, REG_NOT_USED, REG_COMPARE, REG_COUNT);
			ADD_LL_NEXT(newInstruction, ins);

			// We need to set IP7 of the Cause Register and call cc_interrupt()
			newInstruction = newInstrI(ARM_ORR, AL, REG_CAUSE, REG_CAUSE, REG_NOT_USED, 0x8000);
			ADD_LL_NEXT(newInstruction, ins);

			newInstruction = newInstrI(ARM_B, PL, REG_NOT_USED, REG_NOT_USED, REG_NOT_USED, MMAP_FP_BASE + FUNC_GEN_INTERRUPT);
			newInstruction->Ln = 1;
			newInstruction->I = 1;
			ADD_LL_NEXT(newInstruction, ins);
			return;
		}
	}
}