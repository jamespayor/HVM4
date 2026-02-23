// GET(mov_loc) where cell = SUP(lab, sup_loc)
// --------------------------------------------- MOV-SUP
// Commute MOV under SUP: split the MOV cell into two new cells
// (one per SUP branch) and return the new SUP directly.
// Other GETs on this cell will see the new SUP and split again,
// creating GET chains that collapse via GET-GET fusion.
fn Term wnf_mov_sup(u32 mov_loc, Term sup) {
  ITRS_INC("MOV-SUP");
  u32  sup_lab = term_ext(sup);
  u32  sup_loc = term_val(sup);
  Term val_a   = heap_read(sup_loc + 0);
  Term val_b   = heap_read(sup_loc + 1);
  u64  mov_a   = heap_alloc(1);
  u64  mov_b   = heap_alloc(1);
  heap_set(mov_a, val_a);
  heap_set(mov_b, val_b);
  u64 new_sup  = heap_alloc(2);
  heap_set(new_sup + 0, term_new_get((u32)mov_a));
  heap_set(new_sup + 1, term_new_get((u32)mov_b));
  Term result = term_new(0, SUP, sup_lab, (u32)new_sup);
  heap_set(mov_loc, result);
  return result;
}
