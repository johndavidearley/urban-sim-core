#include "src/visualization/VisualizerApp.hpp"
#include <iostream>
#include <exception>
#include <string>

int main(int argc, char* argv[]) {
  try { return runVisualizer(argc, argv, nullptr); }
  catch (const std::exception& error) { std::cerr << "Visualizer: " << error.what() << '\n'; return 1; }
}

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <cstdlib>

// Retain the previous Windows GUI subsystem without SDL's main wrapper.
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
  try { return runVisualizer(__argc, __argv, nullptr); }
  catch (const std::exception& error) {
    const std::string message=std::string("UrbanSimCore could not start. Verify the installed game files and update your graphics driver. If the problem continues, include these details in your report:\n\n")+error.what();
    MessageBoxA(nullptr, message.c_str(), "UrbanSimCore", MB_OK | MB_ICONERROR);
    return 1;
  }
}
#endif
