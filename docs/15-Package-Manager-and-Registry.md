# 15 - Package Manager, Storage & Registry (`frustpack`)

**`frustpack`** is the official package manager, build tool, dependency solver, and server registry ecosystem for **Frust** (**F**unctional **Rust**).

---

## 1. Package Outputs

* **`bin`**: Standalone AOT "Hard-Iron" executable binary (`.exe` / ELF).
* **`lib`**: Frust library module for JIT and AOT linking.
* **`bundle`**: Compressed `.frpack` archive containing source code, config files (`frustpack.toml`), binary artifacts, and resources.

---

## 2. Manifest (`frustpack.toml`) & Lockfile (`frustpack.lock`)

```toml
[package]
name = "dsp_suite"
version = "1.2.0"
authors = ["W. Westlake <admin@lagdaemon.com>"]
type = "lib"

[dependencies]
std = "1.0.0"
math_intrinsics = "^0.4.1"

[resources]
assets = ["assets/presets/", "config/default.toml"]
```

---

## 3. Server Architecture (PostgreSQL + S3 Storage)

```
[ frustpack publish ] ──> [ LagDaemon Registry API (lagdaemon.com) ]
                                  │
                  ┌───────────────┴───────────────┐
                  ▼                               ▼
    [ PostgreSQL Index ]                 [ Amazon S3 Storage ]
    - Metadata & SemVer                  - Stores .frpack archives
    - Dependency Graph                   - Source, config & assets
```

---

## 4. CLI Commands

```bash
frustpack new my_app --bin     # Create new binary project
frustpack build --release      # Build Hard-Iron AOT binary
frustpack pack                 # Create .frpack bundle
frustpack publish              # Publish to LagDaemon Registry (S3 + Postgres)
```
