/* malloc.c */
#include <stddef.h>

/* Heap boundaries - defined in linker script */
extern unsigned char __heap_start[];
extern unsigned char __heap_end[];


/* Allocator state */
static char *brkval = NULL;
static size_t malloc_margin = 1024; /* Conservative stack margin */

struct freelist_t {
    size_t sz;
    struct freelist_t *nx;
};

static struct freelist_t *__freelist = NULL;

/* Alignment helper */
#define ALIGN_UP(x, align) (((x) + ((align)-1)) & ~((align)-1))
#define ALIGN_DOWN(x, align) ((x) & ~((align)-1))

/* Minimum allocation size (8-byte aligned) */
#define MIN_ALLOC (ALIGN_UP(sizeof(struct freelist_t) - sizeof(size_t), 8))

/* Stack pointer access */
__attribute__((optimize("Os"), naked))
uintptr_t get_stack_pointer(void) {
     // __asm__ volatile("mrs %0, msp" : "=r" (sp));  // Main Stack Pointer
    __asm__ volatile(
        "mov r0, sp\n"
        "bx lr\n"
    );
}



void *malloc(size_t len)
{
    struct freelist_t *fp1, *fp2, *sfp1, *sfp2;
    char *cp;
    size_t s, avail;

    /* Handle zero-size allocation */
    if(len == 0)
        return NULL;

    /* Ensure minimum size and alignment */
    len = ALIGN_UP(len < MIN_ALLOC ? MIN_ALLOC : len, 8);

    /* First-fit search */
    /* Initialize traversal */
    s = 0;
    fp1 = __freelist;   /* current node */
    fp2 = NULL;         /* previous node */
    
    while(fp1 != NULL) {
    
        /* Case 1: too small → skip */
        if(fp1->sz < len) {
            fp2 = fp1;
            fp1 = fp1->nx;
            continue;
        }
    
        /* Case 2: exact match → remove block and return it */
        if(fp1->sz == len) {
            if (fp2 != NULL)
                fp2->nx = fp1->nx;   /* unlink from middle */
            else
                __freelist = fp1->nx; /* unlink from head */
    
            return &(fp1->nx);
        }
    
        /* Case 3: larger block → candidate for best-fit split */
        if(s == 0 || fp1->sz < s) {
            s = fp1->sz;
            sfp1 = fp1;   /* best-fit block so far */
            sfp2 = fp2;   /* its previous node */
        }
    
        /* Advance traversal */
        fp2 = fp1;
        fp1 = fp1->nx;
    }


    /* Split existing chunk if suitable */
    if(s) {
        if(s - len < sizeof(struct freelist_t)) {
            /* Too small to split */
            if(sfp2)
                sfp2->nx = sfp1->nx;
            else
                __freelist = sfp1->nx;
            return &(sfp1->nx);
        }

        /* Split the chunk */
        cp = (char *)sfp1;
        s -= len;
        cp += s;
        sfp2 = (struct freelist_t *)cp;
        sfp2->sz = len;
        sfp1->sz = s - sizeof(size_t);
        return &(sfp2->nx);
    }

    /* Initialize heap if first allocation */
    if(brkval == NULL)
        brkval = (char *)__heap_start;

    /* Calculate available memory */
    uintptr_t sp = (uintptr_t)get_stack_pointer();
    char *stack_limit = (char *)(sp - malloc_margin);
    char *heap_limit = ((uintptr_t)stack_limit < (uintptr_t)__heap_end) ? stack_limit : (char *)__heap_end;

    avail = heap_limit - brkval;
    
    /* Check if we have enough space */
    if(avail >= len + sizeof(size_t)) {
        fp1 = (struct freelist_t *)brkval;
        brkval += len + sizeof(size_t);
        fp1->sz = len;
        return &(fp1->nx);
    }

    return NULL; /* Out of memory */
}

void free(void *p)
{
    struct freelist_t *fp1, *fp2, *fpnew;
    char *cp1, *cpnew;

    if(p == NULL)
        return;

    cpnew = (char *)p - sizeof(size_t);
    fpnew = (struct freelist_t *)cpnew;
    fpnew->nx = NULL;

    /* Try to coalesce with top of heap */
    if((char *)p + fpnew->sz == brkval) {
        brkval = cpnew;
        if (__freelist == NULL)
            return;
            
    /* Find new top-most chunk */
    fp1 = __freelist;
    fp2 = NULL;
    
    /* Walk to the last node in the list */
    while(fp1->nx != NULL) {
        fp2 = fp1;        /* previous node */
        fp1 = fp1->nx;    /* advance to next */
    }

        
        cp1 = (char *)&(fp1->nx);
        if(cp1 + fp1->sz == brkval) {
            if(fp2 == NULL)
                __freelist = NULL;
            else
                fp2->nx = NULL;
            brkval = cp1 - sizeof(size_t);
        }
        return;
    }

    /* Insert into free list */
    if(__freelist == NULL) {
        __freelist = fpnew;
        return;
    }

    /* Find insertion point in sorted free list */
    fp1 = __freelist;   /* current node */
    fp2 = NULL;         /* previous node */
    
    while(fp1 != NULL) {
        /* If fp1 is still before fpnew in memory, keep walking */
        if(fp1 < fpnew) {
            fp2 = fp1;
            fp1 = fp1->nx;
            continue;
        }
    
        /* Insert fpnew before fp1 */
        fpnew->nx = fp1;
    
        /* Try to merge fpnew with the next chunk (fp1) */
        if((char *)&fpnew->nx + fpnew->sz == (char *)fp1) {
            fpnew->sz += fp1->sz + sizeof(size_t);
            fpnew->nx = fp1->nx;
        }
    
        /* If inserting at the head of the list */
        if(fp2 == NULL) {
            __freelist = fpnew;
            return;
        }
    
        /* Otherwise, stop — fp2 will link to fpnew outside the loop */
        break;
    }


    /* Insert and try to merge with previous chunk */
    fp2->nx = fpnew;
    cp1 = (char *)&(fp2->nx);
    if(cp1 + fp2->sz == (char *)fpnew) {
        fp2->sz += fpnew->sz + sizeof(size_t);
        fp2->nx = fpnew->nx;
    }
}

/* Additional helper functions */
void *calloc(size_t nmemb, size_t size)
{
    size_t total = nmemb * size;
    void *p = malloc(total);
    if(p) {
        /* Simple memset implementation */
        char *cp = p;
        while(total--)
            *cp++ = 0;
    }
    return p;
}

// /* Additional helper functions */
// void * calloc(size_t nele, size_t size) {
//     void *p;
//     if ((p = malloc(nele * size)) == 0)
//         return 0;
//     memset(p, 0, nele * size);
//     return p;
// }

void *realloc(void *ptr, size_t size)
{
    /* C standard requirement */
    if(ptr == NULL)
        return malloc(size);
    
    if(size == 0) {
        free(ptr);
        return NULL;
    }
    
    /* Get original block size */
    struct freelist_t *fp = (struct freelist_t *)((char *)ptr - sizeof(size_t));
    size_t old_size = fp->sz;
    
    /* Allocate new block */
    void *new_ptr = malloc(size);
    if(new_ptr == NULL)
        return NULL;
    
    /* Copy data */
    size_t copy_size = old_size < size ? old_size : size;
    char *src = ptr;
    char *dst = new_ptr;
    while(copy_size--)
        *dst++ = *src++;
    
    free(ptr);
    return new_ptr;
}


void malloc_init(void)
{
    /* Initialize heap pointer */
    brkval = (char *)__heap_start;
    __freelist = NULL;
    
    /* Optionally create one big free block covering entire heap */
    size_t heap_size = __heap_end - __heap_start;
    if(heap_size > sizeof(struct freelist_t)) {
        struct freelist_t *initial_block = (struct freelist_t *)__heap_start;
        initial_block->sz = heap_size - sizeof(size_t);
        initial_block->nx = NULL;
        __freelist = initial_block;
    }
    
    /* Set conservative stack margin (adjust based on your needs) */
    malloc_margin = 1024; /* 1KB safety margin */
}
