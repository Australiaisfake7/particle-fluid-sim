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

const size_t PARTICLE_COUNT = 1000;
const float PHYSICS_TIMESTEP = 1.0 / 60.0;

unsigned int currentParticleBuffer = 0;
double lastFrame = 0.0;
double dtAccumulator = 0.0;

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

void physicsUpdate(unsigned int buffers[2], unsigned int program, array<unsigned int, 192> gridCellPointers)
{
    glUseProgram(program);

    glUniform1uiv(glGetUniformLocation(program, "gridCellPointers"), 192, gridCellPointers.data());

    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, buffers[currentParticleBuffer]);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, buffers[1 - currentParticleBuffer]);

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
    GLFWwindow* window = glfwCreateWindow(800, 600, "OpenGL Template", nullptr, nullptr);
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

    string vertexShaderSource = readFile("src/shaders/passthrough.vert");
    string fragmentShaderSource = readFile("src/shaders/fluid.frag");

    unsigned int vertexShader = createShader(vertexShaderSource.c_str(), GL_VERTEX_SHADER);
    unsigned int fragmentShader = createShader(fragmentShaderSource.c_str(), GL_FRAGMENT_SHADER);

    unsigned int shaders[] = {vertexShader, fragmentShader};

    unsigned int shaderProgram = createShaderProgram(shaders, sizeof(shaders) / sizeof(unsigned int));

    deleteShaders(shaders, sizeof(shaders) / sizeof(unsigned int));

    string particleShaderSource = readFile("src/shaders/particles.comp");

    unsigned int particleShader = createShader(particleShaderSource.c_str(), GL_COMPUTE_SHADER);
    unsigned int particleShaderProgram = createShaderProgram(&particleShader, 1);

    deleteShaders(&particleShader, 1);

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

    unsigned int particleBuffers[2];

    glGenBuffers(2, particleBuffers);

    array<Particle, PARTICLE_COUNT> particles;
    array<unsigned int, 192> gridCellPointers;

    glBindBuffer(GL_SHADER_STORAGE_BUFFER, particleBuffers[0]);
    glBufferData(GL_SHADER_STORAGE_BUFFER, sizeof(Particle) * PARTICLE_COUNT, particles.data(), GL_DYNAMIC_DRAW);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, particleBuffers[1]);
    glBufferData(GL_SHADER_STORAGE_BUFFER, sizeof(Particle) * PARTICLE_COUNT, nullptr, GL_DYNAMIC_DRAW);

    lastFrame = glfwGetTime();

    // This is the render loop
    while (!glfwWindowShouldClose(window))
    {
        double now = glfwGetTime();
        double deltatime = now - lastFrame;
        lastFrame = now;

        dtAccumulator += deltatime;

        glfwPollEvents();
        ProcessInput(window);

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        glClearColor(1.00f, 0.49f, 0.04f, 1.00f);
        glClear(GL_COLOR_BUFFER_BIT);

        while (dtAccumulator >= PHYSICS_TIMESTEP)
        {
            physicsUpdate(particleBuffers, particleShaderProgram, gridCellPointers);
            dtAccumulator -= PHYSICS_TIMESTEP;
        }

        glUseProgram(shaderProgram);

        glBindVertexArray(VAO);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        glBindVertexArray(0);

        ImGui::Begin("Test Window");
        ImGui::Text("Debug Text");
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