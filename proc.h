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
  int vruntime;                // Virtual runtime of the process
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


void rbinsertfixup(rbtree *t, node *n) {

  while (n->parent->c == RED) {
    if (n->parent == n->parent->parent->l) {
      node *y = n->parent->parent->r;
      if (y->c == RED) {
        n->parent->c = BLACK;
        y->c = BLACK;
        n->parent->parent->c = RED;
        n = n->parent->parent;
      } 
      else {
        if (n == n->parent->r) {
          n = n->parent;
          rotateleft(t, n);
        }
        n->parent->c = BLACK;
        n->parent->parent->c = RED;
        rotateright(t, n->parent->parent);
      }
    } 
    else {
      node *y = n->parent->parent->l;
      if (y->c == RED) {
        n->parent->c = BLACK;
        y->c = BLACK;
        n->parent->parent->c = RED;
        n = n->parent->parent;
      } 
      else {
        if (n == n->parent->l) {
          n = n->parent;
          rotateright(t, n);
        }
        n->parent->c = BLACK;
        n->parent->parent->c = RED;
        rotateleft(t, n->parent->parent);
      }
    }
  }
  t->root->c = BLACK;
}

void rbinsert(rbtree *t, node *n) {
  
  node *x = t->root;
  node *y = t->nil;
  while (x != t->nil) {
    y = x;
    if (n->p->vruntime < x->p->vruntime)
      x = x->l;
    else
      x = x->r;
  }
  n->parent = y;
  if (y == t->nil)
    t->root = n;
  else if (n->p->vruntime < y->p->vruntime)
    y->l = n;
  else
    y->r = n;
  n->r = t->nil;
  n->l = t->nil;
  n->c = RED;
  rbinsertfixup(t, n);
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

node *minimum(rbtree *t, node *u){
    while (u->l != t->nil)
    {
      u = u->l;
    }
    return u;
}

void rb_delete_fixup(rbtree *t, node *x){
    while (x != t->root && x->c == BLACK)
    {
      if (x == x->parent->l){
        node *w = x->parent->r;
        // type 1
        if (w->c == RED){
          w->c == BLACK;
          x->parent->c = RED;
          rotateleft(t, x->parent);
          w = x->parent->r;
        }
        // type 2
        if (w->l->c == BLACK && w->r->c == BLACK){
          w->c = RED;
          x = x->parent;
        } 
        else {
          // type 3
          if (w->r->c == BLACK){
            w->l->c = BLACK;
            w->c = RED;
            rotateright(t, w);
            w = x->parent->r;
          }
          // type 4
          w->c = x->parent->c;
          x->parent->c = BLACK;
          w->r->c = BLACK;
          rotateleft(t, x->parent);
          x = t->root;
        }
      } else {
        node *w = x->parent->l;
        // type 1
        if (w->c == RED){
          w->c = BLACK;
          x->parent->c = RED;
          rotateright(t, x->parent);
          w = x->parent->l;
        }
        // type 2
        if (w->r->c == BLACK && w->l->c == BLACK){
          w->c = RED;
          x = x->parent;
        }
        else {
          // type 3
          if (w->l->c == BLACK){
            w->r->c = BLACK;
            w->c = RED;
            rotateleft(t, w);
            w = x->parent->l;
          }
          // type 4
          w->c = x->parent->c;
          x->parent->c = BLACK;
          w->l->c = BLACK;
          rotateright(t, x->parent);
          x = t->root;
        }
      }
    }
    x->c = BLACK;
}



void rb_delete(rbtree *t, node *z){

    node *y = z;
    enum color y_main_color = y->c;
    if (z->l == t->nil){
      node *x = z->r;
      rb_transplant(t, z, z->r);
    }
    else if (z->r == t->nil){
      node *x = z->l;
      rb_transplant(t, z, z->l);
    }
    else{
      y = minimum(t, z->r);
      y_main_color = y->c;
      node *x = y->r;
      if (y->parent == z){
        x->parent = y;
      } else {
        rb_transplant(t, y, y->r);
        y->r = z->r;
        y->r->parent = y;
      }
      rb_transplant(t, z, y);
      y->l = z->l;
      y->l->parent = y;
      y->c = z->c;
    if (y_main_color == BLACK){
      rb_delete_fixup(t, x);
      }
    }
}

// Process memory is laid out contiguously, low addresses first:
//   text
//   original data and bss
//   fixed-size stack
//   expandable heap
