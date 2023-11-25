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

// State of each process in red black tree
enum color { RED, BLACK};

// Per-process state
struct proc {
  int vruntime;                // Virtual runtime of the process
  struct proc *rbparent;       // Parent node in red black tree
  struct proc *r;              // Right child
  struct proc *l;              // Left child
  enum color c;                // Color of the node
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
  struct proc *root;  // Root of the tree
  struct proc *nil;
} rbtree;



void rotateright(rbtree *t, struct proc *p) {
  struct proc *y = p->l;
  p->l = y->r;
  if (y->r != t->nil) {
      y->r->rbparent = p;
  }
  y->rbparent = p->rbparent;
  if (p->rbparent == t->nil) {
      t->root = y;
  }
  else if (p == p->rbparent->l) {
      p->rbparent->l = y;
  }
  else {
      p->rbparent->r = y;
  }
  y->r = p;
  p->rbparent = y;
}

void rotateleft(rbtree *t, struct proc *p) {
  struct proc *y = p->r;
  p->r = y->l;
  if (y->l != t->nil) {
      y->l->rbparent = p;
  }
  y->rbparent = p->rbparent;
  if (p->rbparent == t->nil) {
      t->root = y;
  }
  else if (p == p->rbparent->l) {
      p->rbparent->l = y;
  }
  else {
      p->rbparent->r = y;
  }
  y->l = p;
  p->rbparent = y;
}


void rbinsertfixup(rbtree *t, struct proc *p) {

  while (p->rbparent->c == RED) {
    if (p->rbparent == p->rbparent->rbparent->l) {
      struct proc *y = p->rbparent->rbparent->r;
      if (y->c == RED) {
        p->rbparent->c = BLACK;
        y->c = BLACK;
        p->rbparent->rbparent->c = RED;
        p = p->rbparent->rbparent;
      } 
      else {
        if (p == p->rbparent->r) {
          p = p->rbparent;
          rotateleft(t, p);
        }
        p->rbparent->c = BLACK;
        p->rbparent->rbparent->c = RED;
        rotateright(t, p->rbparent->rbparent);
      }
    } 
    else {
      struct proc *y = p->rbparent->rbparent->l;
      if (y->c == RED) {
        p->rbparent->c = BLACK;
        y->c = BLACK;
        p->rbparent->rbparent->c = RED;
        p = p->rbparent->rbparent;
      } 
      else {
        if (p == p->rbparent->l) {
          p = p->rbparent;
          rotateright(t, p);
        }
        p->rbparent->c = BLACK;
        p->rbparent->rbparent->c = RED;
        rotateleft(t, p->rbparent->rbparent);
      }
    }
  }
  t->root->c = BLACK;
}

void rbinsert(rbtree *t, struct proc *p) {
  
  struct proc *x = t->root;
  struct proc *y = t->nil;
  while (x != t->nil) {
    y = x;
    if (p->vruntime < x->vruntime)
      x = x->l;
    else
      x = x->r;
  }
  p->rbparent = y;
  if (y == t->nil)
    t->root = p;
  else if (p->vruntime < y->vruntime)
    y->l = p;
  else
    y->r = p;
  p->r = t->nil;
  p->l = t->nil;
  p->c = RED;
  rbinsertfixup(t, p);
}

void rb_transplant(rbtree *t, struct proc *u, struct proc *v){

    if (u->rbparent == t->nil){
      t->root = v;
    }
    else if (u == u->rbparent->l){
      u->rbparent->l = v;
    } 
    else {
      u->rbparent->r = v;
    }
    v->rbparent = u->rbparent;
}

struct proc *minimum(rbtree *t, struct proc *u){
    while (u->l != t->nil)
    {
      u = u->l;
    }
    return u;
}

void rb_delete_fixup(rbtree *t, struct proc *x){
    while (x != t->root && x->c == BLACK)
    {
      if (x == x->rbparent->l){
        struct proc *w = x->rbparent->r;
        // type 1
        if (w->c == RED){
          w->c = BLACK;
          x->rbparent->c = RED;
          rotateleft(t, x->rbparent);
          w = x->rbparent->r;
        }
        // type 2
        if (w->l->c == BLACK && w->r->c == BLACK){
          w->c = RED;
          x = x->rbparent;
        } 
        else {
          // type 3
          if (w->r->c == BLACK){
            w->l->c = BLACK;
            w->c = RED;
            rotateright(t, w);
            w = x->rbparent->r;
          }
          // type 4
          w->c = x->rbparent->c;
          x->rbparent->c = BLACK;
          w->r->c = BLACK;
          rotateleft(t, x->rbparent);
          x = t->root;
        }
      } else {
        struct proc *w = x->rbparent->l;
        // type 1
        if (w->c == RED){
          w->c = BLACK;
          x->rbparent->c = RED;
          rotateright(t, x->rbparent);
          w = x->rbparent->l;
        }
        // type 2
        if (w->r->c == BLACK && w->l->c == BLACK){
          w->c = RED;
          x = x->rbparent;
        }
        else {
          // type 3
          if (w->l->c == BLACK){
            w->r->c = BLACK;
            w->c = RED;
            rotateleft(t, w);
            w = x->rbparent->l;
          }
          // type 4
          w->c = x->rbparent->c;
          x->rbparent->c = BLACK;
          w->l->c = BLACK;
          rotateright(t, x->rbparent);
          x = t->root;
        }
      }
    }
    x->c = BLACK;
}



void rb_delete(rbtree *t, struct proc *z){

    struct proc *y = z;
    enum color y_main_color = y->c;
    struct proc *x;
    if (z->l == t->nil){
      x = z->r;
      rb_transplant(t, z, z->r);
    }
    else if (z->r == t->nil){
      x = z->l;
      rb_transplant(t, z, z->l);
    }
    else{
      y = minimum(t, z->r);
      y_main_color = y->c;
      x = y->r;
      if (y->rbparent == z){
        x->rbparent = y;
      } else {
        rb_transplant(t, y, y->r);
        y->r = z->r;
        y->r->rbparent = y;
      }
      rb_transplant(t, z, y);
      y->l = z->l;
      y->l->rbparent = y;
      y->c = z->c;
    if (y_main_color == BLACK){
      rb_delete_fixup(t, x);
      }
    }
}

struct proc *minimum_runnable(rbtree *tree, struct proc *p) {

    struct proc *t;
    if (p == tree->nil) return tree->nil;
    t = minimum_runnable(tree, p->l);
    if (t != tree->nil) return t;
    else if (p->state == RUNNABLE) return p;
    else
      t = minimum_runnable(tree, p->r);
    return t;
}

// Process memory is laid out contiguously, low addresses first:
//   text
//   original data and bss
//   fixed-size stack
//   expandable heap