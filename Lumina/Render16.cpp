#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <iostream>

#include "glm/glm.hpp"
#include "glm/gtc/matrix_transform.hpp"
#include "glm/gtc/type_ptr.hpp"

#include "imgui/imgui.h"
#include "imgui/imgui_impl_glfw.h"
#include "imgui/imgui_impl_opengl3.h"

// Debug Flag
#define _FRAMEBUFFER_DEBUG

#include "lazy.hpp"
#include "Camera.hpp"
#include "Shader.hpp"
#include "./Shaders/Model.hpp"
#include "./Shaders/FrameBuffer.hpp"
#include "./Lights/LightingManager.hpp"
#include "./Shaders/BloomTools.hpp"
#include "./Shaders/GBuffer.hpp"
#include "./Shaders/SSAOtools.hpp"

glm::vec3 campos(0.0, 0.0, 0.0);
glm::vec3 camup(0.0, 1.0, 0.0);

Camera camera(campos, camup);

bool enter = true;

// MultiSampling
// Disable MultiSampling for Bloom
int multisample = 1;

// Screen
int ScreenWidth = 1920;
int ScreenHeight = 1080;

void framebuffer_size_callback(GLFWwindow* window, int width, int height)
{
    ScreenWidth = width;
    ScreenHeight = height;
    glViewport(0, 0, ScreenWidth, ScreenHeight);
}

void mouse_callback(GLFWwindow* window, double xpos, double ypos)
{
    static float lastxpos = ScreenWidth / 2;
    static float lastypos = ScreenHeight / 2;
    if(enter)
    {
        lastxpos = xpos;
        lastypos = ypos;
        enter = false;
    }
    float xoffset = xpos - lastxpos;
    float yoffset = -(ypos - lastypos);
    lastxpos = xpos;
    lastypos = ypos;
    camera.Mouse(xoffset, yoffset);
}

void scroll_callback(GLFWwindow* window, double xoffset, double yoffset)
{
    camera.MouseScroll(yoffset);
}

void inputs(GLFWwindow* window)
{
    static float lastframe_time = 0;
    static float deltatime = 0;
    float currentframe_time = glfwGetTime();
    deltatime = currentframe_time - lastframe_time;
    lastframe_time = currentframe_time;

    if(glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS)
        camera.Move(FORWARD, deltatime);
    if(glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS)
        camera.Move(LEFT, deltatime);
    if(glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS)
        camera.Move(BACKWARD, deltatime);
    if(glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS)
        camera.Move(RIGHT, deltatime);

    if(glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
        glfwSetWindowShouldClose(window, true);

    if(glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS)
    {
        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
        glfwSetCursorPosCallback(window, NULL);
        enter = true;
    }
    else
    {
        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
        glfwSetCursorPosCallback(window, mouse_callback);
    }
}

int main()
{
    lazy::glfwCoreEnv(3, 3);

    // This Func Should be Called before the Window being Created
    glfwWindowHint(GLFW_SAMPLES, multisample);

    GLFWwindow *window = glfwCreateWindow(ScreenWidth, ScreenHeight, "Deferred Rendering with SSAO", NULL, NULL);
    if(window == NULL)
    {
        std::cout << "Failed to Create GLFW window" << std::endl;
        glfwTerminate();
        return -1;
    }

    glfwMakeContextCurrent(window);
    if(!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress))
    {
        std::cout << "Failed to init GLAD" << std::endl;
        return -1;
    }

    const char *glsl_version = "#version 330 core";

    // Imgui
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    (void)io;
    ImGui::StyleColorsLight();

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init(glsl_version);

    bool main_page = true;

    // CallBacks
    glViewport(0, 0, ScreenWidth, ScreenHeight);
    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
    glfwSetCursorPosCallback(window, mouse_callback);
    glfwSetScrollCallback(window, scroll_callback);

    // GL_ENABLES

    // Enable by default
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);

    // to Store DEPTH used for SSAO (USUALLY LINEARIZED AND BIGGER THAN 1.0) Blend should be OFF to Avoid Color Problems
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    GBuffer GeoPassgfb(ScreenWidth, ScreenHeight);
    // Layer 0 = World_Position(RGB)LinearizedDepth(A)
    // Layer 1 = View_Position(RGB)LinearizedDepth(A)
    // Layer 2 = World_Normal
    // Layer 3 = View_Normal
    // Layer 4 = Albedo(RGB)Specular(A)
    Shader GeoPassShader("./Shaders/GeometryPass.vert", "./Shaders/GeometryPass.frag");
    FrameBuffer LightingPassfb(ScreenWidth, ScreenHeight, 1, 2);
    // Layer 0 = all color
    // Layer 1 = bright color
    Shader LightingPassShader("./Shaders/LightingPass.vert", "./Shaders/LightingPass.frag");

    Shader SSAOPassShader("./Shaders/HDR.vert", "./Shaders/SSAO.frag");

    Shader PostEffectsShader("./Shaders/HDR.vert", "./Shaders/HDR.frag");

    // Models and Shaders
    Model Pier("./Model/Pei_Er/Pei_Er.pmx");
    Model Floor("./Model/Floor/draft_floor.fbx");

    Model Cube("./Model/JustCube/untitled.fbx");
    Shader LightCubeShader("./Shaders/LightCube.vert", "./Shaders/LightCubeBloom.frag");

    glm::mat4 model(1.0f);
    GeoPassShader.Use();
    GeoPassShader.setMat4("model", model);
    GeoPassShader.setFloat("z_near", camera.Znear);
    GeoPassShader.setFloat("z_far", camera.Zfar);

    // UniformBuffer
    unsigned int MatricesBlock;
    glGenBuffers(1, &MatricesBlock);
    glBindBuffer(GL_UNIFORM_BUFFER, MatricesBlock);
    glBufferData(GL_UNIFORM_BUFFER, 2 * sizeof(glm::mat4) + sizeof(glm::vec3), NULL, GL_STATIC_DRAW);
    glm::mat4 projection = glm::perspective(glm::radians(camera.Fov), (float)ScreenWidth / (float)ScreenWidth, camera.Znear, camera.Zfar);
    glBufferSubData(GL_UNIFORM_BUFFER, sizeof(glm::mat4), sizeof(glm::mat4), glm::value_ptr(projection));
    glBufferSubData(GL_UNIFORM_BUFFER, 2 * sizeof(glm::mat4), sizeof(glm::vec3), glm::value_ptr(camera.Position));
    glBindBuffer(GL_UNIFORM_BUFFER, 0);

    // Shader UniformBlock Bindings
    GeoPassShader.Use();
    GeoPassShader.setUniformBlock("Matrices", 0);

    LightingPassShader.Use();
    LightingPassShader.setUniformBlock("Matrices", 0);

    LightCubeShader.Use();
    LightCubeShader.setUniformBlock("Matrices", 0);

    SSAOPassShader.Use();
    SSAOPassShader.setUniformBlock("Matrices", 0);

    // UniformbLock Slot Binding
    glBindBufferBase(GL_UNIFORM_BUFFER, 0, MatricesBlock);

    // Shadow FrameBuffer
    unsigned int ShadowMapfbo;
    glGenFramebuffers(1, &ShadowMapfbo);

    // Depth Map Texture Attachment
    const unsigned int Shadow_Resolution = 4096;

    // Lighting Manager
    LightManager LM;

    // HDR
    glm::vec3 lightcol(10.0f, 10.0f, 10.0f);
    glm::vec3 lightdir(0.0f, -1.0f, -1.0f);
    LightAttrib attrib(lightcol, lightcol, lightcol);

    // DirLight Depth Map Texture
    unsigned int DirLightShadowMap;
    glGenTextures(1, &DirLightShadowMap);
    glBindTexture(GL_TEXTURE_2D, DirLightShadowMap);

    // Use the Depth Texture as a normal Texture and Sampling it
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT, Shadow_Resolution, Shadow_Resolution, 0, GL_DEPTH_COMPONENT, GL_FLOAT, NULL);
    // Remove overborder samples
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
    float bordercolor[] = {1.0f, 1.0f, 1.0f, 1.0f};
    glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, bordercolor);
    glBindTexture(GL_TEXTURE_2D, 0);

    // Binding
    glBindFramebuffer(GL_FRAMEBUFFER, ShadowMapfbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, DirLightShadowMap, 0);

    // and we don't need color attachment this time so disable the coloroutput of the framebuffer by setting its read/write buffer to NULL(GL_NONE aka 0)
    glDrawBuffer(GL_NONE);
    glReadBuffer(GL_NONE);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        std::cout << "ERROR::FRAMEBUFFER::SHADOWMAPPING:: FrameBuffer is NOT Compelete." << std::endl;
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    // Matrices for Light Space Transform <only used for DirLight>
    // All Objects should be in the Space between far_plane and near_plane <Might need Refinements Here>
    float near_plane = 30.0f;
    float far_plane = 120.0f;
    float OrthoBorder = 10.0f;
    glm::mat4 DirLight_projection = glm::ortho(-OrthoBorder, OrthoBorder, -OrthoBorder, OrthoBorder, near_plane, far_plane);
    glm::vec3 DirLight_Pos = -25.0f * lightdir + 10.0f * camup;
    glm::mat4 DirLight_view = glm::lookAt(DirLight_Pos, DirLight_Pos + lightdir, camup);
    glm::mat4 DirLight_Transform = DirLight_projection * DirLight_view;

    // Shadow Shader
    Shader DirLightShadowShader("./Shaders/SimpleDepth.vert", "./Shaders/SimpleDepth.frag");
    DirLightShadowShader.Use();
    DirLightShadowShader.setMat4("LightSpaceTransform", DirLight_Transform);
    DirLightShadowShader.setMat4("model", model);
    DirLightShadowShader.setBool("useInstance", false);

    // Static Lighting's Shadow Mapping
    glViewport(0, 0, Shadow_Resolution, Shadow_Resolution); // Shadow Map Resolution
    glBindFramebuffer(GL_FRAMEBUFFER, ShadowMapfbo);
    glClear(GL_DEPTH_BUFFER_BIT);

    // Pre-Render
    DirLightShadowShader.Use();
    Pier.Draw(&DirLightShadowShader);
    Floor.Draw(&DirLightShadowShader);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    // Lighting Management and Shadow Shader Config
    LM.dirlights.push_back(DirLight(attrib, lightdir, DirLight_Transform, DirLightShadowMap));
    LM.ShaderConfig(&LightingPassShader);

    // PointLight ShadowMap
    unsigned int CubeShadowMap;
    glGenTextures(1, &CubeShadowMap);
    glBindTexture(GL_TEXTURE_CUBE_MAP,CubeShadowMap);
    for (unsigned int i = 0; i < 6; ++i)
        glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, GL_DEPTH_COMPONENT, Shadow_Resolution, Shadow_Resolution, 0, GL_DEPTH_COMPONENT, GL_FLOAT, NULL);

    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);

    // FrameBuffer Config <For PointLight Usage>
    glBindFramebuffer(GL_FRAMEBUFFER, ShadowMapfbo);
    glFramebufferTexture(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, CubeShadowMap, 0);
    glDrawBuffer(NULL);
    glReadBuffer(NULL);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        std::cout << "ERROR::FRAMEBUFFER::SHADOWMAPPING:: FrameBuffer is NOT Compelete." << std::endl;
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    // Matrices and Shaders for CubeDepthMap Usage
    float aspect_ratio = 1.0f;
    float near = 1.0f;
    float far = 120.0f;
    glm::vec3 PointLight_Pos(0.0f, 2.0f, 2.0f);
    glm::mat4 PointLight_projection = glm::perspective(glm::radians(90.0f), aspect_ratio, near, far);
    std::vector<glm::mat4> PointLight_Transform;
    PointLight_Transform.push_back(PointLight_projection * glm::lookAt(PointLight_Pos, PointLight_Pos + glm::vec3(1.0f, 0.0f, 0.0), glm::vec3(0.0f, -1.0f, 0.0f)));
    PointLight_Transform.push_back(PointLight_projection * glm::lookAt(PointLight_Pos, PointLight_Pos + glm::vec3(-1.0f, 0.0f, 0.0), glm::vec3(0.0f, -1.0f, 0.0f)));
    PointLight_Transform.push_back(PointLight_projection * glm::lookAt(PointLight_Pos, PointLight_Pos + glm::vec3(0.0f, 1.0f, 0.0), glm::vec3(0.0f, 0.0f, 1.0f)));
    PointLight_Transform.push_back(PointLight_projection * glm::lookAt(PointLight_Pos, PointLight_Pos + glm::vec3(0.0f, -1.0f, 0.0), glm::vec3(0.0f, 0.0f, -1.0f)));
    PointLight_Transform.push_back(PointLight_projection * glm::lookAt(PointLight_Pos, PointLight_Pos + glm::vec3(0.0f, 0.0f, 1.0), glm::vec3(0.0f, -1.0f, 0.0f)));
    PointLight_Transform.push_back(PointLight_projection * glm::lookAt(PointLight_Pos, PointLight_Pos + glm::vec3(0.0f, 0.0f, -1.0), glm::vec3(0.0f, -1.0f, 0.0f)));
    
    // Cube Shadow Map Shader Config
    Shader PointLightShader("./Shaders/CubeDepth.vert", "./Shaders/CubeDepth.geom", "./Shaders/CubeDepth.frag");
    PointLightShader.Use();
    PointLightShader.setMat4("model", model);
    for (int i = 0; i < PointLight_Transform.size(); ++i)
        PointLightShader.setMat4("Shadow_Matrices[" + std::to_string(i) + "]", PointLight_Transform.at(i));
    PointLightShader.setVec3("LightPos", PointLight_Pos);
    PointLightShader.setFloat("Far", far);

    // Light Cube Shader Config
    glm::mat4 lightcubemodel(1.0f);
    lightcubemodel = glm::translate(lightcubemodel, PointLight_Pos);
    lightcubemodel = glm::scale(lightcubemodel, glm::vec3(0.05f));
    LightCubeShader.Use();
    LightCubeShader.setMat4("model", lightcubemodel);
    LightCubeShader.setVec3("light_col", lightcol);

    glViewport(0, 0, Shadow_Resolution, Shadow_Resolution);
    glBindFramebuffer(GL_FRAMEBUFFER, ShadowMapfbo);
    glClear(GL_DEPTH_BUFFER_BIT);

    // Pre-Rendering
    PointLightShader.Use();
    Pier.Draw(&PointLightShader);
    Floor.Draw(&PointLightShader);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    LM.pointlights.push_back(PointLight(attrib, PointLight_Pos, Attenuation(0.7f, 3.5f), CubeShadowMap, far));

    LM.ShaderConfig(&LightingPassShader);

    // Viewport Settings
    glViewport(0, 0, ScreenWidth, ScreenHeight);

    // SSAO Tools
    SSAOtools st(ScreenWidth, ScreenHeight, &GeoPassgfb, &SSAOPassShader);
    SSAOPassShader.Use();
    st.ShaderConfig();

    // Bloom
    Shader GaussainBlurShader("./Shaders/GaussainBlur.vert", "./Shaders/GaussainBlur.frag");
    Shader BloomMixShader("./Shaders/BloomMix.vert", "./Shaders/BloomMix.frag");
    BloomTool bt(&LightingPassfb, &GaussainBlurShader, &BloomMixShader);

    // Vars used for imgui
    bool grayscale = false;
    bool inversion = false;
    int kernel = 0;
    bool gammacorrection = true;

    float exposure = 0.4;

    bool bloom = false;
    int bloomloop = 15;
    bool SSAO = true;
    bool SSAOBlur = true;

    while(!glfwWindowShouldClose(window))
    {
        inputs(window);
        glfwPollEvents();

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        if(main_page)
        {
            ImGui::Begin("Status");

            ImGui::BulletText("Time:%.1fs", (float)glfwGetTime());
            ImGui::BulletText("FPS:%.1f", ImGui::GetIO().Framerate);

            ImGui::NewLine();
            ImGui::TextColored(ImVec4(1.0, 0.0, 0.0, 1.0), "PostEffects:");
            
            ImGui::Checkbox("Grayscale", &grayscale);
            ImGui::Checkbox("Inversion", &inversion);
            ImGui::Checkbox("Gamma Correction", &gammacorrection);
            ImGui::TextColored(ImVec4(1.0, 0.0, 0.0, 1.0), "Kernels Available:\n0::NoEffect\t1::Sharpen\t2::Blur\t3::EdgeDetection");
            ImGui::SliderInt("Kernel Selector", &kernel, 0, 3);
            ImGui::NewLine();
            ImGui::SliderFloat("Exposure", &exposure, 0.0f, 100.0f, "%.2f");

            ImGui::NewLine();
            ImGui::Checkbox("SSAO", &SSAO);
            ImGui::Checkbox("SSAOBlur", &SSAOBlur);

            ImGui::NewLine();
            ImGui::Checkbox("Bloom", &bloom);
            ImGui::TextColored(ImVec4(1.0, 0.0, 0.0, 1.0), "Blur by GaussainBlur");
            ImGui::SliderInt("Bloom Blur Factor", &bloomloop, 1, 25);

            ImGui::End();
        }

        ImGui::Render();

        // UniformBlock Data Update
        // View Matrice
        glm::mat4 view = camera.GetViewMatrix();
        glBindBuffer(GL_UNIFORM_BUFFER, MatricesBlock);
        glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(glm::mat4), glm::value_ptr(view));
        // Perspective Matrice
        projection = glm::perspective(glm::radians(camera.Fov), (float)ScreenWidth / (float)ScreenHeight, camera.Znear, camera.Zfar);
        glBufferSubData(GL_UNIFORM_BUFFER, sizeof(glm::mat4), sizeof(glm::mat4), glm::value_ptr(projection));
        // View Pos
        glBufferSubData(GL_UNIFORM_BUFFER, 2 * sizeof(glm::mat4), sizeof(glm::vec3), glm::value_ptr(camera.Position));
        glBindBuffer(GL_UNIFORM_BUFFER, 0);

        // GeometryPass
        glBindFramebuffer(GL_FRAMEBUFFER, GeoPassgfb.fb.ID);
        glEnable(GL_DEPTH_TEST);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glClearColor(0.0, 0.0, 0.0, 1.0);

        GeoPassgfb.fb.MRTRenderConfig();

        // to Store DEPTH used for SSAO (USUALLY LINEARIZED AND BIGGER THAN 1.0) Blend should be OFF to Avoid Color Problems
        glDisable(GL_BLEND);

        GeoPassShader.Use();
        Pier.Draw(&GeoPassShader);
        Floor.Draw(&GeoPassShader);

        // SSAO Pass
        SSAOPassShader.Use();
        st.Draw();

        // when Blend is on Opengl can't pass a color which has aphla that > 1.0
        glEnable(GL_BLEND);

        // LightingPass
        glBindFramebuffer(GL_FRAMEBUFFER, LightingPassfb.ID);
        glDisable(GL_DEPTH_TEST);
        glClear(GL_COLOR_BUFFER_BIT);
        glClearColor(0.0, 0.0, 0.0, 1.0);

        LightingPassfb.MRTRenderConfig();

        LightingPassShader.Use();
        GeoPassgfb.Deferred_Rendering_Config(&LightingPassShader);
        // SSAO Vars by Gui used for LightingPass
        st.LightingPass_Shader_Config(&LightingPassShader, SSAOBlur);
        LightingPassShader.setBool("ssao_compoent.apply_SSAO", SSAO);

        LightingPassfb.Draw();

        // Use Depth Data from Geometry_Pass as a Mask for Forward_Rendering after LightingPass
        glBlitNamedFramebuffer(GeoPassgfb.fb.ID, LightingPassfb.ID, 0, 0, GeoPassgfb.SCRWidth, GeoPassgfb.SCRHeight, 0, 0, LightingPassfb.ScreenWidth, LightingPassfb.ScreenHeight, GL_DEPTH_BUFFER_BIT, GL_NEAREST);
        glEnable(GL_DEPTH_TEST);

        // // Light Cube
        LightCubeShader.Use();
        Cube.Draw(&LightCubeShader);

        // Bloom
        if(bloom)
            bt.ApplyBloom(bloomloop);

        // PostEffect
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glDisable(GL_DEPTH_TEST);
        glClear(GL_COLOR_BUFFER_BIT);
        glClearColor(0.3, 0.3, 0.3, 1.0);

        // Imgui Post Effects Dynamics
        PostEffectsShader.Use();
        PostEffectsShader.setBool("Grayscale", grayscale);
        PostEffectsShader.setBool("Inversion", inversion);
        PostEffectsShader.setInt("KernelIndex", kernel);
        PostEffectsShader.setBool("GammaCorrection", gammacorrection);
        PostEffectsShader.setFloat("exposure", exposure);

        PostEffectsShader.Use();
        LightingPassfb.Draw(bloom ? bt.tex_finished() : LightingPassfb.ServeTextures().at(0));

        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window);
    }

    ImGui_ImplGlfw_Shutdown();
    ImGui_ImplOpenGL3_Shutdown();
    ImGui::DestroyContext();
    glfwTerminate();
}