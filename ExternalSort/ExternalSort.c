#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <math.h>
#include <string.h>
#include <sys/select.h>
#include <sys/types.h>
#include <limits.h>
#include <time.h>

//limits the length of the binary path to each node
#define MAX_PATH 4
//max number of ints allowed per file
#define MAX_INT_IN_FILE 100
#define READ 0
#define WRITE 1
#define LEFT 0
#define RIGHT 1
#define UP 2

int isPowerOfTwo(unsigned int);
void spawnChildren(int);
void readSort();
void swap(int*, int*);
void sort(int*, int);
int toBinary(char*);

//array containing the filenames
char** filenames;
//binary path taken to each node
char* path;
//left, right, or up / read or write
int fd[3][2];
int max_levels;
int isMaster;
int* intStore;
FILE* output;

int
main(int argc, char* argv[])
{
    output = fopen("final", "wb");

    argc--;
    path = malloc(MAX_PATH * sizeof(char));
    isMaster = 1;

    //only works with number of files that are a power of two!
    if(!isPowerOfTwo(argc))
    {
        printf("# of args(%i) not power of 2\n", argc);
        return 0;
    }

    filenames = malloc(sizeof(char*) * argc);
    //stores files into global array
    for(int i = 0; i < argc; i++)
    {
        filenames[i] = argv[i + 1];
    }

    //number of levels in the tree, discluding the master node
    max_levels = (int)(log(argc) / log(2));
    spawnChildren(max_levels);

    free(filenames);
    fclose(output);
    sleep(1);
    return 0;
}

/* Checks if given int is a power of 2 */
int
isPowerOfTwo(unsigned int x)
{
    //bit comparison
    return ((x != 0) && !(x & (x - 1)));
}

/* Recursively spawns 2 children per parent
 * Determines who is whom(master, parents, children) */
void
spawnChildren(int levels)
{
    //no more levels underneath you means you are a leaf process
    if(levels == 0)
    {
        readSort();
        return;
    }
     
    int len = strlen(path);

    if(isMaster)
       pipe(fd[UP]);

    pipe(fd[LEFT]);
    pipe(fd[RIGHT]);

    //spawns 2 children at a single parent, prevents child
    //having another child within the fork'd process
    int pid = fork();
    //parent
    if(pid > 0)
    {
        int pid2 = fork();
        //child
        if(pid2 == 0)
        {
            if(!isMaster)
            {
                if(levels-1)
                   printf("Parent(%d) spawned process(%d).\n", getppid(), getpid());
                else
                   printf("Parent(%d) spawned leaf process(%d).\n", getppid(), getpid()); 
            }
            else
                printf("Master(%d) spawned process(%d).\n", getppid(), getpid());
            isMaster = 0;
            dup2(fd[RIGHT][WRITE], fd[UP][WRITE]);
            close(fd[LEFT][WRITE]);
            close(fd[RIGHT][WRITE]);
            //record path
            path[len] = '1';
            path[len+1] = '\0';
            spawnChildren(levels-1);
            return;
        }
    }
    //child
    else
    {
        if(!isMaster)
        {
            if(levels-1)
                printf("Parent(%d) spawned process(%d).\n", getppid(), getpid());
            else
                printf("Parent(%d) spawned leaf process(%d).\n", getppid(), getpid());
        }
        else
            printf("Master(%d) spawned process(%d).\n", getppid(), getpid());
        isMaster = 0;
        dup2(fd[LEFT][WRITE], fd[UP][WRITE]);
        close(fd[RIGHT][WRITE]);
        close(fd[LEFT][WRITE]);
        //record path
        path[len] = '0';
        path[len+1] = '\0';
        spawnChildren(levels-1);
        return;
    }

    //parent or master
    if(levels > 0)
    {
       if(isMaster)
           close(fd[UP][WRITE]);
       close(fd[UP][READ]);
       close(fd[LEFT][WRITE]);
       close(fd[RIGHT][WRITE]);

       //numbers to be compared
       int num[2];
       //status of reading from buffer
       int status[2];
       //which side is finished
       int finished[2];

       //initial values
       status[LEFT] = read(fd[LEFT][READ], &num[LEFT], sizeof(int));
       if(status[LEFT] != sizeof(int))
           finished[LEFT] = 1;
       else
           finished[LEFT] = 0;

       status[RIGHT] = read(fd[RIGHT][READ], &num[RIGHT], sizeof(int));
       if(status[RIGHT] != sizeof(int))
           finished[RIGHT] = 1;
       else
           finished[RIGHT] = 0;
       
       while(finished[LEFT] == 0 || finished[RIGHT] == 0)
       {
           if(!finished[LEFT] && (num[LEFT] < num[RIGHT] || finished[RIGHT]))
           {
               //if not at top keep sending up
               if(!isMaster)
                  write(fd[UP][WRITE], &num[LEFT], sizeof(int));
               //otherwise you're the master and write to file
               else
                  fprintf(output, "%u\n", num[LEFT]);
               status[LEFT] = read(fd[LEFT][READ], &num[LEFT], sizeof(int));
               //no more bytes to read
               if(status[LEFT] != sizeof(int))
               {
                  close(fd[LEFT][READ]);
                  finished[LEFT] = 1;
               }
           }
           else if(!finished[RIGHT] && (num[RIGHT] <= num[LEFT] || finished[LEFT]))
           {
               //if not at top keep sending up
               if(!isMaster)
                  write(fd[UP][WRITE], &num[RIGHT], sizeof(int));
               //otherwise you're the master and write to file
               else
                  fprintf(output, "%u\n", num[RIGHT]);
               status[RIGHT] = read(fd[RIGHT][READ], &num[RIGHT], sizeof(int));
               //no more bytes to read
               if(status[RIGHT] != sizeof(int))
               {
                  close(fd[RIGHT][READ]);
                  finished[RIGHT] = 1;
               }
           }
       }
       close(fd[UP][WRITE]);
       if(isMaster)
           printf("Wrote sorted file to 'final'.\n");
    }
}

/* Read in the file and sorts it, sends result up to parent */
void
readSort()
{
    close(fd[LEFT][READ]);
    close(fd[RIGHT][READ]);
    close(fd[UP][READ]);

    //converts path to specific binary index(guarantees uniqueness)
    int index = toBinary(path);
    char* filename = filenames[index];

    //array of ints in the file
    intStore = malloc(MAX_INT_IN_FILE * sizeof(int));

    //opens file and reads in input
    FILE* file = fopen(filename, "r");
    int num = 0;
    fscanf(file, "%d", &num);

    printf("%s is reading in on process %i...\n", filename, getpid());

    int i = 0;
    while (!feof(file))
    {
        //stores each int into array(intStore)
        intStore[i] = num;
        i++;
        fscanf(file, "%d", &num);
    }

    sort(intStore, i);
    printf("%s has finished sorting on process %i.  Piping up values.\n", filename, getpid()); 

    //write to pipe, close when done
    for(int k = 0; k < i; k++)
    {
        write(fd[UP][WRITE], &intStore[k], sizeof(int));
    }
    close(fd[UP][WRITE]);

    fclose(file);
}

/* Used for QuickSort */
void
swap(int* a, int* b)
{
    int t = *a;
    *a = *b;
    *b = t;
}

/* No-Fail QuickSort implementation - complements of Wikipedia */
void sort(int *arr, int elements)
{
  #define  MAX_LEVELS 300
  int  piv, beg[MAX_LEVELS], end[MAX_LEVELS], i=0, L, R, swap ;

  beg[0]=0; end[0]=elements;
  while (i>=0) {
    L=beg[i]; R=end[i]-1;
    if (L<R) {
      piv=arr[L];
      while (L<R) {
        while (arr[R]>=piv && L<R) R--; if (L<R) arr[L++]=arr[R];
        while (arr[L]<=piv && L<R) L++; if (L<R) arr[R--]=arr[L]; }
      arr[L]=piv; beg[i+1]=L+1; end[i+1]=end[i]; end[i++]=L;
      if (end[i]-beg[i]>end[i-1]-beg[i-1]) {
        swap=beg[i]; beg[i]=beg[i-1]; beg[i-1]=swap;
        swap=end[i]; end[i]=end[i-1]; end[i-1]=swap; }}
    else {
      i--; }}
}

/* Converts char*(binary format) to int */
int
toBinary(char *s)
{
    return (int) strtol(s, NULL, 2);
}
