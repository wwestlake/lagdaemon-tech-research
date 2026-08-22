# 17 - The Automation Layer (Plugin-Facing Facility API)

## Status

Sections 4.1 (event subscription), 4.2 (manifest-based discovery,
partial - see its own note on what's still missing), and 4.3
(plugin-to-plugin service discovery) are built and verified. This
document was requirements-only when first written; it's since been
updated in place as each piece landed, per this project's practice
("nothing written is an absolute must, but we do talk about them") -
treat it as the current state, not a frozen spec.

## 1. Problem Statement

Frust is meant to be embeddable as a plugin/extension layer inside a wide
range of host applications - not just one domain. Today, a loaded plugin
has exactly one channel to reach anything outside itself: an `extern fn`
call to a symbol the host manually registered
(`frust_plugin_register_host_function`, `projects/09_frust_plugin_host`).
There is no structure to this - the host wires up whatever specific
functions it feels like, one at a time, and a plugin has to already know
the exact name and signature of anything it wants to call. Nothing is
discoverable, nothing is declared, nothing is domain-agnostic tooling -
it's raw, ad hoc FFI.

**The requirement**: a real, domain-agnostic facility layer - "the
automation layer" - that gives plugin authors higher-level tools to
build a wide range of plugins without having to define every low-level
detail themselves inside each plugin, the same way VS Code's extension
API, Eclipse/OSGi's extension-point system, and general C/C++ plugin
architectures give plugin authors structured facilities instead of raw
function-pointer soup.

This must not be scoped to any one domain (not audio, not games, not
IDE-specific) - it has to work for "a wide range of plugins."

## 2. Current State (already built and verified)

These exist today and are the foundation this layer builds on - not to
be redone:

- **Raw host interface**: `frust_plugin_register_host_function` (host
  registers a native function by name) + ordinary `extern fn` (plugin
  calls it). Ad hoc, unstructured, but real and working.
- **Formal contracts**: `interface Name { fn sig(...) -> T }` /
  `impl InterfaceName for TypeName { ... }` - genuine compile-time-
  checked dynamic dispatch (fat pointer + vtable), verified this
  session. `Automation` (`projects/06_frust_library/core/src/automation.fr`)
  is the first pilot interface built with it - a poll-based
  `tick(delta_time) -> f32` contract.
- **Native-host calling convention**: a plain C++ host can construct a
  Frust struct (via an `own`-heap-allocating constructor function, real
  as of this session - see `Codegen.h`'s `compileHeapStructLiteral`) and
  call its methods directly by mangled name
  (`frust_plugin_get_fn(handle, "RampAutomation::tick")`), with zero
  interface/vtable knowledge needed host-side.

## 3. Research Findings

Surveyed three non-audio plugin ecosystems specifically to keep this
domain-agnostic (VST/audio was deliberately excluded from the final
synthesis - a prior draft leaned on it and was explicitly rejected as
the wrong reference point for this project).

| System | Facility | What it gives a plugin author |
| :--- | :--- | :--- |
| VS Code | [Contribution points](https://code.visualstudio.com/api/references/contribution-points) | A plugin *declares* what it provides (commands, views, ...) in a structured manifest; the host discovers it generically - no hardcoded per-plugin knowledge. |
| VS Code | [Activation events](https://code.visualstudio.com/api/references/activation-events) | A plugin activates lazily, in response to a named trigger, not "always running." |
| Eclipse/OSGi | [Extension points](https://www.eclipse.org/articles/Article-Plug-in-architecture/plugin_architecture.html) | Same shape as VS Code's contribution points - a contract a plugin fulfills, registered/discovered by the host at scan/load time. |
| Eclipse/OSGi | Dynamic service registry | Beyond host↔plugin: plugins can discover and use *each other's* registered capabilities at runtime. |
| General C/C++ plugin architecture | [Event hooks via function pointers](https://coditva.github.io/blog/implementing-a-plugin-architecture-in-c/) | The host defines named events at points in its execution; plugins attach handler functions to specific events instead of being polled on a fixed schedule. |

**Synthesis**: the recurring, cross-domain facility that Frust
completely lacks today is **event subscription** - a plugin registering
"call me when X happens" instead of only ever being called on a fixed
poll (like `Automation.tick`) or via a hardcoded name the host already
knew to look up. This was confirmed directly with the user as the
priority gap over the other two (discoverable contribution points,
plugin-to-plugin service registry), which remain named below as real,
lower-priority follow-on work rather than dropped.

## 4. Requirements

### 4.1 Event subscription (priority - the confirmed gap)

A plugin must be able to register a handler function for a named event
the host fires, and have that handler actually get called when the host
fires that event - without the host needing to already know the
plugin's function names ahead of time, and without the plugin needing
to be polled.

Concretely, this needs:
- A way for a **host** to define/fire a named event with some argument
  data.
- A way for a **plugin** to register interest in a named event with a
  handler.
- A dispatch mechanism connecting the two - when the host fires event
  `X`, every plugin handler registered for `X` gets called with the
  event's data.

This is the inverse direction of `frust_plugin_register_host_function`
(host gives plugin something to call) - here, the **plugin** gives the
**host** something to call, keyed by an event name rather than a fixed
symbol name a human has to already know.

### 4.2 Discoverable contribution points

**Partially built.** `FrustPluginManifest` (`plugin.json`) already
existed (name/version/`entryPoints`, purely informational) - extended
this session with real, structured, host-consumable fields: `sourceFile`
(the actual loadable `.frust` path), `implementedInterfaces` (which
`interface`s a plugin implements, the concrete type, and its
constructor function - enough for a host to build-and-call an instance
without hardcoding the type name), `firedEvents`/`listenedEvents`
(§4.1's event names), and `requiredHostFunctions` (what the plugin
needs the host to provide, checkable *before* attempting to load).

Verified via `automation_host_example.cpp`: a host now constructs and
ticks both `RampAutomation` and `DecayAutomation` reading the
constructor/type names entirely from `automation.json` - zero hardcoded
type-name string literals in the driving code. `event_example.cpp`
verifies the event-facing fields and uses `requiredHostFunctions` to
check compatibility before loading.

**Still not done**: no reflection of an interface's own method names
(a host still has to already know `Automation` means `tick(f32) -> f32`
- only *which* interface/type/constructor is discovered, not the full
callable shape), and no host-side registry that enumerates multiple
plugins' manifests together - each manifest is read independently, one
plugin at a time.

### 4.3 Plugin-to-plugin service discovery

**Built and verified.** `frust_register_service`/`frust_lookup_service`
in `frust_plugin_host` (native registry, same architecture as §4.1's
event system: ownership tracked via the `on_init` thread-local window,
automatic purge on `frust_plugin_unload` - fixed one real bug found
while implementing the purge, where a later plugin overwriting an
earlier one's service name under the same key would have had its
registration incorrectly erased when the earlier plugin unloaded; fixed
by only erasing when the registry still holds that specific plugin's
own pointer, not by name alone). `core/services.fr` wraps it as
`register_service`/`lookup_service` for plugin authors.

Verified with two SEPARATELY loaded plugins (`service_provider.frust`/
`service_consumer.frust`) - the provider registers a "doubler" service
in its own `on_init`; the consumer, a completely different plugin/
JITDylib, looks it up by name and calls it entirely from its own Frust
code (via the existing `call_i64` indirect-call builtin) - the host
harness never calls the provider's function directly, only observes
the result. Confirmed the safety property too: after the provider is
unloaded, a lookup for "doubler" returns NULL, not a stale pointer.

## 5. Explicit Non-Goals

- Not audio/VST-specific in any way - no parameter-automation vocabulary
  (`beginEdit`/`performEdit`/normalized 0..1 ranges) belongs in this
  layer's design.
- Not scoped to any single host domain (not games, not IDEs specifically)
  - must work for "a wide range of plugins."
  - Not a replacement for `interface`/`impl X for Y` dispatch or the raw
  `extern fn` mechanism - this layer is additive, built on top of both.
- Not implemented yet - this document is requirements only, written
  before any code for this layer, per this project's standing practice
  of design-before-implementation for real language/tooling decisions.

## 6. Settled Design Constraint: Event Payloads Are Opaque

Not an open question - it falls directly out of "domain-agnostic."
Frust's dispatch mechanism cannot know or care what data a given event
carries, because it cannot know ahead of time what a plugin author will
want to send. An event is **a name plus an opaque payload** (a pointer,
same convention already used everywhere else in this codebase - `String`,
`ASTExpr`, a struct instance are all just an opaque pointer whose actual
meaning is defined by agreement between producer and consumer, never by
the mechanism moving it around). The dispatcher's only job is routing by
event name to every registered handler; it never inspects, types, or
constrains the payload itself. Specifying a fixed payload shape (typed
args, a required schema, anything) would be scoping this to whatever
domain that shape happens to fit - exactly what this layer must not do.

## 7. Settled Architecture: The C++/Frust Split

Confirmed directly by the user: this layer necessarily has two halves -
a bottom half that must speak C++ to whatever application is actually
hosting the plugin, and a top half that must be real Frust, so a plugin
author calls into it easily without hand-writing raw `extern fn`
ceremony themselves. Exactly where the line falls between the two is an
implementation call, made here:

**Bottom half - native, in `frust_plugin_host`:**
- The real event registry: a `std::string` (event name) → list of
  registered handler function pointers.
- `frust_fire_event(const char* name, void* payload)` - a C function any
  host can call to fire a named event; iterates every handler currently
  registered for that name and calls each one with the payload.
- `frust_register_event_handler(const char* name, void (*handler)(void*))`
  - the plugin-facing registration entry point.
- Handler cleanup tied to plugin unload: when a plugin's JITDylib is
  torn down (`frust_plugin_unload`), every handler it registered is
  purged from the registry automatically. This is not a separate design
  choice - a stale handler pointing into unloaded/freed JIT code would
  be a use-after-free the next time that event fired, so it has to be
  automatic given everything else already built this session about
  plugin unload invalidating previously-obtained function pointers.

**Top half - Frust, a shared library file plugin authors `use`:**
- Declares the `extern fn` bindings for `frust_register_event_handler`
  (and whatever a plugin needs to fire events itself) exactly once, so
  individual plugin authors never write raw extern declarations for
  this.
- A plugin author's own code just defines a handler function and
  registers it by name - Frust's existing function-name-decays-to-its-
  own-address behavior (already used elsewhere in this codebase to hand
  a Frust function to a C API expecting a callback) means no manual
  pointer-casting is needed at the call site:

  ```frust
  fn on_something(payload: String) -> i64 = { ... }

  fn init() = {
      register_handler("something_happened", on_something);
  }
  ```

  One `use` of the shared library, zero hand-written extern ceremony.
