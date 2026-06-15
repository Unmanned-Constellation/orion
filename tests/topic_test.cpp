#include <stdexcept>
#include <string_view>

#include <gtest/gtest.h>

#include "orion/topic/topic.hpp"

TEST(TopicSensingTest, DetectionsBuildsCorrectPath)
{
    EXPECT_EQ(orion::topic::sensing::detections("alpha"), "orion/alpha/sensing/detections");
}

TEST(TopicSensingTest, DetectionsVehicleIdPreserved)
{
    EXPECT_EQ(orion::topic::sensing::detections("uav-01"), "orion/uav-01/sensing/detections");
}

TEST(TopicSensingTest, AllDetectionsReturnsWildcard)
{
    EXPECT_EQ(orion::topic::sensing::allDetections(), "orion/*/sensing/detections");
}

TEST(TopicClockTest, SimTimeBuildsCorrectPath)
{
    EXPECT_EQ(orion::topic::clock::simTime("alpha"), "orion/alpha/clock/sim_time");
}

TEST(TopicValidationTest, EmptyVehicleIdThrows)
{
    EXPECT_THROW(orion::topic::sensing::detections(""), std::invalid_argument);
    EXPECT_THROW(orion::topic::clock::simTime(""), std::invalid_argument);
}

TEST(TopicValidationTest, VehicleIdContainingSlashThrows)
{
    EXPECT_THROW(orion::topic::sensing::detections("alpha/beta"), std::invalid_argument);
    EXPECT_THROW(orion::topic::clock::simTime("alpha/beta"), std::invalid_argument);
}
