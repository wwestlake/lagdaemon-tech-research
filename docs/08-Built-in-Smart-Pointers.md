# 08 - Built-in Smart Pointers & Bare-Metal Memory Model

In **Frust**, we eliminate the friction of Rust's explicit lifetime annotations (`'a`, `'b`) while retaining bare-metal OS capability ("aiming for the iron").

Instead of external library templates (like C++ `std::shared_ptr` or Boost wrappers), **Smart Pointers are built directly into Frust as core language primitives**.

---

## The 5 Built-in Pointer Types

```frust
// 1. Unique (Exclusive Owned Pointer)
let buf: own AudioBuffer = own AudioBuffer::new(512)

// 2. Shared (Ref-Counted Smart Pointer)
let state: shared ApplicationState = shared ApplicationState::new()

// 3. Weak (Non-owning Observer Pointer)
let parent_link: weak Node = shared::downgrade(state)

// 4. Raw (Bare-metal Hardware & OS Pointer)
let mmio_reg: raw* u32 = 0xF000_0000 as raw* u32

// 5. Managed Ref (Auto-borrowed Reference)
fn process(data: &AudioBuffer) = { ... }
```

---

## Key Benefits for OS & Bare-Metal Development

1. **No Lifetime Annotation Noise**: No `'a`, `'b`, `&'a mut T` syntax clutter.
2. **OS Kernel Ready**: Direct `raw* T` pointer arithmetic for page tables, MMIO registers, and Interrupt Service Routines (ISRs).
3. **Automatic Cleanup**: `own T` and `shared T` handle memory deallocation deterministically without garbage collection.
