// Per-CPU state
struct cpu {
  uchar apicid;                // Local APIC ID
  struct context *scheduler;   // swtch() here to enter scheduler
  struct taskstate ts;         // Used by x86 to find stack for interrupt
  struct segdesc gdt[NSEGS];   // x86 global descriptor table
  volatile uint started;       // Has the CPU started?
  int ncli;                    // Depth of pushcli nesting.
  int intena;                  // Were interrupts enabled before pushcli?
  struct proc *proc;           // The process running on this cpu or null
};

extern struct cpu cpus[NCPU];
extern int ncpu;

//PAGEBREAK: 17
// Saved registers for kernel context switches.
// Don't need to save all the segment registers (%cs, etc),
// because they are constant across kernel contexts.
// Don't need to save %eax, %ecx, %edx, because the
// x86 convention is that the caller has saved them.
// Contexts are stored at the bottom of the stack they
// describe; the stack pointer is the address of the context.
// The layout of the context matches the layout of the stack in swtch.S
// at the "Switch stacks" comment. Switch doesn't save eip explicitly,
// but it is on the stack and allocproc() manipulates it.
struct context {
  uint edi;
  uint esi;
  uint ebx;
  uint ebp;
  uint eip;
};

enum procstate { UNUSED, EMBRYO, SLEEPING, RUNNABLE, RUNNING, ZOMBIE };

// Per-process state
struct proc {
  uint sz;                     // Size of process memory (bytes)
  pde_t* pgdir;                // Page table
  char *kstack;                // Bottom of kernel stack for this process
  enum procstate state;        // Process state
  int pid;                     // Process ID
  struct proc *parent;         // Parent process
  struct trapframe *tf;        // Trap frame for current syscall
  struct context *context;     // swtch() here to run process
  void *chan;                  // If non-zero, sleeping on chan
  int killed;                  // If non-zero, have been killed
  struct file *ofile[NOFILE];  // Open files
  struct inode *cwd;           // Current directory
  char name[16];               // Process name (debugging)
};

typedef struct rbtree {
  struct node *root;  // Root of the tree 
  struct node *nil;
} rbtree;

// State of each node in Red Black 
enum color { RED, BLACK};

typedef struct node {
  struct rbtree *tree;  // Tree that the node belogns to
  struct node *parent;  // Parent node
  struct node *r;       // Right child
  struct node *l;       // Left child
  enum color c;         // Color of the node
  struct proc *p;       // Proc
} node;


void rotateright(rbtree *t, node *x) {
    node *y = x->l;
    x->l = y->r;
    if (y->r != t->nil) {
        y->r->parent = x;
    }
    y->parent = x->parent;
    if (x->parent == t->nil) {
        t->root = y;
    }
    else if (x == x->parent->l) {
        x->parent->l = y;
    }
    else {
        x->parent->r = y;
    }
    y->r = x;
    x->parent = y;
}

void rotateleft(rbtree *t, node *x) {
    node *y = x->r;
    x->r = y->l;
    if (y->l != t->nil) {
        y->l->parent = x;
    }
    y->parent = x->parent;
    if (x->parent == t->nil) {
        t->root = y;
    }
    else if (x == x->parent->l) {
        x->parent->l = y;
    }
    else {
        x->parent->r = y;
    }
    y->l = x;
    x->parent = y;
}

void rb_transplant(rbtree *t, node *u, node *v){

  if (u->parent == t->nil){
    t->root = v;
  }
  else if (u == u->parent->l){
    u->parent->l = v;
  } 
  else {
    u->parent->r = v;
  }
  v->parent = u->parent;
}

// Process memory is laid out contiguously, low addresses first:
//   text
//   original data and bss
//   fixed-size stack
//   expandable heap
