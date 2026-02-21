fn Term parse_term(PState *s, u32 depth);

// % x = val; body  →  MOV[val, body_with_BJGs]
// Called after '%' and name have been consumed, '=' has been consumed.
fn Term parse_term_mov_body(PState *s, u32 depth, u32 nam) {
  Term val = parse_term(s, depth);
  parse_skip(s);
  parse_match(s, ";");
  parse_skip(s);
  parse_bind_push(nam, depth, PARSE_MOV_LAB, 0, 0);
  Term body = parse_term(s, depth + 1);
  parse_bind_pop();
  // No affinity check: MOV allows multiple uses.
  u64 loc = heap_alloc(2);
  HEAP[loc + 0] = val;
  HEAP[loc + 1] = body;
  return term_new(0, MOV, 0, loc);
}
