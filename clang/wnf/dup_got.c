// ! X &L = GOT(mov_loc)
// ---------------------- DUP-GOT
// The DUP cell contained GOT(mov_loc), so the MOV cell holds a WNF constructor.
// Redirect this DP to point at the MOV cell directly: DP0(mov_loc).
// DUP-constructor (DUP-LAM etc.) will fire when DP0(mov_loc) is entered,
// and will write the other-side result back to mov_loc (via heap_subst_cop).
// The other DP gets GOT(mov_loc)|SUB written to the DUP cell; when it is
// demanded, it reads mov_loc to find the other-side result cached there.
fn Term wnf_dup_got(u32 lab, u32 loc, u8 side, u32 mov_loc, Term inner) {
  ITRS_INC("DUP-GOT");
  printf("[DUP-GOT] lab=%u loc=%u side=%u mov_loc=%u\n", lab, loc, side, mov_loc);
  (void)inner; // MOV cell still holds the value; DP0(mov_loc) will heap_take it.
  heap_set_rel(loc, term_sub_set(term_new_got(mov_loc), 1));
  return term_new(0, side == 0 ? DP0 : DP1, lab, mov_loc);
}
