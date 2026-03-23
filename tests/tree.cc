
// The program tests part of the C++ functionality provided by `gc_cpp.h`
// file (and `gctba` library) by creating balanced trees of various depth
// and arity.

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#undef GC_BUILD

#define GC_DONT_INCL_WINDOWS_H
#define GC_NAMESPACE
#include "gc_cpp.h"

#include <stdio.h>
#include <stdlib.h>

#define NO_GC GC_NS_QUALIFY(NoGC)
#define PTRFREE_GC GC_NS_QUALIFY(PointerFreeGC)
#define USE_GC GC_NS_QUALIFY(UseGC)

#ifdef GC_ATOMIC_UNCOLLECTABLE
#  define PTRFREE_NOGC GC_NS_QUALIFY(PointerFreeNoGC)
#endif

class Tree;

class PTree : public GC_NS_QUALIFY(gc)
{
public:
  Tree *ptr;
};

class Tree : public GC_NS_QUALIFY(gc)
{
public:
  GC_ATTR_EXPLICIT
  Tree(int a, int d, bool uncollectable);
#ifdef CPPCHECK
  Tree(const Tree &other)
      : arity(other.arity), depth(other.depth), m_nodes(other.m_nodes)
  {
  }
  Tree &
  operator=(const Tree &other)
  {
    arity = other.arity;
    depth = other.depth;
    m_nodes = other.m_nodes;
    return *this;
  }
#endif
  ~Tree();
  void verify();

private:
  void verify(int a, int d);
  void verify_node(int a, int d);
  int arity, depth;
  PTree *m_nodes;
};

Tree::Tree(int a, int d, bool uncollectable) : arity(a), depth(d)
{
  PTree *nodes = 0;
  if (depth > 0) {
#ifdef GC_OPERATOR_NEW_ARRAY
    nodes =
#  ifndef CPPCHECK
        uncollectable ? new (NO_GC) PTree[arity] :
#  endif
                      new (USE_GC) PTree[arity];
#else
    nodes = reinterpret_cast<PTree *>(
        uncollectable
            ? GC_MALLOC_UNCOLLECTABLE(sizeof(PTree) * (unsigned)arity)
            : GC_MALLOC(sizeof(PTree) * (unsigned)arity));
    GC_OP_NEW_OOM_CHECK(nodes);
#endif
    for (int i = 0; i < arity; i++) {
      Tree const *pnode
          = depth > 1
                ? (uncollectable ? new (NO_GC) Tree(arity, depth - 1, true)
                                 : new (USE_GC) Tree(arity, depth - 1, false))
            :
#ifdef GC_ATOMIC_UNCOLLECTABLE
            uncollectable ? new (PTRFREE_NOGC) Tree(arity, 0, true)
                          :
#endif
                          new (PTRFREE_GC) Tree(arity, 0, uncollectable);
      GC_PTR_STORE_AND_DIRTY(&nodes[i].ptr, pnode);
    }
  }
  this->m_nodes = nodes;
  GC_END_STUBBORN_CHANGE(this);
  GC_reachable_here(nodes);
}

Tree::~Tree()
{
  if (depth > 0) {
    for (int i = 0; i < arity; i++) {
      delete m_nodes[i].ptr;
    }
#ifdef GC_OPERATOR_NEW_ARRAY
    delete[] m_nodes;
#else
    GC_FREE(m_nodes);
#endif
  }
}

void
Tree::verify()
{
  verify(arity, depth);
}

void
Tree::verify_node(int a, int d)
{
  if (a != arity || d != depth) {
    fprintf(stderr, "Wrong stored arity or depth! arity=%d, depth=%d\n", arity,
            depth);
    exit(1);
  }
  if (0 == depth) {
    if (m_nodes != 0) {
      fprintf(stderr, "Found nonempty node!\n");
      exit(1);
    }
  } else if (0 == m_nodes) {
    fprintf(stderr, "Found empty node! depth=%d\n", depth);
    exit(1);
  }
}

void
Tree::verify(int a, int d)
{
  verify_node(a, d);
  if (0 == depth)
    return;
  for (int i = 0; i < arity; i++) {
    m_nodes[i].ptr->verify(a, d - 1);
  }
}

#ifndef MAX_ARITY
#  define MAX_ARITY 5
#endif

#ifndef MAX_DEPTH
#  define MAX_DEPTH 9
#endif

#ifndef DEL_EVERY_N_TREE
#  define DEL_EVERY_N_TREE 7
#endif

static void
create_trees(bool is_find_leak, bool uncollectable)
{
  int trees_cnt = 0;
  for (int arity = 2; arity <= MAX_ARITY; arity++) {
    for (int depth = 1; depth <= MAX_DEPTH; depth++) {
      Tree *tree = new (USE_GC) Tree(arity, depth, uncollectable);
      tree->verify();
      if (is_find_leak || uncollectable || ++trees_cnt % DEL_EVERY_N_TREE == 0)
        delete tree;
    }
  }
}

int
main(void)
{
#ifdef TEST_MANUAL_VDB
  GC_set_manual_vdb_allowed(1);
#endif
#ifdef TEST_MPROTECT_VDB_DISALLOWED
  GC_set_mprotect_vdb_allowed(0);
#endif
#ifndef CPPCHECK
  GC_INIT();
#endif
  bool is_find_leak = static_cast<bool>(GC_get_find_leak());
#ifndef NO_INCREMENTAL
  GC_enable_incremental();
#endif
  Tree *keep_tree = new (USE_GC) Tree(MAX_ARITY, MAX_DEPTH, false);
  keep_tree->verify();
  create_trees(is_find_leak, false);
  create_trees(is_find_leak, true);
  keep_tree->verify(); /*< recheck */
  if (is_find_leak)
    delete keep_tree;
  printf("SUCCEEDED\n");
  return 0;
}
