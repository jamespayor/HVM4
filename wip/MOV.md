MOV nodes
=========

We seek to implement `MOV` nodes. These are fan-out nodes with the promise that
only one output will be demanded (it gets a bit more complicated but anyway),
and therefore can enjoy asymptotic efficiency gains over using a `DUP` node.

We need to implement the following:
- `MOV` node stored on the heap with a cell pointing at its contents
- `GOT` tag for pointers into a `MOV` (that all point at the cell)
- Parsing syntax like `λ%s. f(s, s)` and `!%s = _; f(s, s)` for building a `MOV` with multiple `GOT`s, mirroring the `λ&s. _` and `!&s = _` syntax for `DUP`s
- Reduction behaviour.

## On reduction of `MOV`

In HVM there is a split between "constructor-like" and "destructor-like" nodes. In terms of the basic nodes, `LAM` and `SUP` are constructor-like, and emit positive information that can be consumed whenever it is encountered; and `APP` and `DUP` are destructor-like, they do not emit any positive information until they can consume a constructor by demanding their input in weak-head-normal-form. HVM4 has other nodes we need to account for also, `MOV` will need interactions that handle all constructor-like nodes.

Further `MOV` will enjoy a pointer-compression optimization: whenever we have a `GOT` that is stuck pointing at another `GOT`, we collapse these so we don't traverse twice again. (This involves making sure a `GOT` is written to one of the `MOV` cells in question, pointing to the main `MOV` cell.)

Now, we have said that `MOV` is destructor-like. So the main question is what happens when it encounters each constructor.

One thing to say is that I think that after it interacts with a constructor, we should write back either an `ERA` or like `ERROR` to the `MOV` cell, to catch invalid use. I favor `ERA` though maybe it's terrible to behave differently depending on which multiple-use happens first.

But anyway I will talk about the interactions with `LAM` and `SUP` and leave you to fill in the rest.

### `MOV`-`LAM`

This is the trickiest one. Suppose in generality that we emit a kind of node called say `MLAM` for "moved lambda". What are its properties?

We have an ostensible guarantee that our `MLAM` will be used at most once (as is usual for HVM terms). One way it may be used "once" is to be called on (i.e. var substitute) a `SUP`, which emulates running it twice. This happens if a `LAM` encounters a `DUP`, and our `MLAM` will need to do similarly.

As in my `movtwice.hvm4` example we can set up scenarios where we can actually get our `MLAM` to encounter two different `DUP`s, even though we made correct local use of the `MOV` node. Here is one kind of example, that was a counterexample to an initial implementation of `MOV`:

```
// version with DUP
@make_A = λ&s. λa. λb.
  λf. f(s(a), s(b))

// version with MOV
@make_B = λ%s. λa. λb.
  λf. f(s(a), s(b))

@id = λa. a
@fst = λa. λb. a
@snd = λa. λb. b

@main =
  !& x = @make_B(@id, @fst, @snd)
  ! fst = x(@fst)
  ! snd = x(@snd)
  fst(snd, @id)

// Output should be 'snd', which should be λa.λb.b
// ...but with make_B the output is 'fst'.
// I suspect this is due to improperly shared reductions across dup branches.
```

The key thing is that we are duplicating the output of `@make_B`, which results in an `APP-SUP` that duplicates `s(a)` and `s(b)`. Our guarantee that `s` would be used once, either from the `s(a)` expression being demanded or the `s(b)` expression being demanded, has been eroded: now depending on what we put in each side of the `SUP` they can be demanded "twice". Our saving grace is that the demand comes through a `DUP`. Our trouble is that the demands may come through different `DUP`s!

So suppose our `MLAM` has a cell or two handy for us. One points at the original `LAM`.

If the `MLAM` directly encounters an `APP`, we are golden, terms are only `APP`'d once, we substitute into the inner `LAM` and carry on.

If the `MLAM` encounters a `DUP`, we will assume that it will be encountered this way at most twice, perhaps through two branches of the same `DUP`, but perhaps through two different `DUP`s. We will duplicate the `LAM` by substituting a `SUP` in, and we will give back to the `DUP` these two answers, but we also record in the `MLAM` the "path not yet taken" (i.e. the `DP1` side if we got here via `DP0`). Then if we make it here again, we assume it is a `DUP` with the same tag and different side, and the `MLAM` returns the cached other side `LAM`.

I don't have a rigorous thing to say about why this would hold. And it may be prudent to add checks on this iteration that indeed the `DUP` tags line up and the sides differ.

Anyway the nice thing about this is that `MLAM` disappears as soon as we reach an `APP` or `DUP`.

### `MOV` - `SUP`

Like before we will think of ourselves as returning some new constructor, let's call it `MSUP`. How should it behave?

As before we have some "it will be used once" promise with a caveat that we might reach it via two different `DUP`s.

Assuming the `DUP` labels agree we have two cases to consider:
(1) `DUP`-`SUP` annihilation: just need to route each side to the corresponding sides of the `DUP`
(2) `DUP`-`SUP` commutation: we return a new `SUP` for each branch, with inner contents each wrapped in `DUP`s. If we reach this twice it should be on the two opposite `DUP` branches.

So we would want our `MSUP` to have some cells that let us track what is going on with the `DUP`s, sanity checks that the labels match if it is encountered twice this way, and remembering the other branch's `SUP`.

## Implementing these

This is not much of a full spec; I will ask you to flesh that out after we verify my thinking checks out. But I think for proving the idea it does make sense to add this full gamut of new node kinds so we can more properly experiment.

In the future things could be optimized down to perhaps just adding `GOT`/`MOV` stuff and being more hacky about how we store information for our split `DUP`s.
