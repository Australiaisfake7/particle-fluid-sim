#include <iostream>
#include <string>
#include <fstream>
#include <array>

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <glm/glm.hpp>

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

using std::string, std::array;

const unsigned int PARTICLE_COUNT_EXPONENT = 15;
// Must be power of 2
const size_t PARTICLE_COUNT = 1 << PARTICLE_COUNT_EXPONENT;
const float PHYSICS_TIMESTEP = 1.0 / 120.0;
// Simulation speed scale factor
const float SIM_RATE = 1.0f;
constexpr unsigned int GRID_WIDTH = 64;
constexpr unsigned int GRID_HEIGHT = 48;
const glm::uvec2 GRID_SIZE = {GRID_WIDTH, GRID_HEIGHT};

unsigned int currentParticleBuffer = 0;
double lastFrame = 0.0;
double dtAccumulator = 0.0;
bool hasGivenFramerateWarning = false;

glm::uvec2 screenRes = glm::uvec2(800, 600);

float physicsSmoothingRadius = 1.0f;
float visualSmoothingRadius = 0.65f;
float particleBrightness = 0.35f;
float mouseRadius = 4.5f;

bool lmbHeld = false;

double mousePosX;
double mousePosY;
float mouseDeltaX;
float mouseDeltaY;

float vertices[] = {
    -1.0f, 1.0f,
    -1.0f, -1.0f,
    1.0f, 1.0f,
    -1.0f, -1.0f,
    1.0f, 1.0f,
    1.0f, -1.0f
};

struct Particle
{
    glm::vec2 position;
    glm::vec2 velocity;
};

#pragma region Helpers

string readFile(const char* path)
{
    std::ifstream file(path);
    if (!file.is_open()) 
    {
        std::cout << "ERROR::FILE_NOT_SUCCESFULLY_READ: " << path << std::endl;
        return "";
    }

    string buffer{std::istreambuf_iterator<char>(file), {}};
    return buffer;
}

unsigned int createShader(const char* source, GLenum shaderType)
{
    unsigned int shader;
    shader = glCreateShader(shaderType);

    glShaderSource(shader, 1, &source, NULL);
    glCompileShader(shader);

    int success;
    char infoLog[512];
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success)
    {
        glGetShaderInfoLog(shader, 512, NULL, infoLog);
        std::cout << "ERROR::SHADER::COMPILATION_FAILED\n" << infoLog << std::endl;
    }

    return shader;
}

unsigned int createShaderProgram(const unsigned int* shaders, const unsigned int count)
{
    unsigned int shaderProgram;
    shaderProgram = glCreateProgram();

    for (int i = 0; i < count; i++)
    {
        glAttachShader(shaderProgram, *(shaders + i));
    }
    glLinkProgram(shaderProgram);

    int success;
    char infoLog[512];
    glGetProgramiv(shaderProgram, GL_LINK_STATUS, &success);
    if (!success) {
        glGetProgramInfoLog(shaderProgram, 512, NULL, infoLog);
        std::cout << "ERROR::SHADER::PROGRAM::LINKING_FAILED\n" << infoLog << std::endl;
    }

    return shaderProgram;
}

void deleteShaders(const unsigned int* shaders, const unsigned int count)
{
    for (int i = 0; i < count; i++)
    {
        glDeleteShader(*(shaders + i));
    }
}

#pragma endregion Helpers

void physicsUpdate(unsigned int particleBuffers[2], unsigned int particleProgram, unsigned int bitonicSortProgram, unsigned int gridCellPointerBuffer, unsigned int cellPointerProgram)
{
    glUseProgram(bitonicSortProgram);
    
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, particleBuffers[currentParticleBuffer]);

    for (int b = 2; b <= PARTICLE_COUNT; b <<= 1)
    {
        for (int s = b >> 1; s >= 1; s >>= 1)
        {
            glUniform1ui(glGetUniformLocation(bitonicSortProgram, "block"), b);
            glUniform1ui(glGetUniformLocation(bitonicSortProgram, "stride"), s);
            glDispatchCompute(((PARTICLE_COUNT / 2) + 63) / 64, 1, 1);
            glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
        }
    }

    glUseProgram(cellPointerProgram);

    glClearNamedBufferData(gridCellPointerBuffer, GL_R32UI, GL_RED_INTEGER, GL_UNSIGNED_INT, nullptr);

    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, particleBuffers[currentParticleBuffer]);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, gridCellPointerBuffer);

    glDispatchCompute((PARTICLE_COUNT + 63) / 64, 1, 1);

    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

    glUseProgram(particleProgram);

    glUniform1f(glGetUniformLocation(particleProgram, "h"), physicsSmoothingRadius / 2.0);

    float mousePosGridX = (mousePosX / screenRes.x) * GRID_WIDTH;
    float mousePosGridY = (1.0 - mousePosY / screenRes.y) * GRID_HEIGHT;
    glUniform2f(glGetUniformLocation(particleProgram, "mousePos"), mousePosGridX, mousePosGridY);

    float mouseDeltaGridX = (mouseDeltaX / screenRes.x) * GRID_WIDTH;
    float mouseDeltaGridY = -(mouseDeltaY / screenRes.y) * GRID_HEIGHT;
    glUniform2f(glGetUniformLocation(particleProgram, "mouseDelta"), mouseDeltaGridX, mouseDeltaGridY);

    glUniform1f(glGetUniformLocation(particleProgram, "mouseRadius"), mouseRadius);
    glUniform1i(glGetUniformLocation(particleProgram, "lmbHeld"), lmbHeld);

    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, particleBuffers[currentParticleBuffer]);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, particleBuffers[1 - currentParticleBuffer]);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, gridCellPointerBuffer);

    glDispatchCompute((PARTICLE_COUNT + 63) / 64, 1, 1);

    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

    currentParticleBuffer = 1 - currentParticleBuffer;
}

void ProcessInput(GLFWwindow* window)
{
    if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
        glfwSetWindowShouldClose(window, true);
}

int main()
{
    glfwInit();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    // Construct the window
    GLFWwindow* window = glfwCreateWindow(800, 600, "Fluid Simulation", nullptr, nullptr);
    if (!window)
    {
        std::cout << "Failed to create the GLFW window\n";
        glfwTerminate();
    }

    glfwMakeContextCurrent(window);

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress))
    {
        std::cout << "Failed to initialize GLAD\n";
        return -1;
    }

    // Handle view port dimensions
    glViewport(0, 0, 800, 600);
    glfwSetFramebufferSizeCallback(window, [](GLFWwindow* window, int width, int height)
    {
        glViewport(0, 0, width, height);
        screenRes = glm::uvec2(width, height);
    });

    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls
    
    // Setup Platform/Renderer backends
    ImGui_ImplGlfw_InitForOpenGL(window, true);          // Second param install_callback=true will install GLFW callbacks and chain to existing ones.
    ImGui_ImplOpenGL3_Init();

    glfwSetMouseButtonCallback(window, [](GLFWwindow* window, int button, int action, int mods)
    {
        ImGui_ImplGlfw_MouseButtonCallback(window, button, action, mods);

        if (button == GLFW_MOUSE_BUTTON_1 && action == GLFW_RELEASE)
        {
            lmbHeld = false;
            return;
        }

        ImGuiIO& io = ImGui::GetIO();
        if (io.WantCaptureMouse)
            return;

        if (button == GLFW_MOUSE_BUTTON_1 && action == GLFW_PRESS)
            lmbHeld = true;

    });

    glfwSetCursorPosCallback(window, [](GLFWwindow* window, double xpos, double ypos)
    {
        ImGui_ImplGlfw_CursorPosCallback(window, xpos, ypos);

        ImGuiIO& io = ImGui::GetIO();
        if (io.WantCaptureMouse)
            return;

        mouseDeltaX = xpos - mousePosX;
        mouseDeltaY = ypos - mousePosY; 
        mousePosX = xpos;
        mousePosY = ypos;
    });

    string vertexShaderSource = readFile("src/shaders/passthrough.vert");
    string fragmentShaderSource = readFile("src/shaders/fluid.frag");

    unsigned int vertexShader = createShader(vertexShaderSource.c_str(), GL_VERTEX_SHADER);
    unsigned int fragmentShader = createShader(fragmentShaderSource.c_str(), GL_FRAGMENT_SHADER);

    unsigned int shaders[] = {vertexShader, fragmentShader};

    unsigned int shaderProgram = createShaderProgram(shaders, sizeof(shaders) / sizeof(unsigned int));

    deleteShaders(shaders, sizeof(shaders) / sizeof(unsigned int));

    glUseProgram(shaderProgram);

    glUniform2ui(glGetUniformLocation(shaderProgram, "gridSize"), GRID_WIDTH, GRID_HEIGHT);

    string particleShaderSource = readFile("src/shaders/particles.comp");

    unsigned int particleShader = createShader(particleShaderSource.c_str(), GL_COMPUTE_SHADER);
    unsigned int particleShaderProgram = createShaderProgram(&particleShader, 1);

    deleteShaders(&particleShader, 1);

    glUseProgram(particleShaderProgram);

    glUniform2ui(glGetUniformLocation(particleShaderProgram, "gridSize"), GRID_WIDTH, GRID_HEIGHT);
    glUniform1f(glGetUniformLocation(particleShaderProgram, "dt"), PHYSICS_TIMESTEP * SIM_RATE);

    string bitonicSortShaderSource = readFile("src/shaders/bitonic_pass.comp");

    unsigned int bitonicSortShader = createShader(bitonicSortShaderSource.c_str(), GL_COMPUTE_SHADER);
    unsigned int bitonicSortProgram = createShaderProgram(&bitonicSortShader, 1);

    deleteShaders(&bitonicSortShader, 1);

    glUseProgram(bitonicSortProgram);

    glUniform2ui(glGetUniformLocation(bitonicSortProgram, "gridSize"), GRID_WIDTH, GRID_HEIGHT);

    string cellPointerShaderSource = readFile("src/shaders/grid_cell_pointers.comp");

    unsigned int cellPointerShader = createShader(cellPointerShaderSource.c_str(), GL_COMPUTE_SHADER);
    unsigned int cellPointerProgram = createShaderProgram(&cellPointerShader, 1);

    deleteShaders(&cellPointerShader, 1);

    glUseProgram(cellPointerProgram);

    glUniform2ui(glGetUniformLocation(cellPointerProgram, "gridSize"), GRID_WIDTH, GRID_HEIGHT);

    unsigned int VAO, VBO;

    glGenVertexArrays(1, &VAO);
    glBindVertexArray(VAO);

    glGenBuffers(1, &VBO);
    glBindBuffer(GL_ARRAY_BUFFER, VBO);

    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);

    unsigned int particleBuffers[2], gridCellPointerBuffer;

    glGenBuffers(2, particleBuffers);
    glGenBuffers(1, &gridCellPointerBuffer);

    array<Particle, PARTICLE_COUNT> particles;

    srand(static_cast<unsigned int>(time(nullptr)));
    for (int i = 0; i < PARTICLE_COUNT; i++)
    {
        float randomX = (rand() % 1000) / 1000.0f * GRID_WIDTH;
        float randomY = (rand() % 1000) / 1000.0f * GRID_HEIGHT;
    
        particles[i].position = glm::vec2(randomX, randomY);
        particles[i].velocity = glm::vec2(0.0f, 0.0f);
    }

    glBindBuffer(GL_SHADER_STORAGE_BUFFER, particleBuffers[0]);
    glBufferData(GL_SHADER_STORAGE_BUFFER, sizeof(Particle) * PARTICLE_COUNT, particles.data(), GL_DYNAMIC_DRAW);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, particleBuffers[1]);
    glBufferData(GL_SHADER_STORAGE_BUFFER, sizeof(Particle) * PARTICLE_COUNT, nullptr, GL_DYNAMIC_DRAW);

    glBindBuffer(GL_SHADER_STORAGE_BUFFER, gridCellPointerBuffer);
    glBufferData(GL_SHADER_STORAGE_BUFFER, sizeof(glm::uvec2) * GRID_WIDTH * GRID_HEIGHT, nullptr, GL_DYNAMIC_DRAW);

    lastFrame = glfwGetTime();

    // This is the render loop
    while (!glfwWindowShouldClose(window))
    {
        double now = glfwGetTime();
        double deltatime = now - lastFrame;
        lastFrame = now;

        dtAccumulator += deltatime * SIM_RATE;

        glfwPollEvents();
        ProcessInput(window);

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        glClearColor(1.00f, 0.49f, 0.04f, 1.00f);
        glClear(GL_COLOR_BUFFER_BIT);

        if (dtAccumulator > PHYSICS_TIMESTEP * 10)
        {
            if (hasGivenFramerateWarning)
                std::cout << "Warning: Frame rate accumulator is filling, repeated messages may be an indication that the simulation is too intensive for your computer" << std::endl;
            hasGivenFramerateWarning = true;
        }

        while (dtAccumulator >= PHYSICS_TIMESTEP)
        {
            physicsUpdate(particleBuffers, particleShaderProgram, bitonicSortProgram, gridCellPointerBuffer, cellPointerProgram);
            dtAccumulator -= PHYSICS_TIMESTEP;
        }

        mouseDeltaX = 0.0f;
        mouseDeltaY = 0.0f;

        glUseProgram(shaderProgram);

        glUniform2ui(glGetUniformLocation(shaderProgram, "screenRes"), screenRes.x, screenRes.y);
        glUniform1f(glGetUniformLocation(shaderProgram, "h"), visualSmoothingRadius / 2.0);
        glUniform1f(glGetUniformLocation(shaderProgram, "particleBrightness"), particleBrightness);

        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, particleBuffers[currentParticleBuffer]);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, gridCellPointerBuffer);

        glBindVertexArray(VAO);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        glBindVertexArray(0);

        ImGui::Begin("Simulation Settings");

        ImGui::SeparatorText("Physics");
        ImGui::SliderFloat("Physics Smoothing Radius", &physicsSmoothingRadius, 0.15f, 2.5f);
        ImGui::SliderFloat("Mouse Radius", &mouseRadius, 2.0f, 9.0f);

        ImGui::SeparatorText("Visual");
        ImGui::SliderFloat("Visual Smoothing Radius", &visualSmoothingRadius, 0.05f, 3.0f);
        ImGui::SliderFloat("Particle Brightness", &particleBrightness, 0.0f, 1.5f);
        ImGui::End();

        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(window);
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwTerminate();
    return 0;
}