#include "src/visualization/VisualizerApp.hpp"
#include <iostream>
#include <exception>

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
    MessageBoxA(nullptr, error.what(), "UrbanSimCore Visualizer", MB_OK | MB_ICONERROR);
    return 1;
  }
}
#endif
