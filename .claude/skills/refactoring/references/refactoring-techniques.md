# Refactoring Techniques — the fix catalog

The mechanical, behavior-preserving moves that turn a named smell into clean code. 60+ techniques in 6 families. High-frequency techniques include before → after patterns; the long tail gets a tight "what / when" so you can pick correctly.

**Apply every technique in small steps with tests green after each.** Most modern IDEs automate the common ones (Extract Method, Rename, Inline) — use the tool when available; it won't make typos.

Jump by family:
- [Composing Methods](#composing-methods) — make methods readable
- [Simplifying Conditional Expressions](#simplifying-conditional-expressions) — tame branching
- [Moving Features Between Objects](#moving-features-between-objects) — put behavior where it belongs
- [Organizing Data](#organizing-data) — give data structure
- [Simplifying Method Calls](#simplifying-method-calls) — clean interfaces
- [Dealing with Generalization](#dealing-with-generalization) — inheritance & abstraction

---

## Composing Methods

Long methods are, as Fowler puts it, "the root of all evil." This family breaks them apart and clarifies what's left.

### Extract Method ★
A fragment can be grouped and named. Move it into its own method named for *what it does*, not how.

```js
// before
function printOwing(invoice) {
  printBanner();
  let outstanding = calculateOutstanding(invoice);
  // print details
  console.log(`name: ${invoice.customer}`);
  console.log(`amount: ${outstanding}`);
}

// after
function printOwing(invoice) {
  printBanner();
  const outstanding = calculateOutstanding(invoice);
  printDetails(invoice, outstanding);
}
function printDetails(invoice, outstanding) {
  console.log(`name: ${invoice.customer}`);
  console.log(`amount: ${outstanding}`);
}
```
Fixes: Long Method, Duplicate Code, Comments. The workhorse technique — reach for it first.

### Inline Method
A method body is as clear as its name and adds no value. Replace calls with the body and delete the method. The inverse of Extract Method; use to undo over-fragmentation or before re-grouping logic differently.

```js
// before                              // after
function rating(driver) {              function rating(driver) {
  return moreThanFive(driver) ? 2 : 1;   return driver.deliveries > 5 ? 2 : 1;
}                                      }
function moreThanFive(d){return d.deliveries>5;}
```

### Extract Variable ★
A complex expression is hard to read. Name part of it with a variable that explains the intent.

```js
// before
return order.quantity * order.itemPrice -
  Math.max(0, order.quantity - 500) * order.itemPrice * 0.05 +
  Math.min(order.quantity * order.itemPrice * 0.1, 100);

// after
const basePrice = order.quantity * order.itemPrice;
const quantityDiscount = Math.max(0, order.quantity - 500) * order.itemPrice * 0.05;
const shipping = Math.min(basePrice * 0.1, 100);
return basePrice - quantityDiscount + shipping;
```

### Inline Temp
A temp holds a simple expression, used once, and gets in the way. Replace references with the expression itself. Often a prep step before Replace Temp with Query.

### Replace Temp with Query ★
You store an expression in a temp, then reuse it. Extract it into a method instead, so any method can reach the value and Extract Method becomes possible.

```js
// before
function price(order) {
  const basePrice = order.quantity * order.itemPrice;
  if (basePrice > 1000) return basePrice * 0.95;
  return basePrice * 0.98;
}

// after
function price(order) {
  if (basePrice(order) > 1000) return basePrice(order) * 0.95;
  return basePrice(order) * 0.98;
}
function basePrice(order) { return order.quantity * order.itemPrice; }
```

### Split Temporary Variable
A temp is assigned more than once for *different* purposes (not a loop/accumulator). Give each purpose its own immutable variable.

```js
// before                          // after
let temp = 2 * (h + w);            const perimeter = 2 * (h + w);
console.log(temp);                 console.log(perimeter);
temp = h * w;                      const area = h * w;
console.log(temp);                 console.log(area);
```

### Remove Assignments to Parameters
The code reassigns a parameter. Use a local variable instead — keeps the parameter meaning "the input," avoids confusion, and enables further refactors.

```js
function discount(input) {        function discount(input) {
  input = Math.max(input, 0);  →    let result = Math.max(input, 0);
  return input * rate;              return result * rate;
}                                 }
```

### Replace Method with Method Object
A long method's tangle of local variables blocks Extract Method. Move the whole method into a new class where those locals become fields; now extract freely within that class.

### Substitute Algorithm
A clearer algorithm exists for the same result. Replace the body wholesale (tests guarantee equivalence). Prerequisite: the method is already small enough to swap confidently.

---

## Simplifying Conditional Expressions

Conditionals "tend to get more and more complicated over time." This family keeps branching legible.

### Decompose Conditional ★
A complex `if/else` is hard to read. Extract the condition and each branch into intention-revealing methods.

```js
// before
if (date.before(SUMMER_START) || date.after(SUMMER_END))
  charge = quantity * winterRate + winterServiceCharge;
else
  charge = quantity * summerRate;

// after
if (isOffSeason(date)) charge = offSeasonCharge(quantity);
else                   charge = summerCharge(quantity);
```

### Consolidate Conditional Expression
Several checks have the same result. Combine them with `&&`/`||` and extract into one named method.

```js
// before
if (e.seniority < 2)      return 0;
if (e.monthsDisabled > 12) return 0;
if (e.isPartTime)         return 0;

// after
function isIneligible(e) {
  return e.seniority < 2 || e.monthsDisabled > 12 || e.isPartTime;
}
if (isIneligible(e)) return 0;
```

### Consolidate Duplicate Conditional Fragments
The same code sits in every branch of a conditional. Move it out (above or below the branch) so it runs once.

```js
if (special) { total = price * 0.95; send(); }   if (special) total = price * 0.95;
else         { total = price * 0.98; send(); }   else         total = price * 0.98;
                                            →    send();
```

### Replace Nested Conditional with Guard Clauses ★
Nested `if/else` hides the main path. Return early on each special case; leave the happy path flat. (See principle #3.)

```js
function getPay(e) {                    function getPay(e) {
  if (e.isDead) return deadAmount();      if (e.isDead)     return deadAmount();
  else {                          →       if (e.isRetired)  return retiredAmount();
    if (e.isRetired) return retiredAmount();  return normalPay(e);
    else return normalPay(e);           }
  }
}
```

### Replace Conditional with Polymorphism ★
A conditional chooses behavior by an object's type. Move each branch into an overriding method on a subclass; the conditional vanishes and adding a type means adding a class, not editing a switch.

```js
// before
function speed(bird) {
  switch (bird.type) {
    case "european": return baseSpeed();
    case "african":  return baseSpeed() - load(bird);
    case "norwegian":return bird.isNailed ? 0 : voltageSpeed(bird);
  }
}

// after
class EuropeanSwallow  { speed() { return baseSpeed(); } }
class AfricanSwallow   { speed() { return baseSpeed() - this.load(); } }
class NorwegianBlue    { speed() { return this.isNailed ? 0 : this.voltageSpeed(); } }
// caller: bird.speed();
```
Fixes: Switch Statements, some Long Method. Don't over-apply — a single, local two-branch `if` is clearer as an `if`.

### Remove Control Flag
A boolean flag steers loop/branch flow. Replace with `break`, `continue`, or `return`.

### Introduce Null Object
Repeated `if (x == null)` checks scatter everywhere. Replace null with a NullObject that answers every call with sane defaults, so callers stop null-checking.

```js
class NullCustomer { get name() { return "occupant"; } get plan() { return BASIC; } }
// customer = found ?? new NullCustomer();  → callers just use customer.name
```

### Introduce Assertion
A section assumes something that isn't stated (e.g. value is non-negative). Make it explicit with an assertion — documents the contract and fails loudly when violated. Assertions must never carry program logic.

---

## Moving Features Between Objects

Put data and the behavior that uses it in the same place.

### Move Method ★
A method touches another class more than its own (Feature Envy). Move it to the class it envies; leave a delegating stub only if external callers still need the old location.

```js
// before — Account reaches into AccountType for everything
class Account {
  overdraftCharge() {
    if (this.type.isPremium) return this.daysOverdrawn * 1.75;
    return this.daysOverdrawn * 1.0;
  }
}
// after — the rule lives with the type that owns it
class AccountType {
  overdraftCharge(daysOverdrawn) {
    return this.isPremium ? daysOverdrawn * 1.75 : daysOverdrawn * 1.0;
  }
}
```

### Move Field
A field is used more by another class than its own. Relocate it there (often paired with Move Method).

### Extract Class ★
One class is doing two jobs (Large Class, Divergent Change). Split the secondary responsibility into a new class.

```js
// before
class Person {
  name; officeAreaCode; officeNumber;
  telephoneNumber() { return `(${this.officeAreaCode}) ${this.officeNumber}`; }
}
// after
class TelephoneNumber {
  areaCode; number;
  toString() { return `(${this.areaCode}) ${this.number}`; }
}
class Person { name; telephone = new TelephoneNumber(); }
```

### Inline Class
A class no longer pulls its weight (Lazy Class). Fold its members into its main user and delete it. Inverse of Extract Class.

### Hide Delegate
Clients call `server.getDelegate().method()` (Message Chain), coupling them to the delegate. Add a method on the server that hides the hop.

```js
// client: manager = john.department.manager;   →   manager = john.manager();
class Person { manager() { return this.department.manager; } }
```

### Remove Middle Man
A class only forwards calls (Middle Man). Let clients talk to the delegate directly. Inverse of Hide Delegate — balance the two.

### Introduce Foreign Method
A library class lacks a method you need and you can't change it. Add a helper in your client class that takes the library object as its first argument.

### Introduce Local Extension
You need several such methods. Create a subclass or wrapper of the library class that adds them, keeping the extra behavior in one cohesive place.

---

## Organizing Data

Make data easier and safer to work with.

### Replace Magic Number with Symbolic Constant ★
A literal carries unexplained meaning. Name it. (See principle #4.)

```js
const GRAVITATIONAL_CONSTANT = 9.81;
return GRAVITATIONAL_CONSTANT * mass * height;
```

### Encapsulate Field
A field is public. Make it private and expose accessors, so you control reads/writes and can change representation later.

### Encapsulate Collection
A getter returns the live collection, letting callers mutate it behind your back. Return a read-only copy/view and provide explicit `add`/`remove` methods.

```js
class Course {}
class Person {
  #courses = [];
  get courses() { return [...this.#courses]; }     // read-only view
  addCourse(c) { this.#courses.push(c); }
  removeCourse(c) { /* ... */ }
}
```

### Replace Data Value with Object
A primitive grows behavior/validation needs (Primitive Obsession). Promote it to its own class.

```js
// "555-1234" string everywhere  →  class PhoneNumber { constructor(raw){…} format(){…} }
```

### Replace Array with Object
An array holds heterogeneous items addressed by position (`row[0]` = name, `row[1]` = age). Replace with an object whose named fields say what each slot means.

### Replace Type Code with Class / Subclasses / State-Strategy
A numeric/string "type code" controls behavior.
- **with Class** — when the code doesn't affect behavior: wrap it in a class for type safety.
- **with Subclasses** — when behavior varies by code and the code is fixed at creation: one subclass per value (enables Replace Conditional with Polymorphism).
- **with State/Strategy** — when behavior varies *and* the value changes at runtime: delegate to a swappable state/strategy object.

### Self Encapsulate Field
Even internal code reads fields directly. Route through getters/setters so subclasses can override and computed fields become possible.

### Replace Subclass with Fields
Subclasses differ only by constant return values. Collapse them into one class with fields. Inverse of Replace Type Code with Subclasses.

### Change Value to Reference / Reference to Value
- **Value → Reference**: many equal value-objects should be one shared instance (identity matters, e.g. one Customer). Route creation through a factory/registry.
- **Reference → Value**: a reference object is small and immutable; make it a value with `equals`/`hashCode`. Easier in distributed/concurrent code.

### Change Uni- ↔ Bidirectional Association
Add a back-pointer only when a class genuinely needs to navigate back; remove it the moment that need disappears to cut coupling. Bias toward unidirectional.

### Duplicate Observed Data
Domain data is trapped inside a GUI class. Copy it into a domain object and keep the two in sync via an observer, so business logic stops living in the UI.

---

## Simplifying Method Calls

Interfaces should be easy to understand and use correctly.

### Rename Method ★
The name doesn't reveal intent. Rename it (and update callers — let the IDE do it).

### Introduce Parameter Object ★
A clump of parameters always travels together (Data Clumps, Long Parameter List). Bundle them into an object — which often attracts related behavior later.

```js
// before
function invoice(start, end, customer) { … amountInvoiced(start, end) … }
// after
class DateRange { constructor(start, end){…} get days(){…} }
function invoice(range, customer) { … amountInvoiced(range) … }
```

### Preserve Whole Object
You extract several values from one object just to pass them in. Pass the whole object instead; shortens the list and decouples from its internals.

```js
// before                                  // after
const low = days.low, high = days.high;    if (room.withinRange(daysTempRange)) …
if (room.withinRange(low, high)) …
```

### Add Parameter / Remove Parameter
Add when the method needs more info there's no other way to get; remove the moment a parameter goes unused (Dead Code / Speculative Generality). Prefer Preserve Whole Object or Replace Parameter with Method Call over piling on parameters.

### Separate Query from Modifier
A method returns a value *and* changes state — surprising and hard to test. Split into a pure query and a separate command (command-query separation).

### Parameterize Method
Several methods do the same thing with different constants. Merge into one that takes the value as a parameter.

### Replace Parameter with Explicit Methods
A method branches on an enum-like parameter. Replace with one clearly named method per case (inverse of Parameterize Method) — callers read better and you drop the internal switch.

### Replace Parameter with Method Call
The callee can obtain a value itself instead of receiving it. Drop the parameter and let it ask.

### Remove Setting Method / Hide Method
- **Remove Setting Method**: a field should be set only at creation — drop its setter to make it immutable.
- **Hide Method**: a method isn't used outside its class — make it private to shrink the public surface.

### Replace Constructor with Factory Method
Construction needs more than a plain `new` (choose a subclass, return cached instances, validate). Use a named factory method that can encode the choice.

```js
// Employee.create(type)  picks Engineer | Manager | Salesman
```

### Replace Error Code with Exception ★
A method returns a special error code callers must remember to check. Throw an exception instead — errors can't be silently ignored, and the happy path stays clean.

```js
// before                               // after
function withdraw(amt){                 function withdraw(amt){
  if (amt > balance) return -1;           if (amt > balance) throw new BalanceError();
  balance -= amt; return 0;               balance -= amt;
}                                       }
```

### Replace Exception with Test
You catch an exception for a condition you could simply check first (exceptions shouldn't drive normal control flow). Replace the try/catch with an `if`.

```js
// before                               // after
try { return values[i]; }              return i < values.length ? values[i] : 0;
catch (e) { return 0; }
```

---

## Dealing with Generalization

Move features up, down, and across an inheritance hierarchy to remove duplication and clarify roles.

### Pull Up Method / Field / Constructor Body
Subclasses share identical members. Move them to the superclass (Pull Up Method/Field), and shared constructor setup into the parent constructor (Pull Up Constructor Body). Kills duplication.

### Push Down Method / Field
A superclass member is used by only one subclass. Move it down to where it belongs — the parent stops carrying irrelevant baggage (treats Refused Bequest).

### Extract Superclass
Two classes share features. Create a common superclass and pull the shared parts up.

### Extract Subclass
Some instances use features the rest don't (often signaled by a Temporary Field or a type code). Create a subclass for those features.

### Extract Interface
Several clients use the same subset of a class's methods, or two classes share part of their interface. Define an interface for that subset to decouple clients from the concrete class.

### Collapse Hierarchy
A subclass and superclass are barely different (Speculative Generality / Lazy Class). Merge them into one.

### Form Template Method ★
Two subclasses run the same algorithm with steps that differ. Pull the shared skeleton into a superclass method that calls overridable step-methods; subclasses fill in only the differences.

```js
// Parent.statement() = header() + body() + footer();  body() is abstract,
// each subclass overrides only body(). The shape lives once, in the parent.
```
Fixes: Duplicate Code across sibling subclasses.

### Replace Inheritance with Delegation ★
A subclass uses only part of its parent (Refused Bequest) or inheritance leaks unwanted members. Give the class a *field* of the former superclass and delegate the calls it actually needs — "has-a" instead of a wrong "is-a."

```js
// before: class Stack extends Vector { … }   // exposes all of Vector, wrongly
// after:
class Stack {
  #items = new Vector();
  push(x) { this.#items.add(x); }
  pop()   { return this.#items.removeLast(); }
}
```

### Replace Delegation with Inheritance
A class delegates *so* much to one object that it's all boilerplate forwarding, and the delegate isn't shared elsewhere. Make it a subclass instead. Inverse of the above — only when delegation has clearly overgrown.

---

## Choosing the right move quickly

| You see… | Reach for |
|---|---|
| A method too long to read | Extract Method (+ Extract Variable, Replace Temp with Query) |
| Arrow-shaped nesting | Replace Nested Conditional with Guard Clauses |
| `switch`/`if` on a type | Replace Conditional with Polymorphism |
| Same code in N places | Extract Method → Pull Up / Form Template Method |
| A class doing two jobs | Extract Class |
| 4+ params / params from one object | Introduce Parameter Object / Preserve Whole Object |
| Bare literals & primitives for domain ideas | Replace Magic Number; Replace Data Value with Object |
| `a.b().c().d()` chains | Hide Delegate |
| Error codes / null checks everywhere | Replace Error Code with Exception; Introduce Null Object |
| Wrong `is-a` inheritance | Replace Inheritance with Delegation |

When two techniques are inverses (Extract ↔ Inline, Hide Delegate ↔ Remove Middle Man), the right one is whichever moves *this* code toward fewer moving parts and clearer intent. There is no universal direction — only the cleaner result for the case in front of you.
