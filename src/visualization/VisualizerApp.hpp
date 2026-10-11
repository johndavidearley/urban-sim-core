#pragma once
namespace visualizer {
class PlaytestBridge;
}
int runVisualizer(int argc, char *argv[],
                  visualizer::PlaytestBridge *bridge = nullptr);
