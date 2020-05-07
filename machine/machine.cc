// machine.cc 
//	Routines for simulating the execution of user programs.
//
//  DO NOT CHANGE -- part of the machine emulation
//
// Copyright (c) 1992-1993 The Regents of the University of California.
// All rights reserved.  See copyright.h for copyright notice and limitation 
// of liability and disclaimer of warranty provisions.

#include "copyright.h"
#include "machine.h"
#include "system.h"

// Textual names of the exceptions that can be generated by user program
// execution, for debugging.
static char* exceptionNames[] = { "no exception", "syscall", 
				"page fault/no TLB entry", "page read only",
				"bus error", "address error", "overflow",
				"illegal instruction" };

//----------------------------------------------------------------------
// CheckEndian
// 	Check to be sure that the host really uses the format it says it 
//	does, for storing the bytes of an integer.  Stop on error.
//----------------------------------------------------------------------

static
void CheckEndian()
{
    union checkit {
        char charword[4];
        unsigned int intword;
    } check;

    check.charword[0] = 1;
    check.charword[1] = 2;
    check.charword[2] = 3;
    check.charword[3] = 4;

#ifdef HOST_IS_BIG_ENDIAN
    ASSERT (check.intword == 0x01020304);
#else
    ASSERT (check.intword == 0x04030201);
#endif
}

//----------------------------------------------------------------------
// Machine::Machine
// 	Initialize the simulation of user program execution.
//
//	"debug" -- if TRUE, drop into the debugger after each user instruction
//		is executed.
//----------------------------------------------------------------------

Machine::Machine(bool debug)
{
    int i;

    for (i = 0; i < NumTotalRegs; i++)
        registers[i] = 0;
    mainMemory = new char[MemorySize];
    for (i = 0; i < MemorySize; i++)
      	mainMemory[i] = 0;
    for (i = 0; i < NumPhysPages; i++){
        memoryBitmap[i] = false;
    }
    for (i = 0 ; i < NumPhysPages; i++){
        rt_page_table[i].physicalPage = i;
        rt_page_table[i].virtualPage = -1;
        rt_page_table[i].valid = false;
        rt_page_table[i].tid = -1;
        rt_page_table[i].readOnly = false;
        rt_page_table[i].use = false;
        rt_page_table[i].LRU = -1;
    }
#ifdef USE_TLB
    tlb = new TranslationEntry[TLBSize];
    for (i = 0; i < TLBSize; i++){
        tlb[i].valid = false;
        tlb[i].LRU = -1;
    };
    tlbsize = TLBSize;
    pageTable = NULL;
#else	// use linear page table
    tlb = NULL;
    pageTable = NULL;
#endif

    singleStep = debug;
    CheckEndian();
}

//----------------------------------------------------------------------
// Machine::~Machine
// 	De-allocate the data structures used to simulate user program execution.
//----------------------------------------------------------------------

Machine::~Machine()
{
    delete [] mainMemory;
    if (tlb != NULL){
        printf("TLB Hit Rate: %d / %d =  %lf",tlbHits,tlbTimes,float(tlbHits)/float(tlbTimes));
        delete [] tlb;
    }
 
    
}

//----------------------------------------------------------------------
// Machine::RaiseException
// 	Transfer control to the Nachos kernel from user mode, because
//	the user program either invoked a system call, or some exception
//	occured (such as the address translation failed).
//
//	"which" -- the cause of the kernel trap
//	"badVaddr" -- the virtual address causing the trap, if appropriate
//----------------------------------------------------------------------

void
Machine::RaiseException(ExceptionType which, int badVAddr)
{
    DEBUG('m', "Exception: %s\n", exceptionNames[which]);
    
//  ASSERT(interrupt->getStatus() == UserMode);
    registers[BadVAddrReg] = badVAddr;
    DelayedLoad(0, 0);			// finish anything in progress
    interrupt->setStatus(SystemMode);
    ExceptionHandler(which);		// interrupts are enabled at this point
    interrupt->setStatus(UserMode);
}



void 
Machine::tlbReplace(int address) {
    // printf("Entering TLB Replacement\n");
	unsigned int vpn = (unsigned)address / PageSize;
    int position = -1;
    for (int i = 0; i < TLBSize ; ++i ) {
        if (tlb[i].valid == false){
            position = i;
            break;
        }
    }
    /*
    if (position == -1){ // FIFO
        for (int i = 0 ; i < TLBSize-1 ; ++i){
            tlb[i] = tlb[i+1];
        }
        position = TLBSize-1;
    }
    */
    /*
    if (position == -1){  // random replacement
        position = Random()%TLBSize;
    }
    */

    if (position == -1){  // Least Recently Used 
        int minTag = TLBSize;
        for (int i = 0 ; i < TLBSize; i++){
            if (tlb[i].LRU < minTag){
                minTag = tlb[i].LRU;
                position = i;
            }
        }
        for( int j = 0 ; j < TLBSize ; j++){
            if ( j == position) continue;
            tlb[j].LRU--;
        }
        tlb[position].LRU = TLBSize-1;
    }
    tlb[position].virtualPage = vpn;
    tlb[position].physicalPage = pageTable[vpn].physicalPage;
    tlb[position].valid= true;
    tlb[position].use = false;
    tlb[position].dirty = false;
    
}


void 
Machine::ReverseTableReplace(int address) {
    unsigned int vpn = (unsigned)address / PageSize;
    int position = -1;
    for (int i = 0; i < TLBSize ; ++i ) {
        if (rt_page_table[i].valid == false){
            position = i;
            break;
        }
    }
    /*
    if (position == -1){  // Least Recently Used 
        int minTag = NumPhysPages;
        for (int i = 0 ; i < NumPhysPages ; i++){
            if (rt_page_table[i].LRU < NumPhysPages){
                minTag = rt_page_table[i].LRU;
                position = i;
            }
        }
        for( int j = 0 ; j < NumPhysPages ; j++){
            if ( j == position) continue;
            rt_page_table[j].LRU--;
        }
        rt_page_table[position].LRU = NumPhysPages-1;
    }*/
    if (position == -1){  // random replacement
        position = Random()%NumPhysPages;
    }

    // If dirty,write back to disk
    if (rt_page_table[position].valid == true && rt_page_table[position].dirty == true){
        int vpn = rt_page_table[position].virtualPage;
        int ppn = rt_page_table[position].physicalPage;
        int tid = rt_page_table[position].tid;
        for ( int j = 0 ; j < PageSize ; ++j){
            thread_poiners[tid]->space->disk[vpn*PageSize+j] = mainMemory[ppn*PageSize+j];
        }
    }

    // read from disk 
    int ppn = rt_page_table[position].physicalPage;
    
    for ( int j = 0 ; j < PageSize; j++){
        mainMemory[ppn*PageSize+j] = thread_poiners[currentThread->GetThreadID()]->space->disk[vpn*PageSize+j];
    }
    rt_page_table[position].virtualPage = vpn;
    rt_page_table[position].valid = true;
    rt_page_table[position].dirty = false;
    rt_page_table[position].tid = currentThread->GetThreadID();
   // printf("In thread %d,get physical page %d from virtual disk page %d\n",currentThread->GetThreadID(),ppn,vpn);

}


//----------------------------------------------------------------------
// Machine::Debugger
// 	Primitive debugger for user programs.  Note that we can't use
//	gdb to debug user programs, since gdb doesn't run on top of Nachos.
//	It could, but you'd have to implement *a lot* more system calls
//	to get it to work!
//
//	So just allow single-stepping, and printing the contents of memory.
//----------------------------------------------------------------------

void Machine::Debugger()
{
    char *buf = new char[80];
    int num;

    interrupt->DumpState();
    DumpState();
    printf("%d> ", stats->totalTicks);
    fflush(stdout);
    fgets(buf, 80, stdin);
    if (sscanf(buf, "%d", &num) == 1)
	runUntilTime = num;
    else {
	runUntilTime = 0;
	switch (*buf) {
	  case '\n':
	    break;
	    
	  case 'c':
	    singleStep = FALSE;
	    break;
	    
	  case '?':
	    printf("Machine commands:\n");
	    printf("    <return>  execute one instruction\n");
	    printf("    <number>  run until the given timer tick\n");
	    printf("    c         run until completion\n");
	    printf("    ?         print help message\n");
	    break;
	}
    }
    delete [] buf;
}
 
//----------------------------------------------------------------------
// Machine::DumpState
// 	Print the user program's CPU state.  We might print the contents
//	of memory, but that seemed like overkill.
//----------------------------------------------------------------------

void
Machine::DumpState()
{
    int i;
    
    printf("Machine registers:\n");
    for (i = 0; i < NumGPRegs; i++)
	switch (i) {
	  case StackReg:
	    printf("\tSP(%d):\t0x%x%s", i, registers[i],
		   ((i % 4) == 3) ? "\n" : "");
	    break;
	    
	  case RetAddrReg:
	    printf("\tRA(%d):\t0x%x%s", i, registers[i],
		   ((i % 4) == 3) ? "\n" : "");
	    break;
	  
	  default:
	    printf("\t%d:\t0x%x%s", i, registers[i],
		   ((i % 4) == 3) ? "\n" : "");
	    break;
	}
    
    printf("\tHi:\t0x%x", registers[HiReg]);
    printf("\tLo:\t0x%x\n", registers[LoReg]);
    printf("\tPC:\t0x%x", registers[PCReg]);
    printf("\tNextPC:\t0x%x", registers[NextPCReg]);
    printf("\tPrevPC:\t0x%x\n", registers[PrevPCReg]);
    printf("\tLoad:\t0x%x", registers[LoadReg]);
    printf("\tLoadV:\t0x%x\n", registers[LoadValueReg]);
    printf("\n");
}

//----------------------------------------------------------------------
// Machine::ReadRegister/WriteRegister
//   	Fetch or write the contents of a user program register.
//----------------------------------------------------------------------

int Machine::ReadRegister(int num)
    {
	ASSERT((num >= 0) && (num < NumTotalRegs));
	return registers[num];
    }

void Machine::WriteRegister(int num, int value)
    {
	ASSERT((num >= 0) && (num < NumTotalRegs));
	// DEBUG('m', "WriteRegister %d, value %d\n", num, value);
	registers[num] = value;
    }

void
Machine::AdvancePC(){
	WriteRegister(PrevPCReg,registers[PCReg]);
	WriteRegister(PCReg,registers[PCReg]+4);
	WriteRegister(NextPCReg,registers[NextPCReg]+4);
}