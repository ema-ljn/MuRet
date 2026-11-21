# Clean Code Principles

Foundational habits that prevent smells before they start. Language-agnostic; examples use a readable JS/TS-ish or Python style, but the patterns apply to any object-oriented or procedural codebase.

Read this when the goal is "write/keep this clean," not necessarily "untangle a specific smell." For diagnosing existing mess, use `code-smells.md`; for the mechanical fixes, use `refactoring-techniques.md`.

---

## 1. Intention-revealing names

A name should answer: why does this exist, what does it do, how is it used. If a name needs a comment to explain it, the name is wrong.

```js
// before
const d = 30;                 // elapsed time in days
function getThem(list) {
  const r = [];
  for (const x of list) if (x[0] === 4) r.push(x);
  return r;
}

// after
const ELAPSED_DAYS = 30;
function getFlaggedCells(board) {
  return board.filter(cell => cell.isFlagged());
}
```

Rules of thumb:
- Pronounceable and searchable. `genymdhms` → `generationTimestamp`.
- No type/scope encoding (`strName`, `m_count`) unless the codebase already does it.
- Verbs for functions (`isValid`, `calculateTotal`), nouns for values (`total`, `customer`).
- One word per concept: don't mix `fetch`, `get`, `retrieve` for the same idea.
- Length should match scope: a 2-line loop index `i` is fine; a module-level export is not.

## 2. Functions do one thing, at one level of abstraction

A function should read like a short paragraph at a single altitude. Mixing high-level policy with low-level detail forces the reader to constantly change zoom.

```js
// before — mixes orchestration with byte-level detail
function sendReport(user) {
  const rows = db.query("SELECT * FROM events WHERE uid=" + user.id);
  let html = "<table>";
  for (const row of rows) html += "<tr><td>" + row.name + "</td></tr>";
  html += "</table>";
  smtp.connect(); smtp.mail(user.email, html); smtp.close();
}

// after — each line is one idea; details live one level down
function sendReport(user) {
  const events = loadEvents(user);
  const html = renderEventsTable(events);
  email(user, html);
}
```

Heuristics: a function that needs the words "and" / "then" to describe it is doing too much. Deeply nested blocks (> 3-4 levels) usually hide an extractable function. See *Extract Method*.

## 3. Flatten with guard clauses; avoid arrow-shaped code

Handle special/edge cases first and return early. Reserve the main body for the happy path.

```js
// before — the real logic is buried under nesting
function getPayAmount(employee) {
  let result;
  if (employee.isSeparated) {
    result = { amount: 0, reason: "separated" };
  } else {
    if (employee.isRetired) {
      result = { amount: 0, reason: "retired" };
    } else {
      result = computeNormalPay(employee);
    }
  }
  return result;
}

// after — guard clauses; happy path is unindented and obvious
function getPayAmount(employee) {
  if (employee.isSeparated) return { amount: 0, reason: "separated" };
  if (employee.isRetired)   return { amount: 0, reason: "retired" };
  return computeNormalPay(employee);
}
```

## 4. Replace magic numbers and strings with named constants

A bare literal forces the reader to guess its meaning and makes every duplicate a future bug.

```python
# before
if temperature > 100:        # 100 what? why?
    shutdown()
total = subtotal * 1.0825    # mystery tax

# after
BOILING_POINT_C = 100
SALES_TAX_RATE = 0.0825
if temperature > BOILING_POINT_C:
    shutdown()
total = subtotal * (1 + SALES_TAX_RATE)
```

Exception: truly self-evident values (`0`, `1`, `2` for halving) don't need names. Don't over-constantize.

## 5. DRY — every piece of knowledge has one home

Duplication isn't just copy-pasted lines; it's any knowledge expressed in two places that must change together.

```js
// before — the discount rule lives in two places
function cartTotal(items)  { return sum(items) * 0.9; }   // 10% off
function quote(items)      { return sum(items) * 0.9; }   // ...and here

// after — one source of truth
const LOYALTY_DISCOUNT = 0.9;
const applyDiscount = (amount) => amount * LOYALTY_DISCOUNT;
function cartTotal(items) { return applyDiscount(sum(items)); }
function quote(items)     { return applyDiscount(sum(items)); }
```

Caution: don't unify code that merely *looks* similar but represents different decisions (false DRY) — that creates the wrong coupling. Unify knowledge, not coincidence.

## 6. Use explaining variables and queries instead of dense expressions

Name intermediate results so the formula reads in domain terms.

```js
// before
if (platform.includes("mac") && browser.includes("ie") && initialized && resize > 0) { ... }

// after
const isMacIE = platform.includes("mac") && browser.includes("ie");
const wasResized = resize > 0;
if (isMacIE && initialized && wasResized) { ... }
```

See *Extract Variable* and *Replace Temp with Query*.

## 7. Comments: explain *why*, never *what*

The code already says what it does. A comment that restates the code rots the moment the code changes. Good comments capture intent, trade-offs, warnings, or links a reader can't infer.

```js
// bad — redundant, will lie after the next edit
i = i + 1; // increment i

// bad — comment compensating for a poor name
const list2 = filter(list); // active users only

// good — names removed the need for a comment
const activeUsers = filterActive(allUsers);

// good — explains a non-obvious why
// Stripe rounds half-up; mirror that here so our totals reconcile with their dashboard.
const cents = Math.round(amount * 100);
```

Prefer making code self-explanatory (better names, extracted methods) over adding a comment. Delete commented-out code — version control remembers it.

## 8. Keep one consistent abstraction per module/class

A class should group things that change together for the same reason (high cohesion) and depend minimally on others (low coupling). When a class accumulates unrelated responsibilities, split it by feature/domain — not by technical type. See *Extract Class* and the *Large Class* / *Divergent Change* smells.

## 9. Prefer immutability and narrow scope

- Declare variables as close to first use as possible; give each variable a single responsibility (see *Split Temporary Variable*).
- Don't reassign parameters (see *Remove Assignments to Parameters*).
- Default to read-only data; expose collections as read-only views with explicit add/remove methods (see *Encapsulate Collection*).

## 10. Fail loudly, not silently

Don't swallow errors, fake success, or return sentinels that hide problems. Make assumptions explicit (*Introduce Assertion*), throw exceptions instead of error codes (*Replace Error Code with Exception*), and fix root causes rather than patching symptoms.

---

### How these map to fixes

| Principle violated | Smell it becomes | Technique to apply |
|---|---|---|
| Vague names | Comments, hard-to-read code | Rename Method / Extract Variable |
| Function does many things | Long Method | Extract Method, Decompose Conditional |
| Arrow-shaped nesting | Long Method | Replace Nested Conditional with Guard Clauses |
| Magic literals | Primitive Obsession | Replace Magic Number with Symbolic Constant |
| Copy-paste knowledge | Duplicate Code | Extract Method, Pull Up Method, Form Template Method |
| Dense expressions | Long Method | Extract Variable, Replace Temp with Query |
| Explanatory comments | Comments (smell) | Extract Method, Rename, Introduce Assertion |
| Class does too much | Large Class / Divergent Change | Extract Class |
