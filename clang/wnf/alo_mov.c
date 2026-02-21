// @{s} !% x = v; t
// ------------------ ALO-MOV
// Allocate MOV cell, store ALO-expanded val in it.
// Extend bind list with GET pointing at the MOV cell.
// Return ALO for body with extended bind list.
fn Term wnf_alo_mov(u32 ls_loc, u32 len, u32 book_loc) {
  u64 mov_cell = heap_alloc(1);
  Term alo_v;
  if (len == 0) {
    alo_v = term_new(0, ALO, 0, book_loc + 0);
  } else {
    u64 alo0 = heap_alloc(1);
    heap_set(alo0, ((u64)ls_loc << 32) | (book_loc + 0));
    alo_v = term_new(0, ALO, len, (u32)alo0);
  }
  heap_set(mov_cell, alo_v);
  u64 bind_ent = heap_alloc(2);
  heap_set(bind_ent + 0, term_sub_set(term_new_get((u32)mov_cell), 1));
  heap_set(bind_ent + 1, term_new_num(ls_loc));
  u64 alo2 = heap_alloc(1);
  heap_set(alo2, ((u64)(u32)bind_ent << 32) | (book_loc + 1));
  return term_new(0, ALO, len + 1, (u32)alo2);
}
