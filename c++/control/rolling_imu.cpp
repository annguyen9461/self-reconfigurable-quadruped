// === Standard C/C++ Libraries ===
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <unordered_map>
#include <random>
#include <thread>
#include <chrono>

// === System Headers ===
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>

#if defined(__linux__) || defined(__APPLE__)
    #include <termios.h>
    #define STDIN_FILENO 0
#elif defined(_WIN32) || defined(_WIN64)
    #include <conio.h>
#endif

// === Linux I2C ===
#include <linux/i2c-dev.h>

// === Dynamixel SDK ===
#include "dynamixel_sdk.h"  // Uses Dynamixel SDK library

// === Macro Definitions ===

// Control table addresses
#define ADDR_PRO_TORQUE_ENABLE          64
#define ADDR_PRO_GOAL_POSITION          116
#define ADDR_PRESENT_POSITION           132

// Data Byte Length
#define LEN_PRO_GOAL_POSITION           4
#define LEN_PRESENT_POSITION            4

// Protocol version
#define PROTOCOL_VERSION                2.0

// Default settings
#define BAUDRATE                        57600
#define DEVICENAME                      "/dev/ttyUSB0"

#define TORQUE_ENABLE                   1
#define TORQUE_DISABLE                  0
#define DXL_MINIMUM_POSITION_VALUE      0
#define DXL_MAXIMUM_POSITION_VALUE      4095
#define DXL_MOVING_STATUS_THRESHOLD     20

#define ESC_ASCII_VALUE                 0x1b
#define NUM_MOTORS                      12
#define MAX_INPUT_SIZE                  100

// === Utility Functions ===

int write_register(int file, uint8_t reg, uint8_t value) {
    uint8_t buf[2] = {reg, value};
    if (write(file, buf, 2) != 2) {
        std::cerr << "Failed to write register 0x" << std::hex << (int)reg << std::endl;
        return -1;
    }
    return 0;
}

int read_register(int file, uint8_t reg) {
    uint8_t data;
    if (write(file, &reg, 1) != 1) {
        std::cerr << "Failed to write register address: 0x" << std::hex << (int)reg << std::endl;
        return -1;
    }
    if (read(file, &data, 1) != 1) {
        std::cerr << "Failed to read from register: 0x" << std::hex << (int)reg << std::endl;
        return -1;
    }
    return data;
}

int16_t read_16bit_register(int file, uint8_t reg_low, uint8_t reg_high) {
    int16_t low = read_register(file, reg_low);
    int16_t high = read_register(file, reg_high);
    if (low == -1 || high == -1) return -1;
    return (high << 8) | low;
}


int DXL_ID;
bool toggle_position = false;  // Toggles between the two positions
bool forward_running = false;

void scan_motors(dynamixel::GroupSyncRead &groupSyncRead, 
                 dynamixel::PacketHandler *packetHandler, 
                 dynamixel::PortHandler *portHandler);

void set_torque(dynamixel::PacketHandler *packetHandler, 
                dynamixel::PortHandler *portHandler, 
                const char *command, char *ids_str); 

// ALL CONFIGS
// 1 for WALKING, 2 for ROLLING
int mode = 1;
int present_positions[NUM_MOTORS + 1] = {0, 
  2045, 2053, 3049, 2054, 2035, 1014, 2044, 2047, 3071, 3051, 2043, 1056
};

int aligned_before_rolling[NUM_MOTORS + 1] = {0, 
  2045, 2053, 3049, 2054, 2035, 1014, 2044, 2047, 3071, 3051, 2043, 1056
};
int home_walking2[NUM_MOTORS + 1] = {0, 
  2879, 2032, 3049, 1440, 2066, 1014, 2835, 2009, 3071, 2429, 2078, 1056
};

///////////////////////////////// WALKING START /////////////////////////////////

// Constants
// TURNING RIGHT
// const double TICKS_PER_DEGREE = 4096.0 / 360.0;  // ≈ 11.37778
// const int UP_DOWN_TICKS = static_cast<int>(30 * TICKS_PER_DEGREE);  // 30 degrees → 341 ticks
// const int CW_CCW_TICKS = static_cast<int>(25 * TICKS_PER_DEGREE);   // 20 degrees → 227 ticks
// const int UP_DOWN_TICKS_BACKLEG = static_cast<int>(22 * TICKS_PER_DEGREE); 
// const int CW_CCW_TICKS_BACKLEG = static_cast<int>(35 * TICKS_PER_DEGREE);

// TURNING RIGHT
const double TICKS_PER_DEGREE = 4096.0 / 360.0;  // ≈ 11.37778
const int UP_DOWN_TICKS = static_cast<int>(30 * TICKS_PER_DEGREE);  // 30 degrees → 341 ticks
const int CW_CCW_TICKS = static_cast<int>(10 * TICKS_PER_DEGREE);   // 20 degrees → 227 ticks
const int UP_DOWN_TICKS_BACKLEG = static_cast<int>(22 * TICKS_PER_DEGREE); 
const int CW_CCW_TICKS_BACKLEG = static_cast<int>(20 * TICKS_PER_DEGREE);


// Home Tiptoe Positions
int home_tiptoe[NUM_MOTORS + 1] = {0, 
  2745,  // [ID:1]
  2187,  // [ID:2]
  3062,  // [ID:3]
  1343,  // [ID:4]
  1890,  // [ID:5]
  1025,  // [ID:6]
  2752,  // [ID:7]
  2190,  // [ID:8]
  3072,  // [ID:9]
  2429,  // [ID:10]
  1864,  // [ID:11]
  1050   // [ID:12]
};


// Leg 4 Movements
int leg4_up[NUM_MOTORS + 1] = {0}; 
int leg4_cw[NUM_MOTORS + 1] = {0};
int leg4_down[NUM_MOTORS + 1] = {0};

// Leg 3 Movements
int leg3_up[NUM_MOTORS + 1] = {0}; 
int leg3_ccw[NUM_MOTORS + 1] = {0};
int leg3_down[NUM_MOTORS + 1] = {0};

// Leg 1 Movements
int leg1_up[NUM_MOTORS + 1] = {0}; 
int leg1_ccw[NUM_MOTORS + 1] = {0};
int leg1_down[NUM_MOTORS + 1] = {0};

// Leg 2 Movements
int leg2_up[NUM_MOTORS + 1] = {0}; 
int leg2_ccw[NUM_MOTORS + 1] = {0};
int leg2_down[NUM_MOTORS + 1] = {0};
///////////////////////////////// WALKING END /////////////////////////////////

///////////////////////////////// ROLLING START /////////////////////////////////
const int UP_DOWN_TICKS_ROLL = static_cast<int>(50 * TICKS_PER_DEGREE); 

const int UP_TICKS_PROPEL_SMALL = static_cast<int>(30 * TICKS_PER_DEGREE);

// Circle Positions
int perfect_cir[NUM_MOTORS + 1] = {0, 
  2040,  // [ID:1]
  1098,  // [ID:2]
  3081,  // [ID:3]
  2054,  // [ID:4]
  2997,  // [ID:5]
  1007,  // [ID:6]
  2041,  // [ID:7]
  2993,  // [ID:8]
  1045,  // [ID:9]
  3054,  // [ID:10]
  1095,  // [ID:11]
  3091   // [ID:12]
};

// BLUE
// Leg 3 and 4 Movements
int blue_up_cir[NUM_MOTORS + 1] = {0}; 
int blue_down_cir[NUM_MOTORS + 1] = {0};

int blue_up_propel[NUM_MOTORS + 1] = {0}; 

// YELLOW
// Leg 1 and 2 Movements
int yellow_up_cir[NUM_MOTORS + 1] = {0}; 
int yellow_down_cir[NUM_MOTORS + 1] = {0};

int yellow_up_propel[NUM_MOTORS + 1] = {0}; 
///////////////////////////////// ROLLING END /////////////////////////////////

// Function to copy array
void copy_array(int* dest, int* src) {
    for (int i = 0; i <= NUM_MOTORS; i++) {
        dest[i] = src[i];
    }
}

// Function to populate movement arrays based on sequence
void generate_movement_arrays_walk_fw(bool turning_right) {
  // Start with home_tiptoe for all movements
  copy_array(leg4_up, home_tiptoe);
  copy_array(leg4_cw, home_tiptoe);
  copy_array(leg4_down, home_tiptoe);
  
  copy_array(leg3_up, home_tiptoe);
  copy_array(leg3_ccw, home_tiptoe);
  copy_array(leg3_down, home_tiptoe);

  copy_array(leg1_up, home_tiptoe);
  copy_array(leg1_ccw, home_tiptoe);
  copy_array(leg1_down, home_tiptoe);

  copy_array(leg2_up, home_tiptoe);
  copy_array(leg2_ccw, home_tiptoe);
  copy_array(leg2_down, home_tiptoe);
  
  if (turning_right == 1) {
    std::cout << "GENERATING FOR TURNING RIGHT\n";
    // --- Leg 4 Movements ---
    leg4_up[11] -= UP_DOWN_TICKS;      // Up (ID:11)
    copy_array(leg4_cw, leg4_up);
    leg4_cw[10] += CW_CCW_TICKS;       // Forward CW (ID:10)
    copy_array(leg4_down, leg4_cw);
    leg4_down[11] += UP_DOWN_TICKS;    // Down (ID:11)

    // --- Leg 3 Movements ---
    copy_array(leg3_up, leg4_down);
    leg3_up[8] += UP_DOWN_TICKS;       // Up (ID:8)
    copy_array(leg3_ccw, leg3_up);
    leg3_ccw[7] -= CW_CCW_TICKS;       // Forward CCW (ID:7)
    copy_array(leg3_down, leg3_ccw);
    leg3_down[8] -= UP_DOWN_TICKS;     // Down (ID:8)

    // --- Leg 1 Movements ---
    leg1_up[2] += UP_DOWN_TICKS_BACKLEG;       // Up (ID:2)
    copy_array(leg1_ccw, leg1_up);
    leg1_ccw[1] += CW_CCW_TICKS_BACKLEG;       // Forward CCW (ID:1)
    copy_array(leg1_down, leg1_ccw);
    leg1_down[2] -= UP_DOWN_TICKS_BACKLEG;     // Down (ID:2)

    // --- Leg 2 Movements ---
    copy_array(leg2_up, leg1_down);
    leg2_up[5] -= UP_DOWN_TICKS_BACKLEG;       // Up (ID:5)
    copy_array(leg2_ccw, leg2_up);
    leg2_ccw[4] -= CW_CCW_TICKS_BACKLEG;       // Forward CCW (ID:4)
    copy_array(leg2_down, leg2_ccw);
    leg2_down[5] += UP_DOWN_TICKS_BACKLEG;     // Down (ID:5)
  } 
  else if (turning_right == 0) {
    std::cout << "GENERATING FOR TURNING LEFT\n";
    // TURNING LEFT
    // --- Leg 3 Movements ---
    leg3_up[8] += UP_DOWN_TICKS;       // Up (ID:8)
    copy_array(leg3_ccw, leg3_up);
    leg3_ccw[7] -= CW_CCW_TICKS;       // Forward CCW (ID:7)
    copy_array(leg3_down, leg3_ccw);
    leg3_down[8] -= UP_DOWN_TICKS;     // Down (ID:8)

    // --- Leg 4 Movements ---
    copy_array(leg4_up, leg3_down);
    leg4_up[11] -= UP_DOWN_TICKS;      // Up (ID:11)
    copy_array(leg4_cw, leg4_up);
    leg4_cw[10] += CW_CCW_TICKS;       // Forward CW (ID:10)
    copy_array(leg4_down, leg4_cw);
    leg4_down[11] += UP_DOWN_TICKS;    // Down (ID:11)

    // --- Leg 3 Movements ---
    copy_array(leg3_up, leg4_down);
    leg3_up[8] += UP_DOWN_TICKS;       // Up (ID:8)
    copy_array(leg3_ccw, leg3_up);
    leg3_ccw[7] -= CW_CCW_TICKS;       // Forward CCW (ID:7)
    copy_array(leg3_down, leg3_ccw);
    leg3_down[8] -= UP_DOWN_TICKS;     // Down (ID:8)

     // --- Leg 2 Movements ---
    leg2_up[5] -= UP_DOWN_TICKS_BACKLEG;       // Up (ID:5)
    copy_array(leg2_ccw, leg2_up);
    leg2_ccw[4] -= CW_CCW_TICKS_BACKLEG;       // Forward CCW (ID:4)
    copy_array(leg2_down, leg2_ccw);
    leg2_down[5] += UP_DOWN_TICKS_BACKLEG;     // Down (ID:5)
  
    // --- Leg 1 Movements ---
    copy_array(leg1_up, leg2_down);
    leg1_up[2] += UP_DOWN_TICKS_BACKLEG;       // Up (ID:2)
    copy_array(leg1_ccw, leg1_up);
    leg1_ccw[1] += CW_CCW_TICKS_BACKLEG;       // Forward CCW (ID:1)
    copy_array(leg1_down, leg1_ccw);
    leg1_down[2] -= UP_DOWN_TICKS_BACKLEG;     // Down (ID:2)
  }
}

// Function to populate movement arrays based on sequence
void generate_movement_arrays_roll_fw() {
  // Start with perfect_cir for all movements
  copy_array(blue_up_cir, perfect_cir);
  copy_array(blue_up_propel, perfect_cir);

  // copy_array(blue_down_cir, perfect_cir)

  copy_array(yellow_up_cir, perfect_cir);
  copy_array(yellow_up_propel, perfect_cir);

  // copy_array(yellow_down_cir, perfect_cir);
  
  std::cout << "GENERATING FOR BLUE LEGS\n";
  // BLUE FOLDS IN REVERSE SO REVERSE INCREMENTS
  blue_up_cir[11] += UP_DOWN_TICKS_ROLL;      // Up LEG 4
  blue_up_cir[8] -= UP_DOWN_TICKS_ROLL;      // Up LEG 3

  blue_up_propel[11] += UP_TICKS_PROPEL_SMALL;      // Up LEG 4
  blue_up_propel[8] -= UP_TICKS_PROPEL_SMALL;      // Up LEG 3

  // blue_down_cir[11] += UP_DOWN_TICKS;    // Down LEG 4
  // blue_down_cir[8] += UP_DOWN_TICKS;    // Down LEG 3

  std::cout << "GENERATING FOR YELLOW LEGS\n";
  yellow_up_cir[5] -= UP_DOWN_TICKS_ROLL;      // Up LEG 2
  yellow_up_cir[2] += UP_DOWN_TICKS_ROLL;      // Up LEG 1

  yellow_up_propel[5] -= UP_TICKS_PROPEL_SMALL;      // Up LEG 4
  yellow_up_propel[2] += UP_TICKS_PROPEL_SMALL;      // Up LEG 3

  // yellow_down_cir[5] -= UP_DOWN_TICKS;    // Down LEG 2
  // yellow_down_cir[2] -= UP_DOWN_TICKS;    // Down LEG 1

}

int home_tiptoe_thin[NUM_MOTORS + 1] = {0, 
  2207, 2325, 3053, 1818, 1789, 1020, 2226, 2299, 3070, 2833, 1786, 1049
};

// WALK TO CIR
int walk_to_cir1[NUM_MOTORS + 1] = {0, 
    2045, 1637, 3059, 2052, 2435, 1017, 2045, 1983, 2726, 3051, 2085, 1396
};

// CIR TO WALK
int cir_to_blue3_180[NUM_MOTORS + 1] = {0, 
    2047, 1041, 3089, 2050, 3043, 996, 2086, 2987, 3082, 3054, 1099, 3098
};
int cir_to_both_blues_180[NUM_MOTORS + 1] = {0, 
    2047, 974, 3096, 2049, 3088, 993, 2086, 2979, 3082, 3054, 1101, 1052
};

// RECOVERY LEFT SIDE ON GROUND
int s_shape_30_out[NUM_MOTORS + 1] = {0, 
    2023, 1458, 3088, 2059, 2640, 977, 2081, 2642, 1018, 3085, 1459, 3099
};
int s_shape_full_90_out[NUM_MOTORS + 1] = {0, 
    2023, 2140, 3071, 2091, 1958, 962, 2064, 2051, 1003, 3087, 2045, 3098
};
int blue3_180[NUM_MOTORS + 1] = {0, 
    2020, 2140, 3069, 2140, 1958, 946, 1997, 2051, 3070, 3097, 2043, 3081
};
int cir_to_yellow_up60[NUM_MOTORS + 1] = {0, 
    2045, 1281, 3096, 2045, 2829, 989, 2103, 2987, 3062, 3053, 1100, 1052
};
int cir_to_yellow_up90[NUM_MOTORS + 1] = {0, 
    2046, 1631, 3097, 2042, 2485, 984, 2105, 2990, 3060, 3053, 1104, 1052
};
// RECOVERY RIGHT SIDE ON GROUND


// Up and Down movement (2, 5, 8, 11) (ROLL)
// WALKING
// #define DOWN_MOTOR2  2048  // Down (flat no under)
#define UP_MOTOR2  3074  // Up
// ROLLING
#define DOWN_MOTOR2  1020  // Down (for rolling)

// WALKING
// #define DOWN_MOTOR5  2042  // Down (flat no under)
#define UP_MOTOR5  1039  // Up
// ROLLING
#define DOWN_MOTOR5  2919  // Down (for rolling)

#define DOWN_MOTOR8  2051  // Down (flat no under)
#define UP_MOTOR8  3204  // Up

#define DOWN_MOTOR11 2044  // Down (flat no under)
#define UP_MOTOR11  957  // Up



// Yaw movement (1, 4, 7, 10) (CLOCKWISE & COUNTER-CLOCKWISE)
#define CLOCKWISE_MOTOR1       1858
#define COUNTER_CLOCKWISE_MOTOR1 3010

#define CLOCKWISE_MOTOR4       1063
#define COUNTER_CLOCKWISE_MOTOR4 2236

#define CLOCKWISE_MOTOR7       1877
#define COUNTER_CLOCKWISE_MOTOR7 3092

#define CLOCKWISE_MOTOR10      2004
#define COUNTER_CLOCKWISE_MOTOR10 3215



// Fold movement (3, 6, 9, 12) (CLOCKWISE & COUNTER-CLOCKWISE)
#define CLOCKWISE_MOTOR3       990
#define COUNTER_CLOCKWISE_MOTOR3 3072

#define CLOCKWISE_MOTOR6       1028
#define COUNTER_CLOCKWISE_MOTOR6 3080

#define CLOCKWISE_MOTOR9       3082
#define COUNTER_CLOCKWISE_MOTOR9 1024

#define CLOCKWISE_MOTOR12      3093 
#define COUNTER_CLOCKWISE_MOTOR12 1041

// WALKING
// Struct to store both motors per leg (roll and yaw)
struct LegMotors {
  int roll_motor_id;     // Motor responsible for up/down
  int yaw_motor_id;      // Motor responsible for side-to-side
  int roll_down;         // Min position for roll movement
  int roll_up;           // Max position for roll movement
  int yaw_cw;            // Min position for yaw (CLOCKWISE)
  int yaw_ccw;           // Max position for yaw (COUNTER-CLOCKWISE)
};

// Map each leg number to its corresponding motors (using defines)
std::unordered_map<int, LegMotors> leg_motor_map = {
  {1, {2, 1, DOWN_MOTOR2, UP_MOTOR2, CLOCKWISE_MOTOR1, COUNTER_CLOCKWISE_MOTOR1}},  
  {2, {5, 4, DOWN_MOTOR5, UP_MOTOR5, CLOCKWISE_MOTOR4, COUNTER_CLOCKWISE_MOTOR4}},  
  {3, {8, 7, DOWN_MOTOR8, UP_MOTOR8, CLOCKWISE_MOTOR7, COUNTER_CLOCKWISE_MOTOR7}},  
  {4, {11, 10, DOWN_MOTOR11, UP_MOTOR11, CLOCKWISE_MOTOR10, COUNTER_CLOCKWISE_MOTOR10}}
};

// ROLLING
// Struct to store motors for rolling
struct LegMotorsFold {
  int fold_motor_id;
  int fold_cw;
  int fold_ccw;
};

// Map each leg number to its corresponding motors (using defines)
std::unordered_map<int, LegMotorsFold> fold_map = {
  {1, {3, CLOCKWISE_MOTOR3, COUNTER_CLOCKWISE_MOTOR3}},  
  {2, {6, CLOCKWISE_MOTOR6, COUNTER_CLOCKWISE_MOTOR6}},  
  {3, {9, CLOCKWISE_MOTOR9, COUNTER_CLOCKWISE_MOTOR9}},  
  {4, {12, CLOCKWISE_MOTOR12, COUNTER_CLOCKWISE_MOTOR12}}
};


int degree_to_pos_diff(int degree) {
  return static_cast<int>((degree/360.0) * 4095);   // used 360.0 to prevent zero for small angles
}

// Fold the motor CW by a given degree amount
int fold_cw(int leg_num, int degree) {
  LegMotorsFold motors = fold_map[leg_num];
  int fold_cw = motors.fold_cw;     // Clockwise position
  int fold_ccw = motors.fold_ccw;   // Counter-clockwise position
  int diff = degree_to_pos_diff(degree);
  int curr_pos_motor = present_positions[motors.fold_motor_id];

  // Ensure position stays within limits when moving CLOCKWISE
  if (fold_ccw > fold_cw) {
      return std::max(curr_pos_motor - diff, fold_cw);
  } else {
      return std::min(curr_pos_motor + diff, fold_cw);
  }
}

// Fold the motor CCW by a given degree amount
int fold_ccw(int leg_num, int degree) {
  LegMotorsFold motors = fold_map[leg_num];
  int fold_cw = motors.fold_cw;     // Clockwise position
  int fold_ccw = motors.fold_ccw;   // Counter-clockwise position
  int diff = degree_to_pos_diff(degree);
  int curr_pos_motor = present_positions[motors.fold_motor_id];

  // Ensure position stays within limits when moving CLOCKWISE
  if (fold_ccw > fold_cw) {
      return std::max(curr_pos_motor + diff, fold_cw);
  } else {
      return std::min(curr_pos_motor - diff, fold_cw);
  }
}


// Moves the motor CLOCKWISE by a given degree amount (YAW motor)
int go_clockwise(int leg_num, int degree) {
    LegMotors motors = leg_motor_map[leg_num];
  int yaw_cw = motors.yaw_cw;     // Clockwise position
  int yaw_ccw = motors.yaw_ccw;   // Counter-clockwise position
  int diff = degree_to_pos_diff(degree);
  int curr_pos_motor = present_positions[motors.yaw_motor_id];

  // Print debug information
  std::cout << "Leg " << leg_num << ":\n";
  std::cout << "  Yaw Motor ID: " << motors.yaw_motor_id << "\n";
  std::cout << "  Roll Motor ID: " << motors.roll_motor_id << "\n";
  std::cout << "  Current Yaw Position: " << curr_pos_motor << "\n";
  std::cout << "  Clockwise Limit: " << yaw_cw << "\n";
  std::cout << "  Counter-Clockwise Limit: " << yaw_ccw << "\n";
  std::cout << "  Degree to Move: " << degree << "\n";
  std::cout << "  Position Difference: " << diff << "\n";

  // Ensure position stays within limits when moving CLOCKWISE
  if (yaw_ccw > yaw_cw) {
      return std::max(curr_pos_motor + diff, yaw_cw);
  } else {
      return std::min(curr_pos_motor - diff, yaw_cw);
  }
}

// Moves the motor COUNTER-CLOCKWISE by a given degree amount (YAW motor)
int go_counter_clockwise(int leg_num, int degree) {
  LegMotors motors = leg_motor_map[leg_num];
  int yaw_cw = motors.yaw_cw;
  int yaw_ccw = motors.yaw_ccw;
  int diff = degree_to_pos_diff(degree);
  int curr_pos_motor = present_positions[motors.yaw_motor_id];

  // Ensure position stays within limits when moving COUNTER-CLOCKWISE
  if (yaw_ccw > yaw_cw) {
      return std::min(curr_pos_motor - diff, yaw_ccw);
  } else {
      return std::max(curr_pos_motor + diff, yaw_ccw);
  }
}

// Moves the motor UP by a given degree amount (ROLL motor)
int go_up(int leg_num, int degree) {
  LegMotors motors = leg_motor_map[leg_num];
  int roll_up = motors.roll_up;
  int roll_down = motors.roll_down;
  int diff = degree_to_pos_diff(degree);
  int curr_pos_motor = present_positions[motors.roll_motor_id];

  // Ensure position stays within limits when moving UP
  if (roll_up > roll_down) {
      return std::min(curr_pos_motor + diff, roll_up);
  } else {
      return std::max(curr_pos_motor - diff, roll_up);
  }
}

// Moves the motor DOWN by a given degree amount (ROLL motor)
int go_down(int leg_num, int degree) {
  LegMotors motors = leg_motor_map[leg_num];
  int roll_up = motors.roll_up;
  int roll_down = motors.roll_down;
  int diff = degree_to_pos_diff(degree);
  int curr_pos_motor = present_positions[motors.roll_motor_id];

  // Ensure position stays within limits when moving UP
  if (roll_up > roll_down) {
      return std::max(curr_pos_motor - diff, roll_down);
  } else {
      return std::min(curr_pos_motor + diff, roll_down);
  }
}

int getch()
{
#if defined(__linux__) || defined(__APPLE__)
  struct termios oldt, newt;
  int ch;
  tcgetattr(STDIN_FILENO, &oldt);
  newt = oldt;
  newt.c_lflag &= ~(ICANON | ECHO);
  tcsetattr(STDIN_FILENO, TCSANOW, &newt);
  ch = getchar();
  tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
  return ch;
#elif defined(_WIN32) || defined(_WIN64)
  return _getch();
#endif
}

int kbhit(void)
{
#if defined(__linux__) || defined(__APPLE__)
  struct termios oldt, newt;
  int ch;
  int oldf;

  tcgetattr(STDIN_FILENO, &oldt);
  newt = oldt;
  newt.c_lflag &= ~(ICANON | ECHO);
  tcsetattr(STDIN_FILENO, TCSANOW, &newt);
  oldf = fcntl(STDIN_FILENO, F_GETFL, 0);
  fcntl(STDIN_FILENO, F_SETFL, oldf | O_NONBLOCK);

  ch = getchar();

  tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
  fcntl(STDIN_FILENO, F_SETFL, oldf);

  if (ch != EOF)
  {
    ungetc(ch, stdin);
    return 1;
  }

  return 0;
#elif defined(_WIN32) || defined(_WIN64)
  return _kbhit();
#endif
}

void scan_motors(dynamixel::GroupSyncRead &groupSyncRead, 
                 dynamixel::PacketHandler *packetHandler, 
                 dynamixel::PortHandler *portHandler)
{
  printf("Scanning for connected Dynamixel motors...\n");

  // Clear previous parameters
  groupSyncRead.clearParam();
  
  // Scan for active motors
  for (int id = 1; id <= NUM_MOTORS; id++) {
      int dxl_comm_result = packetHandler->ping(portHandler, id);
      if (dxl_comm_result == COMM_SUCCESS) {
          printf("Found Dynamixel ID: %d\n", id);

          // Add ID to GroupSyncRead
          bool dxl_addparam_result = groupSyncRead.addParam(id);
          if (!dxl_addparam_result) {
              printf("Failed to add ID %d to SyncRead\n", id);
          }
      }
  }

  // Read all present positions
  int dxl_comm_result = groupSyncRead.txRxPacket();
  if (dxl_comm_result != COMM_SUCCESS) {
      printf("Failed to read positions: %s\n", packetHandler->getTxRxResult(dxl_comm_result));
  }

  // Print present positions of connected motors
  printf("\nCurrent Positions:\n");
  for (int id = 1; id <= NUM_MOTORS; id++) {
      if (groupSyncRead.isAvailable(id, ADDR_PRESENT_POSITION, LEN_PRESENT_POSITION)) {
          int32_t position = groupSyncRead.getData(id, ADDR_PRESENT_POSITION, LEN_PRESENT_POSITION);
          printf("[ID:%d] Position: %d\n", id, position);
      }
  }
  printf("\n");
}

void print_present(dynamixel::GroupSyncRead &groupSyncRead, 
                 dynamixel::PacketHandler *packetHandler, 
                 dynamixel::PortHandler *portHandler)
{
    // Read all present positions
    int dxl_comm_result = groupSyncRead.txRxPacket();
    if (dxl_comm_result != COMM_SUCCESS) {
        printf("Failed to read positions: %s\n", packetHandler->getTxRxResult(dxl_comm_result));
    }

    // Print present positions of connected motors
    printf("\nCurrent Positions:\n");
    for (int id = 1; id <= NUM_MOTORS; id++) {
        if (groupSyncRead.isAvailable(id, ADDR_PRESENT_POSITION, LEN_PRESENT_POSITION)) {
            int32_t position = groupSyncRead.getData(id, ADDR_PRESENT_POSITION, LEN_PRESENT_POSITION);
            printf("[ID:%d] Position: %d\n", id, position);
        }
    }
    printf("\n");
}

void update_present_positions(dynamixel::GroupSyncRead &groupSyncRead, 
                 dynamixel::PacketHandler *packetHandler, 
                 dynamixel::PortHandler *portHandler)
{
  // Clear previous parameters
  groupSyncRead.clearParam();

  // Scan for active motors
  for (int id = 1; id <= NUM_MOTORS; id++) {
      int dxl_comm_result = packetHandler->ping(portHandler, id);
      if (dxl_comm_result == COMM_SUCCESS) {
          // Add ID to GroupSyncRead
          bool dxl_addparam_result = groupSyncRead.addParam(id);
      }
  }

  // Read all present positions
  int dxl_comm_result = groupSyncRead.txRxPacket();

  // Update present positions of connected motors
  for (int id = 1; id <= NUM_MOTORS; id++) {
      if (groupSyncRead.isAvailable(id, ADDR_PRESENT_POSITION, LEN_PRESENT_POSITION)) {
          int32_t position = groupSyncRead.getData(id, ADDR_PRESENT_POSITION, LEN_PRESENT_POSITION);
          present_positions[id] = position;
      }
  }
  // printf("Present motor positions UPDATED\n");
  // for (int id = 1; id <= NUM_MOTORS; id++) {
  //   printf("[ID: %d] Position: %d\n", id, present_positions[id]);
  // }
}

void update_one_motor_pos(dynamixel::GroupSyncRead &groupSyncRead, 
  dynamixel::PacketHandler *packetHandler, 
  dynamixel::PortHandler *portHandler, 
  int motor_id) // Only update this motor
{
  // Clear previous parameters
  groupSyncRead.clearParam();

  // Add only the specified motor for reading
  bool dxl_addparam_result = groupSyncRead.addParam(motor_id);
  if (!dxl_addparam_result) {
  printf("Failed to add ID %d to SyncRead\n", motor_id);
  return;
  }

  // Read position for the selected motor
  int dxl_comm_result = groupSyncRead.txRxPacket();
  if (dxl_comm_result != COMM_SUCCESS) {
  printf("Failed to read position for ID %d: %s\n", motor_id, packetHandler->getTxRxResult(dxl_comm_result));
  return;
  }

  // Update only the specified motor
  if (groupSyncRead.isAvailable(motor_id, ADDR_PRESENT_POSITION, LEN_PRESENT_POSITION)) {
  int32_t position = groupSyncRead.getData(motor_id, ADDR_PRESENT_POSITION, LEN_PRESENT_POSITION);
  present_positions[motor_id] = position;
  printf("[ID:%d] Updated Position: %d\n", motor_id, position);
  }
}

void set_torque(dynamixel::PacketHandler *packetHandler, 
                dynamixel::PortHandler *portHandler, 
                const char *command, char *ids_str)
{
  bool enable = (strcmp(command, "en") == 0) ? TORQUE_ENABLE : TORQUE_DISABLE;
  uint8_t dxl_error = 0;

  char *token = strtok(ids_str, " ");
  while (token != NULL) {
      int dxl_id = atoi(token);
      if (dxl_id < 1 || dxl_id > NUM_MOTORS) {
          printf("Invalid ID: %d. Must be between 1 and %d.\n", dxl_id, NUM_MOTORS);
      } else {
          int dxl_comm_result = packetHandler->write1ByteTxRx(portHandler, dxl_id, ADDR_PRO_TORQUE_ENABLE, enable, &dxl_error);
          if (dxl_comm_result != COMM_SUCCESS) {
              printf("[ID:%d] Torque change failed: %s\n", dxl_id, packetHandler->getTxRxResult(dxl_comm_result));
          } else {
              printf("[ID:%d] Torque %s\n", dxl_id, enable ? "ENABLED" : "DISABLED");
          }
      }
      token = strtok(NULL, " ");
  }
}

void move_to(
          int* positions,
          dynamixel::GroupSyncWrite &groupSyncWrite, 
          dynamixel::PacketHandler *packetHandler,
          dynamixel::GroupSyncRead &groupSyncRead,
          dynamixel::PortHandler *portHandler) 
{
  // Clear previous SyncWrite parameters
  groupSyncWrite.clearParam();

  for (int id = 1; id <= 12; id++)  // Loop through motor IDs 1-12
  {
      uint8_t param_goal_position[4];
      int goal_position = positions[id];

      param_goal_position[0] = DXL_LOBYTE(DXL_LOWORD(goal_position));
      param_goal_position[1] = DXL_HIBYTE(DXL_LOWORD(goal_position));
      param_goal_position[2] = DXL_LOBYTE(DXL_HIWORD(goal_position));
      param_goal_position[3] = DXL_HIBYTE(DXL_HIWORD(goal_position));

      // Add goal position to SyncWrite buffer
      if (!groupSyncWrite.addParam(id, param_goal_position)) {
          fprintf(stderr, "[ID:%03d] groupSyncWrite addParam failed\n", id);
          continue;
      }
  }
  // Transmit the home positions to all motors at once
  int dxl_comm_result = groupSyncWrite.txPacket();
  if (dxl_comm_result != COMM_SUCCESS) {
      printf("%s\n", packetHandler->getTxRxResult(dxl_comm_result));
  }

  // Clear SyncWrite buffer after sending data
  groupSyncWrite.clearParam();

  std::this_thread::sleep_for(std::chrono::milliseconds(10));  // Allow TIME for motors to reach the position
  update_present_positions(groupSyncRead, packetHandler, portHandler);

  // Print the updated positions for debugging
  // std::cout << "Updated Present Positions:\n";
  // for (int id = 1; id <= NUM_MOTORS; id++) {
  //     std::cout << "[ID:" << id << "] Position: " << present_positions[id] << "\n";
  // }
  // std::cout << "\n";
}

void move_to_target_positions(
                      int* target_positions,
                      dynamixel::GroupSyncWrite &groupSyncWrite, 
                      dynamixel::PacketHandler *packetHandler 
                      )
{
  // Clear previous SyncWrite parameters
  groupSyncWrite.clearParam();

  for (int id = 1; id <= 12; id++)  // Loop through motor IDs 1-12
  {
      uint8_t param_goal_position[4];
      int goal_position = target_positions[id];

      param_goal_position[0] = DXL_LOBYTE(DXL_LOWORD(goal_position));
      param_goal_position[1] = DXL_HIBYTE(DXL_LOWORD(goal_position));
      param_goal_position[2] = DXL_LOBYTE(DXL_HIWORD(goal_position));
      param_goal_position[3] = DXL_HIBYTE(DXL_HIWORD(goal_position));

      // Add goal position to SyncWrite buffer
      if (!groupSyncWrite.addParam(id, param_goal_position)) {
          fprintf(stderr, "[ID:%03d] groupSyncWrite addParam failed\n", id);
          continue;
      }
  }

  // Transmit the target positions to all motors at once
  int dxl_comm_result = groupSyncWrite.txPacket();
  if (dxl_comm_result != COMM_SUCCESS) {
      printf("%s\n", packetHandler->getTxRxResult(dxl_comm_result));
  }

  // Clear SyncWrite buffer after sending data
  groupSyncWrite.clearParam();
}

void gradual_transition(int* next_positions, 
                         dynamixel::GroupSyncWrite &groupSyncWrite, 
                         dynamixel::PacketHandler *packetHandler,
                         dynamixel::GroupSyncRead &groupSyncRead,  // Added parameter
                         dynamixel::PortHandler *portHandler) {  // Added parameter
    const int step_size = 17;
    float step_arr[NUM_MOTORS + 1] = {0};
    int num_motors = NUM_MOTORS;

    int updated_positions[NUM_MOTORS + 1];

    // Ensure present_positions is updated before starting the transition
    update_present_positions(groupSyncRead, packetHandler, portHandler);
    
    std::copy(std::begin(present_positions), std::end(present_positions), std::begin(updated_positions));

    for (int i = 1; i <= num_motors; i++) {
        step_arr[i] = static_cast<float>(next_positions[i] - updated_positions[i]) / step_size;
    }

    for (int step = 0; step < step_size; step++) {
        for (int i = 1; i <= num_motors; i++) {
            updated_positions[i] += std::round(step_arr[i]);  // Fix rounding issue
        }
        move_to_target_positions(updated_positions, groupSyncWrite, packetHandler);
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    move_to_target_positions(next_positions, groupSyncWrite, packetHandler);
}

std::string get_random_command() {
    static std::random_device rd;   // Non-deterministic random seed
    static std::mt19937 gen(rd());  // Mersenne Twister RNG
    static std::uniform_int_distribution<int> dist(0, 1); // Randomly pick 0 or 1

    return dist(gen) ? "rfy" : "rfb"; // Return "rfy" if 1, "rfb" if 0
}

int main() 
{
  int perfect_cir[NUM_MOTORS + 1] = {0, 
    2039, 1113, 3080, 2053, 2980, 1006, 2086, 2983, 1045, 3054, 1112, 3094
  };
  generate_movement_arrays_roll_fw();

  // Initialize PortHandler instance
  // Set the port path
  // Get methods and members of PortHandlerLinux or PortHandlerWindows
  dynamixel::PortHandler *portHandler = dynamixel::PortHandler::getPortHandler(DEVICENAME);

  // Initialize PacketHandler instance
  // Set the protocol version
  // Get methods and members of Protocol1PacketHandler or Protocol2PacketHandler
  dynamixel::PacketHandler *packetHandler = dynamixel::PacketHandler::getPacketHandler(PROTOCOL_VERSION);

  // Initialize GroupSyncWrite instance
  dynamixel::GroupSyncWrite groupSyncWrite(portHandler, packetHandler, ADDR_PRO_GOAL_POSITION, LEN_PRO_GOAL_POSITION);

  // Initialize Groupsyncread instance for Present Position
  dynamixel::GroupSyncRead groupSyncRead(portHandler, packetHandler, ADDR_PRESENT_POSITION, LEN_PRESENT_POSITION);

  int dxl_comm_result = COMM_TX_FAIL;               // Communication result
  bool dxl_addparam_result = false;                 // addParam result

  uint8_t dxl_error = 0;                            // Dynamixel error

  // Open port
  if (portHandler->openPort())
  {
    printf("Succeeded to open the port!\n");
  }
  else
  {
    printf("Failed to open the port!\n");
    printf("Press any key to terminate...\n");
    getch();
    return 0;
  }

  // Set port baudrate
  if (portHandler->setBaudRate(BAUDRATE))
  {
    printf("Succeeded to change the baudrate!\n");
  }
  else
  {
    printf("Failed to change the baudrate!\n");
    printf("Press any key to terminate...\n");
    getch();
    return 0;
  }


  for (int i = 0; i < 20; i++) {
    DXL_ID = i;
    // Enable Dynamixel Torque
    dxl_comm_result = packetHandler->write1ByteTxRx(portHandler, DXL_ID, ADDR_PRO_TORQUE_ENABLE, TORQUE_ENABLE, &dxl_error);
    if (dxl_comm_result != COMM_SUCCESS)
    {
      printf("%s\n", packetHandler->getTxRxResult(dxl_comm_result));
    }
    else if (dxl_error != 0)
    {
      printf("%s\n", packetHandler->getRxPacketError(dxl_error));
    }
    else
    {
      printf("Dynamixel#%d has been successfully connected \n", DXL_ID);
    }

    // Add parameter storage for Dynamixel#1 present position value
    dxl_addparam_result = groupSyncRead.addParam(DXL_ID);
    if (dxl_addparam_result != true)
    {
      fprintf(stderr, "[ID:%03d] groupSyncRead addparam failed", DXL_ID);
      return 0;
    }
  }


    std::string input;

    int file;
    int adapter_nr = 1; // use /dev/i2c-1
    char filename[20];
    snprintf(filename, 19, "/dev/i2c-%d", adapter_nr);
    file = open(filename, O_RDWR);
    if (file < 0) {
        std::cerr << "Failed to open I2C bus\n";
        return 1;
    }

    int addr = 0x6A; // LSM330DHCX I2C address
    if (ioctl(file, I2C_SLAVE, addr) < 0) {
        std::cerr << "Failed to set I2C address\n";
        close(file);
        return 1;
    }

    // Enable gyroscope and accelerometer
    write_register(file, 0x10, 0x60); // Accelerometer
    write_register(file, 0x11, 0x60); // Gyroscope
    sleep(1); // Wait for sensor to initialize

    // Bias offsets
    float accel_z_offset = 0.2;
    float gyro_z_offset = -0.37;

    //  // Open CSV file for writing
    // std::ofstream csvFile("imu_data_no_gaining_momentum.csv");
    // if (!csvFile.is_open()) {
    //     std::cerr << "Failed to open CSV file\n";
    //     close(file);
    //     return 1;
    // }

    // // Write CSV headers
    // csvFile << "Timestamp,Gyro_X_dps,Gyro_Y_dps,Gyro_Z_dps,Accel_X_mps2,Accel_Y_mps2,Accel_Z_mps2\n";


    auto start_time = std::chrono::high_resolution_clock::now();

    // Thresholds based on data analysis
    const float accel_z_threshold = 9.5;    // m/s² for a successful propel
    const float gyro_y_stability = 6.5;     // rad/s threshold for orientation stability
    const int window_size = 2;            // Number of samples to average (for smoothing)
    
    // Variables for data accumulation
    float accumulated_tilt_angle = 0;

    // float accumulated_accel_z = 0;
    // float accumulated_gyro_y = 0;

    int sample_count = 0;
    
  while (true) {
    std::cout << "ENTERING while loop\n";

    std::string command = "rpy";

    // Read gyroscope data
    int16_t gyro_x = read_16bit_register(file, 0x22, 0x23);
    int16_t gyro_y = read_16bit_register(file, 0x24, 0x25);
    int16_t gyro_z = read_16bit_register(file, 0x26, 0x27);

    // Read accelerometer data
    int16_t accel_x = read_16bit_register(file, 0x28, 0x29);
    int16_t accel_y = read_16bit_register(file, 0x2A, 0x2B);
    int16_t accel_z = read_16bit_register(file, 0x2C, 0x2D);


    // Apply scale factors
    float gyro_dps_x = gyro_x * (250.0 / 32768.0);
    float gyro_dps_y = gyro_y * (250.0 / 32768.0);
    float gyro_dps_z = (gyro_z * (250.0 / 32768.0)) - gyro_z_offset;

    float accel_mps2_x = accel_x * (2.0 / 32768.0) * 9.81;
    float accel_mps2_y = accel_y * (2.0 / 32768.0) * 9.81;
    float accel_mps2_z = ((accel_z * (2.0 / 32768.0)) * 9.81) - accel_z_offset;

    // Compute tilt angle around x-axis
    float angle_rad = std::atan2(accel_mps2_y, accel_mps2_z);
    float angle_degrees = angle_rad * (180.0 / M_PI);

    // Timestamp for each reading
    auto current_time = std::chrono::high_resolution_clock::now();
    double timestamp = std::chrono::duration<double>(current_time - start_time).count();

    // std::cout << "Gyro Raw - X: " << gyro_x << " Y: " << gyro_y << " Z: " << gyro_z << " | "
    // << "Accel Raw - X: " << accel_x << " Y: " << accel_y << " Z: " << accel_z << std::endl;
    
    // Write to CSV
    // csvFile << std::fixed << std::setprecision(6)
    //         << timestamp << ","
    //         << gyro_dps_x << "," << gyro_dps_y << "," << gyro_dps_z << ","
    //         << accel_mps2_x << "," << accel_mps2_y << "," << accel_mps2_z << "\n";

    // csvFile.flush(); // Ensure data is written in real-time


    // Accumulate data for smoothing
    accumulated_tilt_angle += angle_degrees;

    sample_count++;
    // std::cout << "Sample Count: " << sample_count << std::endl;

    // Check if enough samples have been collected
    if (sample_count >= window_size) {
        std::cout << "COLLECTED enough samples while loop\n";
        
        float avg_tilt_angle = accumulated_tilt_angle / sample_count;
        std::cout << "Average Tilt Angle: " << avg_tilt_angle << " degrees" << std::endl;
         // Use tilt angle to determine which side is under
        bool blue_under = (avg_tilt_angle >= -180 && avg_tilt_angle <= -122) ||
        (avg_tilt_angle >= 123 && avg_tilt_angle <= 180);     // Positive rotation → blue under
        bool yellow_under = avg_tilt_angle >= -54 && avg_tilt_angle <= 58;  // Negative rotation → yellow under

        // Check propulsion conditions
        if (yellow_under) {
          std::cout << "Yellow under – pushing yellow\n";
          move_to(yellow_up_propel, groupSyncWrite, packetHandler, groupSyncRead, portHandler);
          std::this_thread::sleep_for(std::chrono::milliseconds(700));
          move_to(perfect_cir, groupSyncWrite, packetHandler, groupSyncRead, portHandler);
        } else if (blue_under) {
          std::cout << "Blue under – pushing blue\n";
          move_to(blue_up_propel, groupSyncWrite, packetHandler, groupSyncRead, portHandler);
          std::this_thread::sleep_for(std::chrono::milliseconds(700));
          move_to(perfect_cir, groupSyncWrite, packetHandler, groupSyncRead, portHandler);
        } else {
          std::cout << "Unknown orientation. Executing random roll...\n";
          std::string random_cmd = get_random_command();
          if (random_cmd == "rfy") {
              move_to(yellow_up_propel, groupSyncWrite, packetHandler, groupSyncRead, portHandler);
          } else {
              move_to(blue_up_propel, groupSyncWrite, packetHandler, groupSyncRead, portHandler);
          }
          std::this_thread::sleep_for(std::chrono::milliseconds(700));
          move_to(perfect_cir, groupSyncWrite, packetHandler, groupSyncRead, portHandler);
      }



        // Reset accumulators
        // after the window size is met, instead of prematurely which is immediately after the decision
        accumulated_tilt_angle = 0;
        sample_count = 0;
    }

    if (command == "get") {
      scan_motors(groupSyncRead, packetHandler, portHandler);
    }

    else if (command == "h1") {
      move_to(home_tiptoe, groupSyncWrite, packetHandler,groupSyncRead, portHandler);
    }

    
    else if (command == "cirh") {
      move_to(perfect_cir, groupSyncWrite, packetHandler,groupSyncRead, portHandler);
      std::this_thread::sleep_for(std::chrono::milliseconds(700));  // Allow TIME for motors to reach the position
      move_to(cir_to_blue3_180, groupSyncWrite, packetHandler,groupSyncRead, portHandler);
      std::this_thread::sleep_for(std::chrono::milliseconds(1000));  // Allow TIME for motors to reach the position
      move_to(cir_to_both_blues_180, groupSyncWrite, packetHandler,groupSyncRead, portHandler);
      std::this_thread::sleep_for(std::chrono::milliseconds(1000));  // Allow TIME for motors to reach the position
      move_to(cir_to_yellow_up60, groupSyncWrite, packetHandler,groupSyncRead, portHandler);
      std::this_thread::sleep_for(std::chrono::milliseconds(1000));  // Allow TIME for motors to reach the position
      move_to(cir_to_yellow_up90, groupSyncWrite, packetHandler,groupSyncRead, portHandler);
      std::this_thread::sleep_for(std::chrono::milliseconds(1000));  // Allow TIME for motors to reach the position
      move_to(aligned_before_rolling, groupSyncWrite, packetHandler,groupSyncRead, portHandler);
      std::this_thread::sleep_for(std::chrono::milliseconds(1000));  // Allow TIME for motors to reach the position
      move_to(home_tiptoe_thin, groupSyncWrite, packetHandler,groupSyncRead, portHandler);
      std::this_thread::sleep_for(std::chrono::milliseconds(1000));  // Allow TIME for motors to reach the position
      move_to(home_tiptoe, groupSyncWrite, packetHandler,groupSyncRead, portHandler);

    }
    else if (command == "hcir") {
      move_to(aligned_before_rolling, groupSyncWrite, packetHandler,groupSyncRead, portHandler);
      std::this_thread::sleep_for(std::chrono::milliseconds(500));
      move_to(walk_to_cir1, groupSyncWrite, packetHandler,groupSyncRead, portHandler);
      std::this_thread::sleep_for(std::chrono::milliseconds(300));
      move_to(perfect_cir, groupSyncWrite, packetHandler,groupSyncRead, portHandler);
    }

    else if (command == "cir") {
      move_to(perfect_cir, groupSyncWrite, packetHandler,groupSyncRead, portHandler);
    }

    // recover from LEFT side on the ground
    else if (command == "recol") {
      move_to(perfect_cir, groupSyncWrite, packetHandler,groupSyncRead, portHandler);
      move_to(s_shape_30_out, groupSyncWrite, packetHandler,groupSyncRead, portHandler);
      move_to(s_shape_full_90_out, groupSyncWrite, packetHandler,groupSyncRead, portHandler);
      move_to(blue3_180, groupSyncWrite, packetHandler,groupSyncRead, portHandler);
    }
   
    
    // ROLL FW
    else if (command == "rfw") {  
      move_to(perfect_cir, groupSyncWrite, packetHandler,groupSyncRead, portHandler); 

      

      const int NUM_MOVEMENTS = 4;
      int* roll_fw_movements[NUM_MOVEMENTS] = {
        yellow_up_cir,
        perfect_cir,
        blue_up_cir,
        perfect_cir
      };
      
      while (1) {
        for (int i = 0; i < NUM_MOVEMENTS; i++) {
          move_to(roll_fw_movements[i], groupSyncWrite, packetHandler, groupSyncRead, portHandler);
          std::this_thread::sleep_for(std::chrono::milliseconds(300));  
        }
      }
    }

    // ROLL FW
    else if (command == "rfy") {
      move_to(perfect_cir, groupSyncWrite, packetHandler,groupSyncRead, portHandler); 


      const int NUM_MOVEMENTS = 2;
      int* roll_fw_movements[NUM_MOVEMENTS] = {
        yellow_up_cir,
        perfect_cir
      };

      int sleep_amount[NUM_MOVEMENTS] = {
        300,
        100
      };
      
      
      for (int i = 0; i < NUM_MOVEMENTS; i++) {
        move_to(roll_fw_movements[i], groupSyncWrite, packetHandler, groupSyncRead, portHandler);
        std::this_thread::sleep_for(std::chrono::milliseconds(sleep_amount[i]));  
      }
    }

    // ROLL FW
    else if (command == "rfb") {
      move_to(perfect_cir, groupSyncWrite, packetHandler,groupSyncRead, portHandler); 

      const int NUM_MOVEMENTS = 2;
      int* roll_fw_movements[NUM_MOVEMENTS] = {
        blue_up_cir,
        perfect_cir
      };
      
      int sleep_amount[NUM_MOVEMENTS] = {
        300,
        100
      };
      
      for (int i = 0; i < NUM_MOVEMENTS; i++) {
        move_to(roll_fw_movements[i], groupSyncWrite, packetHandler, groupSyncRead, portHandler);
        std::this_thread::sleep_for(std::chrono::milliseconds(sleep_amount[i]));  
      }
    }

    // ROLL PROPEL FW
    else if (command == "rpy") {
      move_to(perfect_cir, groupSyncWrite, packetHandler,groupSyncRead, portHandler); 
      const int NUM_MOVEMENTS = 2;

      int* roll_fw_movements[NUM_MOVEMENTS] = {
        yellow_up_propel,
        perfect_cir
      };
      
      for (int i = 0; i < NUM_MOVEMENTS; i++) {
        move_to(roll_fw_movements[i], groupSyncWrite, packetHandler, groupSyncRead, portHandler);
        std::this_thread::sleep_for(std::chrono::milliseconds(700));  
      }
    }
    else if (command == "rpb") {
      move_to(perfect_cir, groupSyncWrite, packetHandler,groupSyncRead, portHandler); 

      const int NUM_MOVEMENTS = 2;
      int* roll_fw_movements[NUM_MOVEMENTS] = {
        blue_up_propel,
        perfect_cir
      };
      
      for (int i = 0; i < NUM_MOVEMENTS; i++) {
        move_to(roll_fw_movements[i], groupSyncWrite, packetHandler, groupSyncRead, portHandler);
        std::this_thread::sleep_for(std::chrono::milliseconds(700));  
      }
    }

    else if (command == "ali") {
      move_to(
        aligned_before_rolling,
        groupSyncWrite, 
        packetHandler,
        groupSyncRead,
        portHandler); 
    }
    else {
      std::cout << "Unknown command: " << command << "\n";
    }

     
   

    // Adjust sampling rate (lower value for higher frequency)
    // usleep(2000); // 10ms delay (~100Hz sampling rate)

  }

  // for (int i = 0; i < 20; i++) {
  //   // Disable Dynamixel# Torque
  //   dxl_comm_result = packetHandler->write1ByteTxRx(portHandler, DXL_ID, ADDR_PRO_TORQUE_ENABLE, TORQUE_DISABLE, &dxl_error);
  //   if (dxl_comm_result != COMM_SUCCESS)
  //   {
  //     printf("%s\n", packetHandler->getTxRxResult(dxl_comm_result));
  //   }
  //   else if (dxl_error != 0)
  //   {
  //     printf("%s\n", packetHandler->getRxPacketError(dxl_error));
  //   }
  // }

// csvFile.close();
// Close port
portHandler->closePort();

return 0;

}
