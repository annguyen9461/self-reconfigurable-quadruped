#ifndef QUAD_MOTOR_CONTROL_HPP_
#define QUAD_MOTOR_CONTROL_HPP_

#include <cstdio>
#include <memory>
#include <string>

#include "rclcpp/rclcpp.hpp"
#include "rcutils/cmdline_parser.h"
#include "dynamixel_sdk/dynamixel_sdk.h"
#include "quad_interfaces/msg/set_position.hpp"
#include "quad_interfaces/msg/set_config.hpp"
#include "quad_interfaces/srv/get_position.hpp"
#include "quad_interfaces/srv/get_all_positions.hpp"

#include "quad_interfaces/msg/motor_positions.hpp"
#include "quad_interfaces/msg/robot_state.hpp"  

#include "position_configs.hpp"
// #include <vector>


// Use the full namespace for RobotState
using RobotState = quad_interfaces::msg::RobotState;

enum class RobotStateEnum {
    TURNING = 0,
    HOME1 = 1,
    STOPPED_TURNING = 2,
    WALK_TO_ROLL = 3,
    AT_ROLL_STATIONARY = 4,
    ROLLING = 5,
    KNOCKED_OVER_PINS = 6,
    STOPPED_ROLLING = 7
};

class QuadMotorControl : public rclcpp::Node 
{
public:
    using SetPosition = quad_interfaces::msg::SetPosition;
    using SetConfig = quad_interfaces::msg::SetConfig;
    using GetPosition = quad_interfaces::srv::GetPosition;
    using GetAllPositions = quad_interfaces::srv::GetAllPositions;

    using MotorPositions = quad_interfaces::msg::MotorPositions;

    QuadMotorControl();
    virtual ~QuadMotorControl();

private:
    // IMU related variables
    int i2c_file;
    float accumulated_tilt_angle;
    int sample_count;
    const int window_size = 2;  // Number of samples to average
    
    // IMU functions
    void initIMU();
    int write_register(uint8_t reg, uint8_t value);
    int read_register(uint8_t reg);
    int16_t read_16bit_register(uint8_t reg_low, uint8_t reg_high);
    float getTiltAngle();

    void execute_roll_yellow();
    void execute_roll_blue();

    void gradual_transition(int* next_positions);
    void update_present_positions();

    // DYNAMIXEL SDK components
    dynamixel::PortHandler* portHandler;
    dynamixel::PacketHandler* packetHandler;
    dynamixel::GroupSyncWrite* groupSyncWrite;
    dynamixel::GroupSyncRead* groupSyncRead;

    // ROS2 Components
    rclcpp::Subscription<SetPosition>::SharedPtr set_position_subscriber_;
    rclcpp::Subscription<SetConfig>::SharedPtr set_config_subscriber_;
    rclcpp::Service<GetPosition>::SharedPtr get_position_server_;
    rclcpp::Service<GetAllPositions>::SharedPtr get_all_positions_server_;

    rclcpp::TimerBase::SharedPtr timer_;
    rclcpp::Publisher<quad_interfaces::msg::MotorPositions>::SharedPtr motor_positions_publisher_;

    // Helper functions
    void initDynamixels();
    void publishMotorPositions();
    void executeConfiguration(const SetConfig::SharedPtr msg);
    int readMotorPosition(int motor_id);

    // **Configuration Execution and Motor Control**
    void execute_config(int config_id);              // Executes a predefined configuration
    void apply_motor_positions(int* target_positions);  // Moves motors to a target position immediately
    
    int present_position;
    int last_executed_config_;
    
    RobotStateEnum curr_robot_state_;  // Track the robot's current state as an enum
    // rclcpp::Subscription<RobotState>::SharedPtr robot_state_subscriber_;  // New subscriber
    rclcpp::Subscription<quad_interfaces::msg::RobotState>::SharedPtr robot_state_subscriber_;

    int present_positions[NUM_MOTORS + 1] = {0};
};

// Function to move motors to a target position
void moveto(
    int* target_positions,
    dynamixel::GroupSyncWrite* groupSyncWrite,
    dynamixel::PacketHandler* packetHandler
);

#endif  // QUAD_MOTOR_CONTROL_HPP_