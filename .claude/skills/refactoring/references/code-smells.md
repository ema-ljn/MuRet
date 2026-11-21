# Code Smells — the diagnostic catalog

A code smell is a surface symptom that usually points to a deeper problem. Smells don't prove something is wrong — they tell you *where to look*. Use this file to **name** what's wrong, then jump to the matching technique in `refactoring-techniques.md`.

23 smells in 5 families. Each row: what it smells like (how to detect) → the techniques that treat it.

---

## Bloaters
Code, methods, and classes that have grown to unwieldy proportions. They accumulate gradually — no one writes them on purpose.

| Smell | Smells like (detect) | Treat with |
|---|---|---|
| **Long Method** | A method with many lines; you scroll to read it; comments section it into "paragraphs." The single most common smell. | Extract Method; Replace Temp with Query; Decompose Conditional; Replace Method with Method Object |
| **Large Class** | A class with too many fields/methods doing several jobs; "God object." | Extract Class; Extract Subclass; Extract Interface |
| **Primitive Obsession** | Bare strings/ints for domain ideas (currency, phone, type codes); constants instead of small classes; field groups that travel together. | Replace Data Value with Object; Replace Type Code with Class/Subclasses/State-Strategy; Replace Magic Number with Symbolic Constant; Introduce Parameter Object; Replace Array with Object |
| **Long Parameter List** | More than ~3-4 params; callers pass values they just pulled off one object. | Replace Parameter with Method Call; Preserve Whole Object; Introduce Parameter Object |
| **Data Clumps** | The same group of variables appears together in many signatures/fields (e.g. `start, end`; `x, y, z`). | Extract Class; Introduce Parameter Object; Preserve Whole Object |

## Object-Orientation Abusers
Incomplete or incorrect use of OO principles.

| Smell | Smells like (detect) | Treat with |
|---|---|---|
| **Switch Statements** | A `switch`/`if-else` chain on a type code, repeated in several places, that you must extend whenever a new case appears. | Replace Conditional with Polymorphism; Replace Type Code with Subclasses/State-Strategy; Introduce Null Object; Replace Parameter with Explicit Methods |
| **Temporary Field** | A field set/used only in certain circumstances, empty the rest of the time; orphaned instance variables. | Extract Class; Introduce Null Object; Replace Method with Method Object |
| **Refused Bequest** | A subclass uses only a fraction of what it inherits; inherited members don't make sense for it. | Push Down Method/Field; Replace Inheritance with Delegation; Extract Superclass |
| **Alternative Classes with Different Interfaces** | Two classes do the same thing but their methods are named/shaped differently. | Rename Method; Move Method; Extract Superclass; (sometimes) Inline Class |

## Change Preventers
Structure where one change forces many edits elsewhere — the costliest family because it slows every future change.

| Smell | Smells like (detect) | Treat with |
|---|---|---|
| **Divergent Change** | You edit one class for many unrelated reasons (new report format *and* new tax rule *and* new DB). | Extract Class (split by reason for change) |
| **Shotgun Surgery** | One conceptual change forces tiny edits scattered across many classes. The inverse of Divergent Change. | Move Method; Move Field; Inline Class (gather the scattered behavior into one place) |
| **Parallel Inheritance Hierarchies** | Every time you add a subclass in one hierarchy, you must add a matching one in another. | Move Method; Move Field (then collapse one hierarchy) |

## Dispensables
Something pointless whose removal makes the code cleaner.

| Smell | Smells like (detect) | Treat with |
|---|---|---|
| **Comments** | Comments that explain *what* unclear code does (deodorant over bad code). | Extract Method/Variable + Rename; Introduce Assertion. (Keep *why*-comments.) |
| **Duplicate Code** | Identical or near-identical fragments in multiple spots. | Extract Method; Pull Up Method; Form Template Method; Substitute Algorithm; Consolidate Duplicate Conditional Fragments |
| **Lazy Class** | A class that no longer earns its keep. | Inline Class; Collapse Hierarchy |
| **Data Class** | A class that's only fields + getters/setters, with behavior living elsewhere (often Feature Envy). | Move Method (pull behavior in); Encapsulate Field/Collection; Remove Setting Method |
| **Dead Code** | Variables, params, methods, branches no longer reached. | Delete it (Remove Parameter, Inline Class). Trust version control. |
| **Speculative Generality** | Unused hooks "for the future": abstract classes with one child, unused params, over-parameterized methods. | Collapse Hierarchy; Inline Class; Remove Parameter; Rename Method |

## Couplers
Excessive coupling between classes — or the over-delegation that results from "fixing" it badly.

| Smell | Smells like (detect) | Treat with |
|---|---|---|
| **Feature Envy** | A method uses another object's data more than its own. | Move Method; Extract Method (then move the extracted part) |
| **Inappropriate Intimacy** | Two classes reach into each other's private parts; bidirectional dependence. | Move Method/Field; Extract Class; Hide Delegate; Replace Inheritance with Delegation; Change Bidirectional Association to Unidirectional |
| **Message Chains** | `a.getB().getC().getD().doIt()` — client walks a chain of objects. | Hide Delegate; Extract Method + Move Method |
| **Middle Man** | A class that does nothing but delegate to another. | Remove Middle Man; Inline Method; Replace Delegation with Inheritance |
| **Incomplete Library Class** | A library class lacks a method you need but you can't modify it. | Introduce Foreign Method; Introduce Local Extension |

---

## How to use this catalog

1. **Don't fix on sight.** A smell is a hypothesis. Confirm the cost (does it actually make change harder here?) before acting. Speculative Generality and Middle Man are *opposites* of over-fixing other smells — over-correction creates new smells.
2. **Two smells can describe one mess.** Long Method + Duplicate Code + Comments often co-occur; Extract Method frequently fixes all three at once.
3. **Watch the opposing pairs** — they show that "more structure" isn't always cleaner:
   - Divergent Change ↔ Shotgun Surgery
   - Middle Man ↔ Message Chains
   - Speculative Generality ↔ a genuinely needed abstraction
   Aim for the balance point, not the extreme.
4. **Sequence by leverage.** In a broad cleanup, treat Change Preventers and big Bloaters first — they unblock everything else. Cosmetic Dispensables last.
