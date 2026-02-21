// Lookup binding by name. Skips to outer binds on false shadowing (dup var + lam bind, lam var + dup bind, etc)
// Returns 1 and sets the bind if found, skipped = 1 if skipped a binding with the same name.
fn PBind* parse_bind_lookup(u32 name, int side, int *skipped) {
  *skipped = 0;
  for (int i = PARSE_BINDS_LEN - 1; i >= 0; i--) {
    PBind* bind = &PARSE_BINDS[i];
    if (bind->name == name) {
      // Skip dup bindings if no subscript and not in fork mode
      // (MOV bindings use no subscript, so don't skip them)
      if (side == -1 && bind->lab != 0 && bind->lab != PARSE_MOV_LAB && !bind->forked) {
        *skipped = 1;
        continue;
      }
      // Skip non-dup bindings if subscript or fork mode
      // (MOV bindings also cannot have subscripts)
      if (side != -1 && (bind->lab == 0 || bind->lab == PARSE_MOV_LAB)) {
        *skipped = 1;
        continue;
      }
      return bind;
    }
  }
  return NULL;
}
