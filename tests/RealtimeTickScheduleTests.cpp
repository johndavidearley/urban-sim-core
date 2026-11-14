#include "gtest/gtest.h"
#include "src/visualization/RealtimeTickSchedule.hpp"
#include <limits>

TEST(RealtimeTickSchedule, RenderLatenessDoesNotAccumulateAcrossTicks) {
  uint32_t last=0, ticks=0;
  for(uint32_t now=0;now<=120000;now+=17)
    if(visualizer::realtimeTickDue(now,700,last,false)) ++ticks;
  EXPECT_EQ(ticks,171u);
  EXPECT_EQ(last,119700u);
}

TEST(RealtimeTickSchedule, PauseDiscardsWallTimeAndCatchupIsBoundedPerFrame) {
  uint32_t last=0;
  EXPECT_FALSE(visualizer::realtimeTickDue(10000,700,last,true));
  EXPECT_FALSE(visualizer::realtimeTickDue(10001,700,last,false));
  EXPECT_TRUE(visualizer::realtimeTickDue(10700,700,last,false));
  EXPECT_TRUE(visualizer::realtimeTickDue(13000,700,last,false));
  EXPECT_EQ(last,11400u); // Exactly one tick, even with several overdue.
}

TEST(RealtimeTickSchedule, MillisecondCounterWrapPreservesCadence) {
  uint32_t last=std::numeric_limits<uint32_t>::max()-99;
  EXPECT_FALSE(visualizer::realtimeTickDue(100,700,last,false));
  EXPECT_TRUE(visualizer::realtimeTickDue(600,700,last,false));
  EXPECT_EQ(last,600u);
}
