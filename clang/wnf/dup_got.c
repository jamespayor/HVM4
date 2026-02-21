// ! X &L = GOT(mov_loc)
// ---------------------- DUP-GOT
// Read MOV cell inner value.
// If SUB=0 (first encounter):
//   Clone inner, cache one side in MOV cell (SUB=1),
//   write GOT(mov_loc)|SUB to DUP cell for other DP.
//   Return this side.
// If SUB=1 (cached):
//   Take cached value, write ERA|SUB to cell.
//   Return via heap_subst_cop.
fn Term wnf_dup_got(u32 lab, u32 loc, u8 side, u32 mov_loc, Term inner) {
  ITRS_INC("DUP-GOT");
  Copy c          = term_clone(lab, inner);
  Term this_side  = side == 0 ? c.k0 : c.k1;
  Term other_side = side == 0 ? c.k1 : c.k0;
  heap_set(mov_loc, term_sub_set(other_side, 1));
  heap_set_rel(loc, term_sub_set(term_new_got(mov_loc), 1));
  return this_side;
}
