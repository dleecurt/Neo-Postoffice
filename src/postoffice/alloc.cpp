#include <stdlib.h>
//#include "defs.h"
#include "xallocation.h"
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>

#ifdef IRIX
#include <libexc.h>
#endif

typedef struct _tagAllocs {
	void* Block;
	char *Type, *Origin, *Origin2;
	int Start;
	int Length;
	struct _tagAllocs* Next, * Prev;
} Allocs;

static Allocs* Head = NULL, * Tail = NULL;

static pthread_mutexattr_t *mem_attr = NULL;
static pthread_mutex_t     *mem_mutex = NULL;

void dumpalloc (int i)
{
#ifdef DEBUG_ALLOC
	long total=0,subtotal=0,count=0;
	FILE *F = fopen ("shttpd-memcore","w");
	if (F)
	{
		Allocs* aux = Head;
		Allocs* aux2;
		pthread_mutex_lock (mem_mutex);

		while(aux) {
			//dprintf (24,"dumping %x (next=%x)\n", aux, aux->Next);
			total += aux->Length;
			{
				aux2 = Head;
				count=0; subtotal=0;
				fprintf (F, "%s:%06x:%d:%s\n", aux->Origin, aux->Block, aux->Length, aux->Type);
				fflush (F);
			}
			aux = aux->Next;
		}
		pthread_mutex_unlock (mem_mutex);
		fprintf (F, "TOTAL:%d\n", total);
		fclose (F);
	}
    signal (SIGHUP, dumpalloc);
#endif
}

static int AddBlock(void* Block, int Length, char *Trace)
{
	Allocs* aux;

	aux = (Allocs*)malloc(sizeof(Allocs)); 
		// we can't call new, because we're gonna redefine
		// __builtin_new and __builtin_delete.
		// it will cause only an infinite allocation loop!
		// bad bad thing eh? =) --NeXTPhase
	if(aux==NULL) return -1; // yikes, no memory??
	aux->Next = aux->Prev = NULL;

	if(Head==NULL) Head = aux;
	if(Tail!=NULL)
	{
		aux->Prev = Tail;
		Tail->Next = aux;
	}
	Tail=aux;
	aux->Block = Block;
	aux->Start = (int)Block;
	aux->Length = Length;
	aux->Type = (char *) "<unknown>";
	aux->Origin = Trace;
	//dprintf (24,"aux=%x head=%x tail=%x aux->next=%x aux->prev=%x\n", aux, Head, Tail, aux->Next, aux->Prev);
	return 0;
}

void settype(void* Block, char *Type, char *Origin)
{
}

static int RemoveBlock(void* Block)
{
	Allocs* aux = Head;
	int BlockPos = (int)Block;

	while(aux) {
		if(BlockPos >= aux->Start && BlockPos <= aux->Start+aux->Length) {
			if (BlockPos != aux->Start) printf ("PANIC: erronous position in delete()\n");
			pthread_mutex_lock (mem_mutex);
			#ifdef IRIX
			aux->Origin[0] = 0;
			free(aux->Origin);
			#endif
			free(aux->Block);
			break;
		}
		aux = aux->Next;
	}
	if(aux==NULL) return -1; // not my block

	if(aux==Head) {
		Head = aux->Next;
	}
	else if(aux==Tail) {
		Tail = aux->Prev;
	}
	else {
		if(aux->Next) aux->Next->Prev = aux->Prev;
		if(aux->Prev) aux->Prev->Next = aux->Next;
	}
	free(aux);
	pthread_mutex_unlock (mem_mutex);
	return 0;
}


extern "C" void* ealloc(int Size, char *Trace)
{
	void* Block = malloc(Size);
	if (!mem_attr)
	{
		mem_attr = (pthread_mutexattr_t *) malloc (sizeof (pthread_mutexattr_t));
		mem_mutex = (pthread_mutex_t *) malloc (sizeof (pthread_mutex_t));
		pthread_mutexattr_init (mem_attr);
		pthread_mutex_init (mem_mutex, mem_attr);
	}

	pthread_mutex_lock (mem_mutex);

	if(Block&&AddBlock(Block, Size, Trace)<0) {
		free(Block);
		pthread_mutex_unlock (mem_mutex);
		return NULL; // No memory =(
	}
	pthread_mutex_unlock (mem_mutex);
	return Block;
}

extern "C" int efree(void* Block)
{
	if(Block&&RemoveBlock(Block) < 0) {
		free(Block);	// It was not mine anyway I deallocate it
		return 0;
	}
	if(Block) return 0;
	return -1;
}

#ifdef DEBUG_ALLOC
#ifdef IRIX
void *operator new (size_t size)
{
	char **trace = (char **) malloc (4 * sizeof (char *));
	uint64_t *addr = (uint64_t *) malloc (4 * sizeof (uint64_t));
	
	char *ctrace = (char *) malloc ((size_t) 104);
	
	trace[0] = (char *) malloc ((size_t) 32);
	trace[1] = (char *) malloc ((size_t) 32);
	trace[2] = (char *) malloc ((size_t) 32);
	trace[3] = (char *) malloc ((size_t) 32);
	trace[1][0] = 0;
	trace[2][0] = 0;
	trace[3][0] = 0;
	
	trace_back_stack(4, addr, trace, 4, 31);
	
	strcpy (ctrace, trace[3]);
	strcat (ctrace, "->");
	strcat (ctrace, trace[2]);
	strcat (ctrace, "->");
	strcat (ctrace, trace[1]);
	
	void *x = ealloc(size, ctrace);

	free (trace[0]);
	free (trace[1]);
	free (trace[2]);
	free (trace[3]);
	free (trace);
	free (addr);
	return x;
}

void operator delete (void *block)
{
	efree (block);
}
#else

#include <setjmp.h>

const char hex[18] = "0123456789abcdef";

extern "C" int __builtin_delete(void* block) { efree(block); return 0;}
extern "C" void* __builtin_new(int size)
{
	unsigned long addr1, addr2, addr3;

	addr1 = (unsigned long) *(__builtin_frame_address (1));
	addr2 = (unsigned long) *(__builtin_frame_address (2));
	//addr3 = (unsigned long) __builtin_frame_address (3);
	
	int pos = 0;

	char *ctrace = (char *) malloc ((size_t) 30);

/*	ctrace[pos++] = hex[((addr3 & 0xf0000000) >> 28)];
	ctrace[pos++] = hex[((addr3 & 0x0f000000) >> 24)];
	ctrace[pos++] = hex[((addr3 & 0x00f00000) >> 20)];
	ctrace[pos++] = hex[((addr3 & 0x000f0000) >> 16)];
	ctrace[pos++] = hex[((addr3 & 0x0000f000) >> 12)];
	ctrace[pos++] = hex[((addr3 & 0x00000f00) >>  8)];
	ctrace[pos++] = hex[((addr3 & 0x000000f0) >>  4)];
	ctrace[pos++] = hex[((addr3 & 0x0000000f))      ];
	ctrace[pos++] = '-';
	ctrace[pos++] = '>'; */
	ctrace[pos++] = hex[((addr2 & 0xf0000000) >> 28)];
	ctrace[pos++] = hex[((addr2 & 0x0f000000) >> 24)];
	ctrace[pos++] = hex[((addr2 & 0x00f00000) >> 20)];
	ctrace[pos++] = hex[((addr2 & 0x000f0000) >> 16)];
	ctrace[pos++] = hex[((addr2 & 0x0000f000) >> 12)];
	ctrace[pos++] = hex[((addr2 & 0x00000f00) >>  8)];
	ctrace[pos++] = hex[((addr2 & 0x000000f0) >>  4)];
	ctrace[pos++] = hex[((addr2 & 0x0000000f))      ];
	ctrace[pos++] = '-';
	ctrace[pos++] = '>';
	ctrace[pos++] = hex[((addr1 & 0xf0000000) >> 28)];
	ctrace[pos++] = hex[((addr1 & 0x0f000000) >> 24)];
	ctrace[pos++] = hex[((addr1 & 0x00f00000) >> 20)];
	ctrace[pos++] = hex[((addr1 & 0x000f0000) >> 16)];
	ctrace[pos++] = hex[((addr1 & 0x0000f000) >> 12)];
	ctrace[pos++] = hex[((addr1 & 0x00000f00) >>  8)];
	ctrace[pos++] = hex[((addr1 & 0x000000f0) >>  4)];
	ctrace[pos++] = hex[((addr1 & 0x0000000f))      ];
	ctrace[pos++] = 0;
	
	void *x = ealloc(size, ctrace);
	return x;
}

extern "C" void *__builtin_vec_new (size_t sz, unsigned long bl)
{
	return __builtin_new ((int) sz);
}

extern "C" void *__builtin_vec_delete (void *ptr)
{
	return ((void *) __builtin_delete (ptr));
}

#endif

#endif
