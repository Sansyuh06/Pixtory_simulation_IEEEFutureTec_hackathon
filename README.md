# PixelFactory / Pixtory - IEEE Future Tech Explorers Challenge

> **Turning Invisible Forces Into Interactive Experience**  
> *IEEE Future Tech Explorers Challenge | Sri Sairam Institute of Technology*

---

## 🌟 Overview

**PixelFactory** is an interactive, real-time 3D physics simulation platform designed for STEM students to bridge the gap between abstract mathematical equations and intuitive physical phenomena. Rather than memorizing static formulas, learners can manipulate parameters in real time, observe instant dynamic feedback, and explore both standard syllabus equations and custom user-defined formulas.

---

## 🚀 Key Features

### 1. Native 3D Simulation Engine (`PhysicsSim3D` / `PixelFactory`)
- **Native Performance**: Built with C++17, GLFW 3.4, and OpenGL (compatible across Intel, AMD, and NVIDIA GPUs).
- **Zero-Dependency Portable Run**: Standalone executable with bundled MinGW runtime DLLs — no setup or internet required.
- **Full 3D Orbit Camera**: Mouse drag and scroll zoom to inspect physical systems from any angle.
- **Dynamic Parameter Sliders**: Change variables in real time while simulations continuously run without pausing.

### 2. Custom Equation Engine
- Type any physics formula into the prompt box (e.g., $F = m \cdot a$, $T = 2\pi\sqrt{l/g}$, $E = \frac{1}{2}mv^2$).
- Auto-detects variables and generates corresponding UI sliders with sensible bounds.
- Supports trigonometric, exponential, logarithmic, and power functions ($+, -, *, /, \wedge, \sqrt{}, \sin, \cos, \tan, \exp, \log, \text{abs}, \pi$).

### 3. Comprehensive Syllabus Coverage (25 Equations)
- **Engineering Physics (20BSPH101)**:
  - Atomic Packing Factor (FCC & BCC Structures)
  - Interplanar Spacing (Miller Indices)
  - Einstein A/B Coefficients & $\text{CO}_2$ Laser Gain
  - Numerical Aperture, Acceptance Angle & Snell's Law
  - Young's Modulus (Cantilever Method) & Torsional Pendulum
  - Compton Shift, de-Broglie Wavelength, Particle in 1D Box & Planck's Law
  - Fourier Heat Conduction
- **School STEM (Classes 11 & 12)**:
  - Newton's Second Law & Kinematics
  - Projectile Range & Simple Harmonic Motion (Spring & Pendulum)
  - Ohm's Law, Coulomb's Law, Magnetic Force on Wire & Wave Speed

### 4. Interactive Web Platform (`pixelfactory-app`)
- Modern Vite + React interface for browser-based exploration and learning modules.

---

## 📁 Repository Structure

```
├── sans/
│   ├── IEEEFutureExplorers/       # Main hackathon submission & standalone binaries
│   │   ├── PixelFactory.exe       # Primary native application
│   │   ├── file.cpp               # C++17 OpenGL simulation source code
│   │   ├── compile.bat            # Build script
│   │   └── libs/                  # GLFW headers and library dependencies
│   ├── pixelfactory-app/          # React + Vite web application
│   ├── PhysicsSim3D.exe           # Standalone physics simulator binary
│   └── *.pdf, *.png               # Documentation, design diagrams, and presentation assets
├── projec/                        # Visualizations, TouchDesigner & Rossler simulations
├── phy w/                         # Python pendulum simulation scripts
├── .gitignore                     # Git ignore rules
└── README.md                      # Project documentation
```

---

## 🎮 How to Run

### Standalone Executable (Windows x64)
1. Navigate to `sans/IEEEFutureExplorers/` (or `sans/`).
2. Ensure the required DLL files (`glfw3.dll`, `libstdc++-6.dll`, `libgcc_s_seh-1.dll`, `libwinpthread-1.dll`) remain in the same folder as `PixelFactory.exe`.
3. Double-click `PixelFactory.exe`.

### Web Application (`pixelfactory-app`)
```bash
cd sans/pixelfactory-app
npm install
npm run dev
```

---

## 👥 Team & Acknowledgements

- **Team Members**:
  - Harish Sanjay S
  - Sumitmadhavan S
  - Thuthikathipathi M
  - Jai Ganesh JC
- **Supervisor**: Ms. Aiswarya Priyadharshini S
- **Institution**: Sri Sairam Institute of Technology
