#pragma once

#include <chrono>
#include <fstream>
#include <stdexcept>
#include <string>

namespace visualizer {
// Opt-in, buffered present-to-present wall timings. Includes simulation and
// rendering stalls; not GPU timestamps or an estimate derived from headless ticks.
class PerformanceCapture {
public:
  using Clock = std::chrono::steady_clock;
  explicit PerformanceCapture(const std::string &path) {
    if (path.empty()) return;
    output_.open(path);
    if (!output_) throw std::runtime_error("Cannot open performance capture: " + path);
    output_ << "elapsed_ms,frame_ms,tick_ms,schedule_late_ms,tick,interval_ms,population,width,height,paused\n";
  }
  bool enabled() const { return output_.is_open(); }
  void tick(double milliseconds) { tickMs_ += milliseconds; }
  void late(double milliseconds) { lateMs_ = milliseconds; }
  void suspend() { previous_ = {}; tickMs_ = lateMs_ = 0; }
  void present(unsigned tick, unsigned interval, unsigned population,
               int width, int height, bool paused) {
    if (!enabled()) return;
    const auto now = Clock::now();
    if (previous_ != Clock::time_point{}) {
      output_ << ms(now - start_) << ',' << ms(now - previous_) << ','
              << tickMs_ << ',' << lateMs_ << ',' << tick << ',' << interval
              << ',' << population << ',' << width << ',' << height << ','
              << paused << '\n';
      if (++frames_ % 120 == 0) output_.flush();
    }
    previous_ = now;
    tickMs_ = lateMs_ = 0;
  }
  static double ms(Clock::duration duration) {
    return std::chrono::duration<double, std::milli>(duration).count();
  }
private:
  std::ofstream output_;
  Clock::time_point start_ = Clock::now(), previous_{};
  double tickMs_ = 0, lateMs_ = 0;
  unsigned frames_ = 0;
};
} // namespace visualizer
