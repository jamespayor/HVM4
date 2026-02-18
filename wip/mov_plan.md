MOV Implementation Plan
=======================

## Design Overview

MOV is a sharing node for the Interaction Calculus. It allows a value to be
referenced from multiple positions (via GET/GOT pointers) without eager
duplication. The contract: at most one output path will be actively demanded in
any single evaluation branch. Through DUPs, the value may be demanded up to
twice (once per DUP side).

Key difference from the `mov-part-3` branch: that approach eagerly distributes
MOV over constructors (MOV-LAM creates fresh lambdas). We use **lazy
indirection**: GET/GOT are pointers into a shared MOV cell, and frame handlers
(APP, DUP, etc.) interact with GOT directly. This avoids the variable binding
collision bug in `mov-part-3`.

## New Tags

```
MOV  46   // arity 2: HEAP[loc+0] = val, HEAP[loc+1] = body
GET  47   // arity 0: val = mov_val_cell location. "might be neutral, reduce first"
GOT  48   // arity 0: val = mov_val_cell location. "contains constructor, interact"
BJG  49   // arity 0: val = de Bruijn level. Quoted GET for book/static terms
```

GET and GOT both point at the same MOV cell (`HEAP[mov_loc+0]`). The difference:
- **GET** is the "unreduced" pointer. In WNF enter, it reads the cell, pushes a
  frame, and descends to reduce the value. After reduction, GET's frame writes
  the WNF result back and returns GOT if it's a constructor.
- **GOT** is the "resolved" pointer. It is WHNF — frame handlers dispatch on it.
  GOT reads the MOV cell and performs interactions (APP-GOT, DUP-GOT, etc.).
- **BJG** is the quoted/static form (like BJV for LAM, BJ0/BJ1 for DUP).

## MOV Cell Layout and State Machine

### Book vs Dynamic representation

**Book (static) terms**: MOV is a node with arity 2 (`BOOK[loc+0] = val,
BOOK[loc+1] = body`). This structural representation is needed so ALO can
recognize the binding, expand the val, and extend the bind list.

**Dynamic terms**: MOV dissolves. ALO-MOV allocates a MOV cell (1-2 heap
cells) and returns the expanded body directly. No MOV node exists in the
dynamic term tree. The evaluator never sees `case MOV:` — only GET/GOT.

This differs from DUP, which keeps a dynamic node (so the evaluator can
enter it and skip to the body). DUP needs this because dynamic DUPs are
also created by interactions (DUP-LAM, etc.). MOV is only ever created
from book terms via ALO, so it can dissolve cleanly.

### Dynamic MOV cell layout

```
HEAP[mov_loc+0] = val          (the shared value; GETs point here)
HEAP[mov_loc+1] = debug state  (demand count, DUP label, etc. — optional)
```

Allocated by ALO-MOV. GETs reference `mov_loc`. The debug cell is at
`mov_loc+1` (initialized to `NUM(0)` or `0`).

### Val cell state machine

The val cell (`HEAP[mov_loc+0]`) transitions through states:

```
  State 0: Unreduced value (SUB=0)
    The initial term (VAR, REF, APP, etc.) hasn't been reduced yet.
    GET enters: push frame, descend, reduce.

  State 1: WNF constructor (SUB=0)
    After GET reduces the value, the WNF result (LAM, SUP, CTR, NUM, etc.)
    is written back. GET returns GOT to frame handlers.

  State 2: Cached DUP result (SUB=1)
    After first DUP-GOT interaction: DUP-constructor produces copy_A and
    copy_B. copy_A goes to the DUP. copy_B is written to the MOV cell with
    SUB=1 as a cache for the second demand.

  State 3: Consumed / ERA (SUB=1)
    After a consuming interaction (APP-GOT) or after the cached value is
    taken (second demand), ERA with SUB=1 is written. Further access is a
    contract violation → runtime error.
```

GET enter behavior:
```
  Read cell.
  If SUB=1 → cached/consumed value. Take it (clear SUB), write ERA to cell,
             enter the taken value. (No GET frame pushed.)
  If SUB=0 → push GET frame, descend into cell value to reduce.
```

GET frame in apply:
```
  Write WNF result back to cell (SUB=0).
  If result is a "constructive" WHNF (LAM, SUP, CTR, NUM, ERA, MAT, SWI,
    USE, INC, ANY): return GOT(mov_loc) as whnf.
  If result is "neutral" (NAM, DRY, BJV, BJ0, BJ1): return the neutral
    directly as whnf (not GOT).
```

GOT enter behavior:
```
  GOT is WHNF. Set whnf = GOT term, goto apply.
```

## GOT Interactions (Frame Handlers)

Every frame handler in wnf/_.c needs a `case GOT:` branch. The pattern:

1. Read MOV cell: `inner = heap_read(mov_loc)`
2. Check SUB bit to distinguish first vs second encounter
3. Dispatch based on inner's tag and the frame type

### APP + GOT

```
Read MOV cell → inner (constructor).
If SUB=0 (first encounter):
  Dispatch based on inner tag:
    LAM → APP-LAM(inner, arg). Write ERA|SUB to cell.
    SUP → APP-SUP(app_term, inner). Write ERA|SUB to cell.
    ERA → APP-ERA(). Write ERA|SUB to cell.
    INC → APP-INC(app_term, inner). Write ERA|SUB to cell.
    NAM/DRY → APP-NAM/APP-DRY. Write ERA|SUB to cell.
    MAT/SWI/USE → push MAT/USE frame, enter arg. Write ERA|SUB to cell.
    NUM/CTR → runtime error (can't apply number/constructor).
If SUB=1 (cached from DUP):
  Take cached value (clear SUB), write ERA|SUB.
  Dispatch same as above using the cached value as inner.
```

### DP0/DP1 + GOT (DUP-GOT)

This is the core interaction for MOV correctness.

```
Read MOV cell → inner.
If SUB=0 (first encounter):
  Dispatch DUP-constructor based on inner tag:
    LAM → DUP-LAM(lab, ..., inner) → copy_A, copy_B.
           Cache copy_B in MOV cell (SUB=1).
           Write GOT(mov_loc)|SUB to DUP cell (for other DP side).
           Return copy_A.
    SUP → DUP-SUP(lab, ..., inner) → result.
           If same label (annihilation): cache one side, return other.
           If diff label (commutation): cache one result, return other.
           Write GOT(mov_loc)|SUB to DUP cell.
    CTR → DUP-NOD(lab, ..., inner) → copy_A, copy_B.
           Cache copy_B in MOV cell (SUB=1).
           Write GOT(mov_loc)|SUB to DUP cell.
           Return copy_A.
    NUM/ERA/NAM/DRY/ANY → DUP-NOD or DUP-NAM.
           These are freely shareable. Cache value (SUB=1).
           Write GOT(mov_loc)|SUB to DUP cell.
           Return value.
    Other (MAT, INC, OP2, etc.) → DUP-NOD(lab, ..., inner).
           Cache copy_B. Return copy_A.
If SUB=1 (cached from previous DUP-GOT):
  Take cached value (clear SUB). Write ERA|SUB to cell.
  Return cached value via heap_subst_cop or directly.
```

**Critical invariant**: DUP-GOT fires at most twice on the same MOV cell.
First encounter creates copies and caches. Second encounter returns cache.
Third encounter finds ERA → runtime error (contract violation).

**Note on DUP-GOT vs standard DUP-X**: DUP-GOT wraps the standard DUP-X
interactions but additionally:
  - Writes the "other" copy to the MOV cell (not the DUP cell)
  - Writes GOT(mov_loc) to the DUP cell (so the other DP goes through MOV)
This ensures both DUP branches AND other GETs all go through the MOV cell,
maintaining the "at most twice" invariant.

### MAT/SWI + GOT

```
Read MOV cell → inner.
Dispatch based on inner tag:
  CTR → MAT-CTR(mat, inner). Write ERA|SUB.
  NUM → MAT-NUM(mat, inner). Write ERA|SUB.
  SUP → MAT-SUP(mat, inner). Write ERA|SUB.
  INC → MAT-INC(mat, inner). Write ERA|SUB.
  ERA → APP-ERA(). Write ERA|SUB.
  NAM/DRY → create DRY. Write ERA|SUB.
  Other → stuck, write ERA|SUB.
```

### USE + GOT

```
Read MOV cell → inner.
Dispatch based on inner tag:
  ERA → USE-ERA. Write ERA|SUB.
  SUP → USE-SUP. Write ERA|SUB.
  INC → USE-INC. Write ERA|SUB.
  Other → USE-VAL. Write ERA|SUB.
```

### OP2 + GOT

```
Read MOV cell → inner.
Dispatch like OP2's x-side or y-side based on inner:
  NUM → transition to F_OP2_NUM or compute.
  SUP → OP2-SUP.
  INC → OP2-INC.
  ERA → OP2-ERA.
  Other → stuck.
Write ERA|SUB after interaction.
```

### F_OP2_NUM + GOT

```
Same pattern: read inner, dispatch as y-side NUM interactions.
```

### EQL + GOT

```
Read inner, dispatch as EQL's a-side or b-side.
```

### F_EQL_R + GOT

```
Read inner, dispatch as EQL's b-side (both sides reduced).
```

### AND/OR + GOT

```
Read inner, dispatch as AND/OR's a-side.
```

### DSU/DDU + GOT

```
Read inner, dispatch as DSU/DDU's label-side.
```

### wnf_rebuild + GOT

The rebuild function also needs a GOT case (for step-limiting). GOT in
rebuild should be treated as opaque: just return it as-is since it's WHNF.

## Parser Changes

### Syntax

Two forms, mirroring DUP's `λ&x. body` and `!x& = val; body`:

```
λ%x. body         →  LAM[MOV[BJV(lam_level), body_with_BJGs]]
!% x = val; body  →  MOV[val, body_with_BJGs]
```

Every use of `x` in the body becomes `BJG(mov_level)`.

### Parse `λ%x. body`

In `parse_term_lam.c`, detect `%` after `λ`:
1. Parse name `x`
2. Push binding: name=x, lvl=depth+1, lab=MOV_MARKER, cloned=0
3. Parse body at depth+2 (LAM at depth, MOV at depth+1)
4. Pop binding
5. Build: `LAM(ext=flags, val=lam_loc)` where
   `HEAP[lam_loc] = MOV[BJV(depth), body]` (book term equivalent)

No affinity checking needed: MOV explicitly allows multiple uses.
No auto_dup needed: all uses are BJG references to the same MOV.

### Parse `!% x = val; body`

In `parse_term_dup.c`, detect `%` after `!`:
1. Parse name `x`
2. Consume `=`
3. Parse val at current depth
4. Consume `;`
5. Push binding: name=x, lvl=depth, lab=MOV_MARKER
6. Parse body at depth+1
7. Pop binding
8. Build: `MOV[val, body]`

### Variable Lookup

In `parse_bind_lookup.c` / `parse_term_var.c`:
- When looking up a name bound with lab=MOV_MARKER, return `BJG(level)`
- BJG has no subscripts (₀/₁) — all references are equivalent

## Printer Changes

In `print/term.c`:

- **MOV**: Print as `!% name = val; body` (dynamic) or book-quoted form
- **GET**: Print as the variable name (like VAR for LAM)
- **GOT**: Print as the variable name (like DP0/DP1 for DUP)
- **BJG**: Print as the variable name (like BJV/BJ0/BJ1)

GET/GOT/BJG naming: use the MOV's body location to assign names, similar to
how LAM's body location assigns lambda names and DUP's expr location assigns
dup names. We need a PRINT_MOVS[] table (like PRINT_LAMS[] and PRINT_DUPS[]).

Naming convention: maybe a distinct sigil or namespace. Could reuse lowercase
names since MOV references look like lambda variables to the user.

## Book Term / ALO Changes

### ALO-MOV (new file: `clang/wnf/alo_mov.c`)

Like ALO-DUP. When ALO encounters a MOV in the book:
```
@{s} MOV[v, t]
──────────────
Allocate dynamic MOV cell.
MOV.val ← @{s} v           (expand val with current substitution context)
MOV.body ← @{s'} t         (expand body with extended context)
s' extends s with a GET pointing at the new MOV cell.
```

This mirrors `wnf_alo_dup` which creates a dynamic DUP from a book DUP.

### ALO-BJG (new handling in `clang/wnf/alo_var.c` or new file)

Like ALO for BJ0/BJ1. When ALO encounters a BJG:
```
@{s} BJG(level)
────────────────
Look up bind list at position (len - level).
Return the bound term (which should be a GET pointing at a MOV cell).
```

This mirrors `wnf_alo_cop` which resolves BJ0/BJ1 via the bind list.

## Normalizer Changes (`clang/eval/normalize.c`)

In `eval_normalize_go`:
- **MOV**: arity 2. Enqueue mov_loc+0 (val) and mov_loc+1 (body). Standard.
- **GET**: Like DP0/DP1, follow the val to the MOV cell. `loc = term_val(cur); goto again;`
  This ensures the MOV cell's value is normalized.
- **GOT**: Same as GET — follow to MOV cell. `loc = term_val(cur); goto again;`

The `uset` (visited set) prevents re-normalization if multiple GET/GOTs point
at the same MOV cell.

Also add GET/GOT to the "already WHNF" fast-path check in `wnf_at`:
- GET is NOT already WHNF (needs reduction). So it goes through `wnf()`.
- GOT IS already WHNF. Add to the fast-path list.

## Collapser Changes (`clang/cnf/_.c`, `clang/eval/collapse.c`)

### CNF quoting

When collapsing (converting linked to quoted terms):
- **GET/GOT** → **BJG** (like VAR→BJV, DP0→BJ0, DP1→BJ1)
- Track the MOV binding level for proper de Bruijn assignment.

### SUP lifting

If a GOT's MOV cell contains a SUP, the collapser needs to lift it.
In `cnf()`, handle GOT: read MOV cell, if SUP → lift as usual.

### eval_collapse

For the collapse BFS traversal:
- **MOV**: traverse val and body (arity 2)
- **GET/GOT**: follow to MOV cell (like DP0/DP1 follow to DUP cell)
- **BJG**: quoted form, print as variable name

## Term Infrastructure

### term/arity.c
```c
[MOV] = 2, [GET] = 0, [GOT] = 0, [BJG] = 0,
```

### term/new/mov.c, get.c, got.c
Constructor helpers. MOV allocates 2 cells. GET/GOT/BJG are immediate terms.

### term/clone.c
When `term_clone_at` encounters GET/GOT at a location:
- Create DP0/DP1 pair as usual. The DP0/DP1 will resolve through GET/GOT
  to the MOV cell. This is correct: cloning a GET creates a DUP whose sides
  each independently resolve through the GET to the MOV cell.

No special handling needed — the existing clone mechanism works.

## Implementation Order

### Phase 1: Tags and term infrastructure (can be done first)
1. Add MOV/GET/GOT/BJG tag definitions to `hvm4.c`
2. Add arity entries
3. Add term constructors (`term/new/mov.c`, `term/new/get.c`, etc.)
4. Add to `hvm4.c` include list

### Phase 2: Parser (independent of WNF changes)
1. Add BJG handling to `parse/bind_lookup.c` and `parse/term/var.c`
2. Add `λ%x` to `parse/term/lam.c`
3. Add `!% x = val; body` to `parse/term/dup.c`
4. Add MOV_MARKER constant for parser binding type
5. Skip affinity checks for MOV bindings
6. Add BJG to `parse/count_uses.c` traversal

### Phase 3: Printer (independent)
1. Add MOV/GET/GOT/BJG cases to `print/term.c`
2. Add PRINT_MOVS naming table
3. Add MOV discovery pass (like LAM/DUP discovery)

### Phase 4: ALO handling (independent)
1. Create `clang/wnf/alo_mov.c` (like alo_dup.c)
2. Add BJG handling to ALO dispatch in `wnf/_.c` (ALO enter section)
3. Include new files in `hvm4.c`

### Phase 5: WNF core (the main event)
1. Add MOV, GET, GOT to enter phase in `wnf/_.c`
2. Add GET frame handling in apply phase
3. Add GOT to the WHNF fast-path list
4. Add GOT case to wnf_rebuild

### Phase 6: GOT frame interactions
1. Create `clang/wnf/got_*.c` interaction files (or inline in _.c)
   - APP + GOT
   - DP0/DP1 + GOT (DUP-GOT) — the critical one
   - MAT/SWI + GOT
   - USE + GOT
   - OP2 + GOT, F_OP2_NUM + GOT
   - EQL + GOT, F_EQL_R + GOT
   - AND/OR + GOT
   - DSU/DDU + GOT
2. Include in `hvm4.c`

### Phase 7: Normalizer
1. Add GET/GOT follow-through in `eval/normalize.c`
2. Add GOT to fast-path in `wnf_at`

### Phase 8: Collapser
1. Add GET/GOT/BJG handling in `cnf/_.c`
2. Add handling in `eval/collapse.c`

### Phase 9: Testing
1. Port test cases from wip/ and test/mov_*.hvm4
2. Create minimal unit tests for each interaction
3. Verify the bounty test case
4. Run full test suite

## Debug / Correctness Verification Extension

### Goal

During initial development, we want strong runtime checks that our "at most
twice" invariant holds and that DUP labels are consistent. This helps us
catch bugs early and build confidence before relaxing to the ERA-on-violation
behavior for production.

### MOV Cell Debug State

After MOV enters (body cell at `loc+1` is dead), repurpose `loc+1` as a
debug/state cell. Layout:

```
HEAP[mov_loc+0] = val / cached / ERA        (the "live" cell)
HEAP[mov_loc+1] = debug state                (repurposed after MOV enters)
```

Debug state encoding (pack into a u64):
```
Bits  0-7:  demand_count   (0, 1, or 2 — incremented on each access)
Bits  8-31: first_dup_lab  (DUP label from first DUP-GOT interaction, or 0)
Bit   32:   first_dup_side (0 or 1, the side that triggered first DUP-GOT)
Bit   33:   consumed       (1 if APP-GOT or similar consumed the value)
Bits 34-63: reserved
```

### Checks to perform

1. **On GET enter (reading MOV cell)**:
   - Read debug state from `loc+1`
   - Increment demand_count
   - If demand_count > 2: runtime error
     `"MOV contract violation: value demanded %d times (max 2)"`
   - If consumed == 1 and this is a new demand: runtime error
     `"MOV contract violation: value already consumed"`

2. **On DUP-GOT first encounter**:
   - Store first_dup_lab and first_dup_side in debug state

3. **On DUP-GOT second encounter** (cached value):
   - Read debug state: check that the DUP label matches first_dup_lab
   - Check that the side differs from first_dup_side
   - If mismatch: runtime warning/error
     `"MOV DUP label mismatch: expected lab=%d side=%d, got lab=%d side=%d"`

4. **On APP-GOT (or other consuming interaction)**:
   - Set consumed = 1 in debug state
   - If demand_count > 1: runtime warning
     `"MOV value consumed after being cached (possible issue)"`

### Normalizer adjustment

The normalizer traverses MOV's children at `loc+0` and `loc+1` (arity=2).
After MOV enters, `loc+1` contains debug state, not a valid term. Fix:

Option A: Change MOV arity to 1 for normalizer purposes (special case).
Option B: Have MOV enter write a self-referential or no-op term to `loc+1`
           that the normalizer can safely visit (e.g., `ERA` or `NUM(0)`).
Option C: Initialize `loc+1` debug state as `NUM(0)`, which the normalizer
           visits harmlessly (NUM arity=0, no sub-terms to traverse).

**Recommendation**: Option C. Write `NUM(0)` as the initial debug state.
The normalizer visits it, sees NUM (arity 0), and stops. When we update the
debug state, we write packed u64 values that happen to look like NUM terms
(tag=NUM=30, which is fine — normalizer treats any NUM as arity 0).

Actually, this is fragile. Better: **set MOV arity to 1** in the arity table,
and have the normalizer handle MOV body traversal via the MOV enter path
(the evaluator enters MOV → body → normalizer follows). The normalizer only
needs to explicitly enqueue `loc+0` (the val cell). The body is reached
through normal evaluation flow.

### Implementation plan

**Commit 1**: Basic MOV with ERA-on-violation behavior. MOV arity 2, no debug.
**Commit 2**: Add debug state. Change MOV arity to 1 (normalizer only visits
val cell). Repurpose `loc+1` for debug. Add runtime checks. This commit can
be reverted/compiled out for production.

### GET chain collapsing (pointer compression)

When GET enters and follows a chain (GET → MOV cell → GET → MOV cell → ...),
update the intermediate GOT/GET pointers to skip ahead:

```
GET enters:
  Read cell at mov_loc.
  If cell is GET(other_mov_loc):
    // Chain: this GET's MOV cell points at another GET
    // Compress: update this GET to point at other_mov_loc directly
    // (Rewrite the GET term in the heap to point at the final target)
    Follow the chain to the final non-GET value.
    Update HEAP[original_location] = GET(final_mov_loc)  // path compression
    Continue with the final value.
```

This is O(chain_length) on first traversal but O(1) on subsequent traversals.
Important for fusion performance where squared functions create GET chains.

Implement in Phase 5 (WNF core), after basic GET/GOT works.

## Open Questions / Assumptions

1. **"At most twice" invariant**: We assume the MOV cell is demanded at most
   twice total (across all GET/GOT accesses). This holds because:
   - Original program: at most 1 GET demanded per evaluation path
   - Through 1 DUP: 2 paths, up to 2 demands total
   - Nested DUPs operate on COPIES (from DUP-GOT), not the MOV cell
   We should add assertion checks to verify this during testing.

2. **DUP-GOT label matching**: When the second demand arrives (cached, SUB=1),
   we should verify it comes from a DUP with the same label as the first.
   Currently: no verification (we just return the cache). Adding a debug check
   requires storing the label — we could repurpose HEAP[mov_loc+1] (body cell)
   after MOV enters, since the body is no longer needed. For now: trust the
   invariant, add debug checking later.

3. **GOT + GOT interaction**: Can a GOT appear as the WHNF when another GOT
   frame is on the stack? This would mean a GOT's MOV cell contains another
   GOT. This could happen if MOV wraps another GET/GOT. GET would reduce
   through the chain. GOT in the enter phase would need to handle this:
   either follow the chain (like pointer compression) or treat as WHNF.
   Current plan: GOT enters as WHNF. If the MOV cell contains a GOT, the
   frame handler reads it and... should follow the chain. TBD during impl.

4. **Thread safety**: DUP-GOT modifies the MOV cell (caching). If two threads
   simultaneously access the same MOV cell, there could be races. For single-
   threaded execution: no issue. For multi-threaded: may need atomic operations
   on the MOV cell. Defer to later.

5. **Collapser details**: The exact handling of GET/GOT during collapse
   (quoting, SUP lifting) needs careful implementation. Model on DP0/DP1
   handling. Details TBD during Phase 8.
