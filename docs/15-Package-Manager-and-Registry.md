# 15 - Frate Package Manager, Storage & Registry

**`frate`** (**F**unctional C**rate**) is the official package manager, build tool, dependency solver, and server registry ecosystem for **Frust** (**F**unctional **Rust**).

---

## 1. Package Outputs

* **`bin`**: Standalone AOT "Hard-Iron" executable binary (`.exe` / ELF).
* **`lib`**: Frust library module for JIT and AOT linking.
* **`bundle`**: Compressed `.frate` archive containing source code, config files (`frate.toml`), binary artifacts, and resources.

---

## 2. Manifest (`frate.toml`) & Lockfile (`frate.lock`)

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
[ frate publish ] ──> [ LagDaemon Registry API (lagdaemon.com/api/frate/) ]
                                  │
                  ┌───────────────┴───────────────┐
                  ▼                               ▼
    [ PostgreSQL Index ]                 [ Amazon S3 Storage ]
    - Metadata & SemVer                  - Stores .frate archives
    - Dependency Graph                   - Source, config & assets
```

---

## 4. CLI Commands (`frate`)

```bash
frate new my_app --bin     # Create new binary project
frate build --release      # Build Hard-Iron AOT binary
frate pack                 # Create .frate bundle
frate publish              # Publish to LagDaemon Registry (S3 + Postgres)
```
