# Real-time GPU Fluid Simulation in OpenGL
[![Windows](https://img.shields.io/badge/platform-Windows-blue)](https://learn.microsoft.com/en-us/windows/)
[![OpenGL](https://img.shields.io/badge/OpenGL-4.6-green)](https://www.opengl.org/)
[![License](https://img.shields.io/badge/license-MIT-orange)](LICENSE)

## Contents

* [Highlights](#highlights)
* [Overview](#overview)
* [Implementation](#implementation)
* [Running The Project](#running-the-project)
* [Parameters](#parameters)

## Highlights

* Ships with ~32,000 particles simulated fully on the GPU at 120fps
* Mouse based fluid interaction
* Real-time tunable parameters using Dear ImGui
* Spatial grid acceleration with bitonic sort

<p>
  <img src="Screenshot_1.png" width="30%"/>
  <img src="Screenshot_2.png" width="30%"/>
</p>

## Overview

**A particle based GPU fluid simulation using OpenGL compute shaders.**  
Each particle (32,768 by default) has a position and velocity that is updated 120 times per second, nearby particles push each other away to simulate believable fluid dynamics. Instead of rendering each particle as an individual dot, the fragment shader blends together nearby particles to create a continuous gradient. This blending causes the fluid to *look* like a fluid, instead of a collection of particles.

## Implementation

### The Naive Approach

The most obvious approach to this repulsion based physics system is to have every particle loop over every other particle, and sum all their contributions to get a final force vector. However, this approach is painfully slow and runs in $O(n^2)$ time, which is far too slow for any large number of particles.  
For 32,768 particles at 120fps, the number of calculations becomes 32,768 * 32,768 * 120, which is roughly 128 **Billion** force calculations per second.  
However, one of the advantages of GPU based simulations is their parallelizability, meaning each particle's force can be calculated simultaneously, which reduces the number of sequential operations to 3.9 Million per second. But this is still far too slow.

This is the problem with the naive approach, $O(n^2)$ is simply too slow for real time.  
Fortunately, there is plenty of room for improvement.

> **Note**: Throughout this section, time complexity is given sequentially for the purpose of comparison. In reality, operations are done in parallel on the GPU, reducing all time complexities by a factor of $n$.

### Step 1: The Grid Based Approach

The core fact underpinning this optimization is that particles only act over a short distance, a particle should never be able to influence another particle on the other side of the screen. Because of this most of the naive checks will be redundant, pointlessly adding to the workload.
Instead of every particle checking every other one, the screen is split up into a grid. This simple choice allows the workload to be reduced by only checking particles in grid cells the current particle could potentially influence, which works perfectly for a fluid simulation as particles are usually spread out around evenly, meaning work is divided up evenly.

For a 96 by 72 grid, which is what this sim uses, and a smoothing radius less than 1, the workload per GPU thread is reduced by an average factor of 768. Overall, the time complexity of the search is reduced from $O(n^2)$ to $O(n)$ for a sufficiently sized grid, which is a huge difference.

> **Note**: For a fixed sized grid like in this sim, the amount of work still scales quadratically with the number of particles. However this can be mitigated by increasing the grid size proportionally, which is trivial to do.

### Step 2: Sorting particles

The grid based approach has one big problem: iterating only the particles in nearby cells requires knowing which particles are in nearby cells. You *could* iterate every particle and check which cell they are in, however this defeats the purpose of the optimization as it is intended to avoid iterating every particle. We must therefore efficiently precompute the indices of the relevant particles. The first part of this process is to sort the particles in order based on the grid cell they occupy. This seems like it could pose a problem, as when sorting sequentially the time taken can scale quickly with the number of particles. Luckily particle data is already stored on the GPU ready for physics simulation which eliminates the normal overhead of GPU sorting and allowing the particles to be sorted in parallel. I chose to use bitonic sort as it is a simple and easily parallelizable GPU sorting algorithm, which works well for my use case.

### Step 3: Getting Pointers

Now that the particles are sorted, the final step of preparation is to find the indices of where particles change grid cells and store them in a buffer, so the physics shader can look them up easily. This buffer is constructed using an additional compute shader. Each thread performs a check for a single particle, to see if it is in a different grid cell to its neighbour. If it is, the thread writes its index to the end of the previous cell and the start of the current cell in the buffer. This single pass fully populates the buffer in $O(n)$ time.

Now that the buffer has been constructed, the indices of all the particles in a grid cell can be found in constant time during the physics step.

### Step 4: The Physics Step

Now that all the prerequisite work has been done, the physics calculations can now be performed. Every GPU thread uses the buffer of grid cell pointers to get the indices of all the particles in the current cell and the neighbouring ones. Particles are stored in two separate buffers, one for reading and one for writing. These buffers are swapped physics step, preventing reading from values that were just written by a different thread. The thread then iterates over every particle and calculates their force contribution using cubic spline kernel. Once the forces have been summed, they are divided by a normalization constant, and the force is applied to the current particle's velocity, causing it to be repelled away from the nearby particles. Finally, the particle's position is updated using symplectic Euler integration to find the next position. This choice of numerical integration method allows the simulation to remain stable, unlike standard Euler integration, which adds energy to the simulation over time.

## Running The Project

> Examples require g++ with OpenGL 4.6 support  
> Please ensure g++ is on your system PATH

### VSCode

Debug and run tasks are included in project. In VSCode, open the Run & Debug panel (Ctrl+Shift+D) and select *Run OpenGL Project* from the dropdown, then press F5.

### Manual Build

```bash
g++ -O2 main.cpp src/glad.c src/imgui/*.cpp -Iinclude -Iinclude/imgui -Llib -lglfw3 -lopengl32 -lgdi32 -o main.exe
```

## Parameters

### Physics

**Physics Smoothing Radius**: The distance particles act over in grid units (grid is 96 x 72 cells).  
**Mouse Radius**: The radius of the cursor's circle of interaction in grid units.  

### Visual

**Visual Smoothing Radius**: The distance pixel values consider for calculating their value in grid units, effectively works as a blur size to visualize the fluid continuously.  
**Particle Brightness**: A scale value for the brightness of pixels.  
