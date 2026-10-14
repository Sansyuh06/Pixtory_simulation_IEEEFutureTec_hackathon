PixelFactory  --  Turning Invisible Forces Into Interactive Experience
======================================================================
IEEE Future Tech Explorers Challenge  |  Sri Sairam Institute of Technology
Native OpenGL application  |  Windows x64
Built with C++ + GLFW + OpenGL  |  Cross-compiled with MinGW-w64

HOW TO RUN
----------
1. Keep ALL files in the same folder (do NOT move the .exe without the .dll files)
2. Double-click PixelFactory.exe
3. That's it -- no install, no Python, no internet required

Required files (must stay in same folder as the .exe):
  PixelFactory.exe
  glfw3.dll
  libstdc++-6.dll
  libgcc_s_seh-1.dll
  libwinpthread-1.dll

CONTROLS
--------
  Mouse drag (3D view)     Orbit the scene (all angles — up, down, left, right, any direction)
  Scroll wheel (3D)        Zoom in / out
  Click equation (sidebar) Switch simulation
  Arrow Up / Down          Cycle equations
  Drag sliders             Change variable values (animation never stops)
  Type in prompt box       Enter a custom equation
  Enter or click RUN       Simulate custom equation
  ESC                      Quit

CUSTOM EQUATION PROMPT
----------------------
Type any equation in the top-left input box and press Enter or RUN.

Examples:
  F = m * a
  E = 0.5 * m * v^2
  T = 2 * pi * sqrt(l / g)
  P = rho * g * h
  v = f * lambda
  W = F * d * cos(theta)

Supported operators and functions:
  +  -  *  /  ^  ( )
  sqrt()  sin()  cos()  tan()  exp()  log()  abs()  pi

Variables are auto-detected from the expression.
Sliders are auto-generated with sensible ranges.
The 3D visualization updates live as you drag sliders.

SYLLABUS EQUATIONS  (15 Engineering Physics + 10 Classes 11 & 12)
------------------------------------------------------------------
Engineering Physics (20BSPH101):
  CO1  APF - FCC Structure
  CO1  APF - BCC Structure
  CO1  Interplanar Spacing (Miller indices)
  CO2  Einstein A/B Coefficient
  CO2  CO2 Laser Gain
  CO3  Numerical Aperture
  CO3  Acceptance Angle
  CO3  Snell's Law
  CO4  Young's Modulus (Cantilever method)
  CO4  Torsional Pendulum Period
  CO5  Compton Shift
  CO5  de-Broglie Wavelength
  CO5  Particle in 1D Box (Energy levels)
  CO5  Planck's Law (Peak wavelength)
  CO6  Fourier Heat Conduction

Classes 11 & 12 (Ages 15-17):
  Newton's 2nd Law       F = m * a
  Kinematics             v = u + a*t
  Projectile Range       R = u^2 * sin(2*theta) / g
  SHM Period (Spring)    T = 2*pi*sqrt(m/k)
  Simple Pendulum        T = 2*pi*sqrt(l/g)
  Ohm's Law              I = V / R
  Coulomb's Law          F = k*q1*q2 / r^2
  Force on Wire          F = B * I * L
  Wave Speed             v = f * lambda
  Gravitational PE       PE = m * g * h

EDUCATIONAL FEATURES (from PixelFactory concept)
-------------------------------------------------
  - Real-time 3D simulation of every equation
  - Right panel: STEM Concept Explorer with theory, variable breakdown,
    real-world applications, and learning challenges for each equation
  - Custom equation engine: type any physics expression and explore it live
  - Inquiry-based learning: change sliders and discover how physics responds
  - Systems thinking: all variables interact simultaneously in 3D
  - Problem-solving: engineering design challenges in the info panel
  - Full orbit camera: drag mouse in any direction to see all angles

SYSTEM REQUIREMENTS
-------------------
  Windows 10 / 11  (x64)
  OpenGL 2.1 capable GPU (any Intel/AMD/NVIDIA from 2010+)
  No installation needed

TEAM
----
  Harish Sanjay S
  Sumitmadhavan S
  Thuthikathipathi M
  Jai Ganesh JC

  Supervisor: Ms Aiswarya Priyadharshini S
  Sri Sairam Institute of Technology
  20BSPH101 Engineering Physics

TECHNICAL
---------
  Language   :  C++17
  Renderer   :  OpenGL 2.1 (immediate mode, maximum compatibility)
  Window     :  GLFW 3.4
  Fonts      :  WGL bitmap fonts (Windows GDI -- zero extra dependencies)
  Cross-compiled from Linux using MinGW-w64 (GCC 13)
