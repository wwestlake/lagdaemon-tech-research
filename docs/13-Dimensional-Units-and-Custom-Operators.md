# 13 - Compile-Time Dimensional Units & Custom Operators

In **Frust**, physical units and dimensions are checked at **compile-time with zero runtime overhead**, compiling down to raw primitive numbers (`f32`/`f64`) in machine code.

---

## 1. Physical Units & Dimensions

### Base SI & Derived Units
```frust
unit Meter     : Length
unit Kilogram  : Mass
unit Second    : Time

unit Newton    : Force    = Kilogram * Meter / (Second ^ 2)
unit Joule     : Energy   = Newton * Meter
unit Hertz     : Frequency= 1 / Second
```

### Custom Units & Conversions
```frust
unit Foot : Length = 0.3048 * Meter
unit Inch : Length = Foot / 12.0
unit Mile : Length = 5280.0 * Foot
unit Hour : Time   = 3600.0 * Second
unit MPH  : Speed  = Mile / Hour
```

### Compile-Time Enforcement
```frust
let dist: f32[Meter]  = 100.0 * Meter
let time: f32[Second] = 9.58 * Second

let speed: f32[Meter/Second] = dist / time
let speed_mph: f32[MPH]      = speed.to<MPH>()

// COMPILE ERROR: Cannot add Length to Time!
// let invalid = dist + time
```

---

## 2. Custom Operator Definition

```frust
// 1. Infix Pipe Operator `|>`
infix operator 5 left (|>)
pub fn (|>) <A, B>(val: A, func: fn(A) -> B) -> B = func(val)

let val = 42.0 |> sqrt |> double

// 2. Component Wiring Operator `~>`
infix operator 8 right (~>)
pub fn (~>) (a, b) = connect_components(a, b)

let chain = Osc(440.0) ~> Filter(800.0) ~> AudioOut()

// 3. Postfix Unit Symbol `deg`
postfix operator (deg)
pub fn (deg)(val: f32) -> f32[Radians] = (val * 3.14159 / 180.0) * Radians

let angle = 90.0 deg
```
