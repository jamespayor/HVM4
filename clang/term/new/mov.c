fn Term term_new_mov(Term val, Term bod) {
  u64 loc = heap_alloc(2);
  heap_set(loc + 0, val);
  heap_set(loc + 1, bod);
  return term_new(0, MOV, 0, (u32)loc);
}
