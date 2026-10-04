# NLE — Decentralized Neural-Logic Engine
## محرك الوعي والمنطق العصبي اللامركزي

**NLE** is a fully open-source, *offline* 3D reasoning environment written in pure C99. It lets anyone, anywhere,
explore and solve mathematical, physical and logical problems through an interactive visual interface —
**no servers, no internet, no central AI**. The only dependency is `libm`.

**NLE** بيئة تفاعلية ثلاثية الأبعاد مفتوحة المصدر بالكامل، مكتوبة بلغة C بدون أي مكتبات خارجية. تعتمد على الرياضيات
والمنطق البحت والشبكات العصبية الصغيرة لتمكين أي شخص في العالم من التفكير وحل المعضلات الرياضية والفيزيائية والمنطقية
عبر واجهة بصرية — **بدون خوادم، بدون إنترنت، وبدون ذكاء اصطناعي مركزي.**

## Build / البناء

```sh
make          # builds ./nle  (any C99 compiler: gcc, clang, tcc, ...)
make test     # runs 22 self-tests (SHA-256 / ChaCha20 official vectors, calculus, backprop, logic)
./nle
```

Needs a POSIX terminal with truecolor (Linux, macOS, WSL, most modern terminals). No GPU, no X11, no SDL.

## What is inside / المحتوى

| Module | Role |
|---|---|
| `src/expr.c` | Symbolic math: parser, exact differentiation, simplifier (vars `x y z t`) |
| `src/solve.c` | Newton root finding, Simpson integration, RK4 ODE integrator |
| `src/logic.c` | Propositional logic: truth tables, SAT, entailment, fallacy detection |
| `src/nn.c` | Multilayer perceptron with backpropagation (no framework) |
| `src/crypto.c` | SHA-256, HMAC, ChaCha20 and sealed **capsules** (encrypt-then-MAC) |
| `src/gfx.c` | Software 3D rasterizer: z-buffer, shading, perspective, ANSI truecolor output |
| `src/view.c` | The interactive 3D worlds |
| `src/main.c` | Command shell |

## Usage / الاستخدام

```text
eval sin(x)^2+cos(x)^2            diff x^2*sin(y)            root x^3-2*x-5 2
int sin(x) 0 pi                   logic (a->b)&(b->c) |= (a->c)
plot sin(sqrt(x^2+y^2)-t)         ode lorenz | rossler | thomas | "fx; fy; fz"
orbit 7                           brain xor | ring           predict 1 0
cube (a&b)|(c&!d)                 save my.cap secret         load my.cap secret
```

In any 3D world: **arrows / WASD** rotate, **+ / −** zoom, **space** pause, **r** auto-spin, **p** snapshot (PPM), **q** quit.

- `plot` — live surface `z = f(x,y,t)` with height-colored shading.
- `ode` — chaotic attractors from *your own equations*; two trajectories start 0.001 apart so you can watch chaos diverge.
- `orbit` — N-body gravity integrated with RK4; the status line shows the relative energy drift as a correctness check.
- `brain` — watch a neural network learn XOR or a ring-shaped region; the 3D surface *is* the network's belief, morphing live.
- `cube` — a logic formula drawn as a truth hypercube (up to 6 variables): green vertices are true, red are false.
- `logic` — decides tautology / contradiction / satisfiable, and finds counterexamples to invalid arguments.

Headless rendering (CI, servers you own, screenshots): `./nle --render out.ppm plot 'sin(x)*cos(y)'`.

## Decentralization by design / اللامركزية

State (your expression and trained network) is sealed into a **capsule**: a single file encrypted with ChaCha20 and
authenticated with HMAC-SHA256, keyed from your passphrase (40 000 hash iterations, random salt and nonce).
Its SHA-256 is its content address, so any copy, from any medium (USB, mesh network, printed hex), can be verified without a
server. There is no account, no telemetry and no network code in the whole project.

> Security note: the KDF is iterated SHA-256 to stay dependency-free; for high-value secrets use long random passphrases.

## Contributing / المساهمة

Keep it dependency-free, portable C99, and test-covered (`tests/test.c`). New worlds only need a `step` and a `draw`
callback in `src/view.c`. Ideas: SAT with DPLL for >22 variables, matrix/linear-algebra solver, PDE (heat/wave) worlds,
more capsule transports, mouse support, a Windows console backend.

## License / الرخصة

Copyright (C) 2026 NLE contributors. Licensed under the **GNU Affero General Public License v3.0 or later**
(`AGPL-3.0-or-later`) — see [LICENSE](LICENSE). Modified versions offered over a network must make their source available
to their users (AGPL §13); the `source` command exists for that purpose. **Before publishing, replace `YOUR-NAME` in
`SOURCE_URL` inside `src/main.c` with your GitHub repository.**
