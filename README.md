# Real Time GPU Fluid Simulation in OpenGL

## Highlights

* ~32,000 particles simulated fully on the GPU
* Mouse interaction
* Tunable parameters through Dear ImGui
* Spatial grid acceleration with bitonic sort

*Images*

## Overview

This fluid simulation is particle based and runs fully on the GPU using OpenGL compute shaders.\
Interactions are optimized by using a grid to categorise particles and sorting them GPU side using bitonic sort, to reduce time complexity from O($n^2$), to O($nlog^2n$) sort, and O($n$) physics.\
Particle interactions are purely repulsion based and are ***not*** physically based.

## Running project

### VSCode

Debug and run tasks are included in project, click the run button on main.cpp in VSCode and select *Run OpenGL Project*.

### Manual Build

Run the build command: "g++ -O2 main.cpp src/glad.c src/imgui/*.cpp -Iinclude -Iinclude/imgui -Llib -lglfw3 -lopengl32 -lgdi32 -o main.exe", or the equivalent command for your compiler. 

## Parameters

### Physics

*Physics Smoothing Radius*: The distance particles act over in grid units (grid is 64 x 48 cells).
*Mouse Radius*: The radius of the cursor's circle of interaction in grid units.

### Visual

*Visual Smoothing Radius*: The distance pixel values consider for calculating their value in grid units, effectively works as a blur size to vizualise the fluid continuously.
*Particle Brightness*: A scale value for the brighness of pixels.
