/* sort.c 
 *    Test program to sort a large number of integers.
 *
 *    Intention is to stress virtual memory system.
 *
 *    Ideally, we could read the unsorted array off of the file system,
 *	and store the result back to the file system!
 */

#include "syscall.h"
// int A[1024];	/* size of physical memory; with code, we'll run out of space!*/

char input[10];
char output[10];
int fd;
char f1[10] = "1.txt";
char f2[10] = "2.txt";
char f3[10] = "../test/halt";

void func(){
    
	Create(f1);
}

char main()
{

    
	Create(f2);
	Fork(func);
	//fd = Exec(f3);
	//Join(fd);
    Exit(0);

    /*
    Read(input,10,0);
    Create("testFile");
    fd = Open("testFile");
    Write(input,10,fd);
    Close(fd);
    fd = Open("testFile");
    Read(output,10,fd);
    Close(fd);
    Write(output,10,1);
    */

    

    /*
    int i, j, tmp;

    /* first initialize the array, in reverse sorted order */
    /*
    for (i = 0; i < 1024; i++)		
        A[i] = 1024 - i;

    /* then sort! 
    for (i = 0; i < 1023; i++)
        for (j = i; j < (1023 - i); j++)
	   if (A[j] > A[j + 1]) {	// out of order -> need to swap ! 
	      tmp = A[j];
	      A[j] = A[j + 1];
	      A[j + 1] = tmp;
        }
    Halt();*/
    // Exit(A[0]);		// and then we're done -- should be 0! 
}
