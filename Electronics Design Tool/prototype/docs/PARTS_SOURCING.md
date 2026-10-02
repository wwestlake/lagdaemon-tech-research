# Parts Sourcing Research Notes

Parts sourcing helps a hobby user turn a schematic into something they can
actually buy and build.

Sourcing is separate from verified component specifications. A purchase listing
can help the user buy a part, but it should not overwrite datasheet-backed
component facts.

## Record Types

- `sourcing_provider`: distributor, hobby supplier, marketplace, user inventory,
  or kit source.
- `sourcing_listing`: a buyable listing with URL, title, price, package notes,
  quantity, and timestamp.
- `sourcing_match`: relationship between a listing and a part family,
  manufacturer part, or placed instance.
- `substitution`: suggested equivalent or acceptable replacement with rationale.

## Confidence

- **High**: exact MPN match from distributor or manufacturer source.
- **Medium**: trusted hobby supplier listing or clearly compatible substitute.
- **Low**: keyword match from a marketplace.
- **Warning**: ambiguous kit, clone/counterfeit risk, unclear package, or vague
  "compatible with" language.

## Hobby Workflow

The user may ask:

- Where can I buy this?
- Can I get it on Amazon?
- Is there a breadboard-friendly version?
- Can I buy a kit that covers these parts?
- What can I substitute?
- Do I already have something close enough in my inventory?

The agent should search local sourced/cached records first, then use approved
provider tools. Search results should be saved as provisional sourcing records
with timestamps so the tool can show staleness.

## Schematic Relationship

A placed schematic component should be able to bind to:

```text
archetype -> part family -> manufacturer part -> sourcing listings
```

The schematic remains valid if sourcing is missing. Sourcing is an assistive
build/procurement layer, not the electrical truth of the design.
