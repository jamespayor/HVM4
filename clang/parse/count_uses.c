// Count the number of uses of a target variable in a term
// Variables are identified by tag + level (and ext for BJ mode).
fn u32 count_uses(Term t, u32 lvl, u8 tgt, u32 ext) {
  Term *ts = (Term*)malloc(sizeof(Term) * 1024); // not recursive, since this is a desugared term
  int ts_idx = 0;
  ts[ts_idx++] = t;

  u32 uses = 0;
  while (ts_idx > 0) {
    t = ts[--ts_idx];
    u8  tg = term_tag(t);
    u32 vl = term_val(t);
    if (tg == tgt && vl == lvl && (tgt == BJV || tgt == BJG || term_ext(t) == ext)) {
      uses++;
    }
    u32 ari = term_arity(t);
    for (u32 i = 0; i < ari; i++) {
      u64 loc = vl + i;
      ts[ts_idx++] = HEAP[loc];
    }
  }
  free(ts);
  return uses;
}
