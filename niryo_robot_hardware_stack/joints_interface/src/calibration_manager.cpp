/*
    _calibration_manager.cpp
    Copyright (C) 2020 Niryo
    All rights reserved.

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http:// www.gnu.org/licenses/>.
*/

// std
#include <functional>
#include <string>
#include <utility>
#include <vector>
#include <fstream>

// to be migrated to std/filesystem when switching to C++17
#include <boost/filesystem.hpp>

// ros
#include <ros/console.h>

// niryo
#include "common/model/single_motor_cmd.hpp"
#include "common/model/synchronize_motor_cmd.hpp"
#include "common/model/stepper_command_type_enum.hpp"
#include "common/model/dxl_command_type_enum.hpp"
#include "common/model/stepper_calibration_status_enum.hpp"

#include "common/util/util_defs.hpp"

#include "joints_interface/calibration_manager.hpp"

using ::common::model::JointState;
using ::common::model::EStepperCommandType;
using ::common::model::DxlSyncCmd;
using ::common::model::StepperSingleCmd;
using ::common::model::StepperTtlSyncCmd;
using ::common::model::StepperMotorState;
using ::common::model::EStepperCalibrationStatus;
using ::common::model::EDxlCommandType;
using ::common::model::EBusProtocol;
using ::common::model::StepperTtlSingleCmd;

namespace joints_interface
{

/**
 * @brief CalibrationManager::CalibrationManager
 * @param nh
 * @param joint_list
 * @param ttl_interface
 * @param can_interface
 */
CalibrationManager::CalibrationManager(ros::NodeHandle& nh,
                                       std::vector<std::shared_ptr<JointState> > joint_list,
                                       std::shared_ptr<ttl_driver::TtlInterfaceCore> ttl_interface,
                                       std::shared_ptr<can_driver::CanInterfaceCore> can_interface) :
    _ttl_interface(std::move(ttl_interface)),
    _can_interface(std::move(can_interface)),
    _joint_states_list(std::move(joint_list))
{
    ROS_DEBUG("CalibrationManager::ctor");

    // we need at least tlt_interface
    assert(_ttl_interface);

    if (_can_interface)
        _stepper_bus_interface = _can_interface;
    else
        _stepper_bus_interface = _ttl_interface;

    assert(_stepper_bus_interface);

    initParameters(nh);

    ROS_INFO("Calibration Interface - Calibration interface started");
}

/**
 * @brief CalibrationManager::initParameters
 * @param nh
 */
void CalibrationManager::initParameters(ros::NodeHandle &nh)
{
    nh.getParam("calibration_timeout", _calibration_timeout);

    nh.getParam("calibration_file", _calibration_file_name);
    nh.getParam("/niryo_robot_hardware_interface/hardware_version", _hardware_version);

    ROS_DEBUG("Calibration Interface::initParameters - hardware_version %s", _hardware_version.c_str());
    ROS_DEBUG("Calibration Interface::initParameters - Calibration timeout %d", _calibration_timeout);

    ROS_DEBUG("Calibration Interface::initParameters - Calibration file name %s", _calibration_file_name.c_str());

    // get steppers specific params
    for (int currentIdStepper = 1; nh.hasParam("calibration_params/stepper_" + std::to_string(currentIdStepper) + "/id"); ++currentIdStepper)
    {
        // first get id
        std::string currentNamespace = "calibration_params/stepper_" + std::to_string(currentIdStepper);
        int id{-1};
        int stall_threshold{6};
        int direction{1};
        int delay{0};

        nh.getParam(currentNamespace + "/id", id);
        nh.getParam(currentNamespace + "/stall_threshold", stall_threshold);
        nh.getParam(currentNamespace + "/direction", direction);
        nh.getParam(currentNamespace + "/delay", delay);

        // acceleration and velocity profiles
        common::model::VelocityProfile profile{};
        int data{};
        if (nh.hasParam(currentNamespace + "/v_start"))
        {
            nh.getParam(currentNamespace + "/v_start", data);
            profile.v_start = static_cast<uint32_t>(data);
        }

        if (nh.hasParam(currentNamespace + "/a_1"))
        {
            nh.getParam(currentNamespace + "/a_1", data);
            profile.a_1 = static_cast<uint32_t>(data);
        }
        if (nh.hasParam(currentNamespace + "/v_1"))
        {
            nh.getParam(currentNamespace + "/v_1", data);
            profile.v_1 = static_cast<uint32_t>(data);
        }
        if (nh.hasParam(currentNamespace + "/a_max"))
        {
            nh.getParam(currentNamespace + "/a_max", data);
            profile.a_max = static_cast<uint32_t>(data);
        }
        if (nh.hasParam(currentNamespace + "/v_max"))
        {
            nh.getParam(currentNamespace + "/v_max", data);
            profile.v_max = static_cast<uint32_t>(data);
        }
        if (nh.hasParam(currentNamespace + "/d_max"))
        {
            nh.getParam(currentNamespace + "/d_max", data);
            profile.d_max = static_cast<uint32_t>(data);
        }
        if (nh.hasParam(currentNamespace + "/d_1"))
        {
            nh.getParam(currentNamespace + "/d_1", data);
            profile.d_1 = static_cast<uint32_t>(data);
        }
        if (nh.hasParam(currentNamespace + "/v_stop"))
        {
            nh.getParam(currentNamespace + "/v_stop", data);
            profile.v_stop = static_cast<uint32_t>(data);
        }

        // add parameters
        CalibrationConfig conf{static_cast<uint8_t>(stall_threshold),
                               static_cast<int8_t>(direction),
                               delay,
                               profile};

        ROS_DEBUG("Calibration Interface::initParameters - stepper (id %d): stall threshold: %d, direction: %d, delay: %d",
                  id, stall_threshold, direction, delay);

        ROS_DEBUG("Calibration Interface::initParameters - Calibration Profile: {%d, %d, %d, %d, %d, %d, %d, %d}",
                  profile.v_start,
                  profile.a_1,
                  profile.v_1,
                  profile.a_max,
                  profile.v_max,
                  profile.d_max,
                  profile.d_1,
                  profile.v_stop);

        _calibration_params_map.insert(std::make_pair(static_cast<uint8_t>(id), conf));
    }
}

/**
 * @brief CalibrationManager::startCalibration
 * @param mode
 * @param result_message
 * @return
 */
int CalibrationManager::startCalibration(int mode, std::string &result_message)
{
    int res = niryo_robot_msgs::CommandStatus::CALIBRATION_NOT_DONE;
    result_message.clear();

    // if ttl connection is ok AND (can not present OR can connection ok)
    if ((_ttl_interface && _ttl_interface->isConnectionOk()) &&
        (_stepper_bus_interface && _stepper_bus_interface->isConnectionOk()))
    {
        if (AUTO_CALIBRATION == mode)  // auto
        {
            if (EStepperCalibrationStatus::OK == autoCalibration())
            {
                result_message = "Calibration Interface - Calibration done";
                res = niryo_robot_msgs::CommandStatus::SUCCESS;
            }
            else
                result_message = "Calibration Interface - auto calibration failed";
        }
        else if (MANUAL_CALIBRATION == mode)  // manuel
        {
            if (_hardware_version == "one")
            {
                if (canProcessManualCalibration(result_message))
                {
                    if (EStepperCalibrationStatus::OK == manualCalibration())
                    {
                        result_message = "Calibration Interface - Calibration done";
                        res = niryo_robot_msgs::CommandStatus::SUCCESS;
                    }
                    else
                        result_message = "Calibration Interface - manual calibration failed";
                }
            }
            else
            {
                result_message = "Calibration Interface - Command calibration manual on this plateform is not available";
                res = niryo_robot_msgs::CommandStatus::SUCCESS;
            }
        }
        else                          // unknown
        {
            result_message = "Calibration Interface - Command error";
            res = niryo_robot_msgs::CommandStatus::FAILURE;
        }
    }
    else
    {
        result_message = "Calibration Interface - Please ensure that all motors are connected";
    }

    ROS_ERROR_COND(niryo_robot_msgs::CommandStatus::SUCCESS != res,
                  "Calibration Interface - Calibration error : %s", result_message.c_str());

    return res;
}

/**
 * @brief CalibrationManager::getCalibrationStatus
 * @return
 */
EStepperCalibrationStatus
CalibrationManager::getCalibrationStatus() const
{
    EStepperCalibrationStatus status{EStepperCalibrationStatus::FAIL};

    if (_stepper_bus_interface)
        status = _stepper_bus_interface->getCalibrationStatus();

    return status;
}

/**
 * @brief CalibrationManager::canProcessManualCalibration
 * @param result_message
 * @return
 */
bool CalibrationManager::canProcessManualCalibration(std::string &result_message)
{
    bool res = true;
    result_message.clear();

    if (_stepper_bus_interface)
    {
        auto stepper_motor_states = _stepper_bus_interface->getJointStates();

        // 1. Check if motors firmware version is ok
        for (auto const& mState : stepper_motor_states)
        {
            if (mState)
            {
                std::string firmware_version = std::dynamic_pointer_cast<StepperMotorState>(mState)->getFirmwareVersion();
                if (!firmware_version.empty())
                {
                    if (stoi(firmware_version.substr(0, 1)) >= 2)
                    {
                        // 2. Check if motor offset values have been previously saved (with auto calibration)
                        std::vector<int> motor_id_list;
                        std::vector<int> steps_list;

                        if (readCalibrationOffsetsFromFile(motor_id_list, steps_list))
                        {
                            // 3. Check if all connected motors have a motor offset value

                            bool found = false;

                            for (auto const& m_id : motor_id_list)
                            {
                                if (m_id == mState->getId())
                                {
                                    found = true;
                                    break;
                                }
                            }

                            if (!found)
                            {
                                result_message = "Calibration Interface - Motor " +
                                                std::to_string(mState->getId()) +
                                                " does not have a saved offset value, " +
                                                "you need to do one auto calibration";

                                res = false;
                            }
                        }
                        else
                        {
                            result_message = "Calibration Interface - You need to make an "
                                             "auto calibration before using the manual calibration";

                            res = false;
                        }
                    }
                    else
                    {
                        result_message = "Calibration Interface - You need to upgrade stepper firmware for motor " +
                                            std::to_string(mState->getId());

                        res = false;
                    }
                }
                else
                {
                    result_message = "Calibration Interface - No firmware version available for motor " +
                                    std::to_string(mState->getId()) +
                                    ". Make sure all motors are connected";

                    res = false;
                }
            }  // if (mState)
        }  // for (auto const& mState : stepper_motor_states)
    }
    else
    {
      result_message = "Calibration Interface - Steppers interface not available";

      res = false;
    }

    ROS_ERROR_COND(!res, "Calibration Interface - Can't process manual calibration : %s", result_message.c_str());

    return res;
}

/**
 * @brief CalibrationManager::autoCalibration
 * @return
 */
EStepperCalibrationStatus CalibrationManager::autoCalibration()
{
    // 0. Init velocity profile
    initVelocityProfiles();

    // 1. Place robot in position
    moveRobotBeforeCalibration();

    // 2. Send calibration cmd 1 + 2 + 3 (from can or ttl depending of which interface is instanciated)
    sendCalibrationToSteppers();

    double timeout = 0.0;
    // 3. wait for calibration status to change

    // get calibration result final
    EStepperCalibrationStatus final_status = EStepperCalibrationStatus::IN_PROGRESS;

    while (EStepperCalibrationStatus::IN_PROGRESS == final_status)
    {
        ros::Duration(0.2).sleep();
        timeout += 0.2;
        if (timeout >= 30.0)
        {
            _stepper_bus_interface->resetCalibration();
            ROS_ERROR("CalibrationManager::autoCalibration - calibration timeout, please try again");
            return common::model::EStepperCalibrationStatus::TIMEOUT;
        }
        final_status = getCalibrationStatus();
        ROS_DEBUG("CalibrationManager::autoCalibration - calibration status, %s", common::model::StepperCalibrationStatusEnum(final_status).toString().c_str());
    }

    ros::Duration(0.5).sleep();

    // 4. retrieve values for the calibration
    std::vector<int> sensor_offset_results;
    std::vector<int> sensor_offset_ids;

    for (auto const& jState : _joint_states_list)
    {
        if (jState && jState->isStepper())
        {
            uint8_t motor_id = jState->getId();

            if (_stepper_bus_interface)
            {
                int calibration_result = _stepper_bus_interface->getCalibrationResult(motor_id);

                sensor_offset_results.emplace_back(calibration_result);
                sensor_offset_ids.emplace_back(motor_id);

                ROS_INFO("CalibrationManager::autoCalibration - Motor %d, calibration cmd result %d ", motor_id, calibration_result);
            }
        }
    }

    if (EStepperCalibrationStatus::OK == final_status)
    {
        ROS_INFO("CalibrationManager::autoCalibration -  Calibration successfull, going back home");

        // 8. put back velocity profiles to normal
        resetVelocityProfiles();

        // 5. Move steppers to home
        moveSteppersToHome();
        ros::Duration(3.5).sleep();

        // 6. Write sensor_offset_steps to file
        saveCalibrationOffsetsToFile(sensor_offset_ids, sensor_offset_results);
    }
    else
    {
        ROS_ERROR("CalibrationManager::autoCalibration -  An error occurred while calibrating stepper motors");
    }

    // 7 - activate torque for ned2, disactivate for ned1
    activateTorque("ned2" == _hardware_version);

    return final_status;
}

/**
 * @brief CalibrationManager::manualCalibration
 * @return
 *
 * only for niryo one
 */
EStepperCalibrationStatus
CalibrationManager::manualCalibration()
{
    EStepperCalibrationStatus status = EStepperCalibrationStatus::FAIL;

    if  ("ned2" != _hardware_version)
    {
        if (_stepper_bus_interface)
        {
            std::vector<int> motor_id_list;
            std::vector<int> steps_list;

            if (readCalibrationOffsetsFromFile(motor_id_list, steps_list))
            {
                // 0. Torque ON for motor 2
                auto state = std::dynamic_pointer_cast<common::model::StepperMotorState>(_joint_states_list.at(1));
                if (state)
                {
                    int steps_per_rev = state->stepsPerRev();

                    for (size_t i = 0; i < motor_id_list.size(); i++)
                    {
                        int offset_to_send = 0;
                        int motor_id = motor_id_list.at(i);
                        int sensor_offset_steps = steps_list.at(i);

                        if (motor_id == _joint_states_list.at(0)->getId())
                        {
                            offset_to_send = sensor_offset_steps - _joint_states_list.at(0)->to_motor_pos(_joint_states_list.at(0)->getLimitPositionMax()) % steps_per_rev;
                            if (offset_to_send < 0)
                                offset_to_send += steps_per_rev;

                            StepperSingleCmd stepper_cmd(EStepperCommandType::CMD_TYPE_POSITION_OFFSET, _joint_states_list.at(0)->getId(),
                                                         {offset_to_send, offset_to_send});
                            _stepper_bus_interface->addSingleCommandToQueue(std::make_unique<StepperSingleCmd>(stepper_cmd));
                        }
                        else if (motor_id == _joint_states_list.at(1)->getId())
                        {
                            offset_to_send = sensor_offset_steps - _joint_states_list.at(1)->to_motor_pos(_joint_states_list.at(1)->getLimitPositionMax());

                            offset_to_send %= steps_per_rev;
                            if (offset_to_send < 0)
                                offset_to_send += steps_per_rev;

                            StepperSingleCmd stepper_cmd(EStepperCommandType::CMD_TYPE_POSITION_OFFSET, _joint_states_list.at(1)->getId(),
                                                         {offset_to_send, offset_to_send});
                            _stepper_bus_interface->addSingleCommandToQueue(std::make_unique<StepperSingleCmd>(stepper_cmd));
                        }
                        else if (motor_id == _joint_states_list.at(2)->getId())
                        {
                            offset_to_send = sensor_offset_steps - _joint_states_list.at(2)->to_motor_pos(_joint_states_list.at(2)->getLimitPositionMin());

                            StepperSingleCmd stepper_cmd(EStepperCommandType::CMD_TYPE_POSITION_OFFSET, _joint_states_list.at(2)->getId(),
                                                         {offset_to_send, sensor_offset_steps});
                            _stepper_bus_interface->addSingleCommandToQueue(std::make_unique<StepperSingleCmd>(stepper_cmd));
                        }
                        ros::Duration(0.2).sleep();
                    }

                    status = _stepper_bus_interface->getCalibrationStatus();
                }
            }  // if (state)
        }  // if (getMotorsCalibrationOffsets(motor_id_list, steps_list))
    }
    else
    {
      ROS_ERROR("CalibrationManager::manualCalibration : manual calibration not available for %s robot."
                "Only supported for Niryo One and Niryo Ned", _hardware_version.c_str());
    }

    return status;
}

//**********************
//  Commands to the robot
//*********************

/**
 * @brief CalibrationManager::setTorqueStepperMotor
 * @param pState
 * @param status
 */
void CalibrationManager::setTorqueStepperMotor(const std::shared_ptr<JointState>& pState,
                                               bool status)
{
    _ttl_interface->waitSyncQueueFree();

    if (pState && pState->isStepper())
    {
        uint8_t motor_id = pState->getId();

        if (_can_interface && EBusProtocol::CAN == pState->getBusProtocol())
        {
            _can_interface->addSingleCommandToQueue(std::make_unique<StepperSingleCmd>(
                                                    StepperSingleCmd(EStepperCommandType::CMD_TYPE_TORQUE,
                                                                     motor_id, {status})));
        }
        else if (_ttl_interface && EBusProtocol::TTL == pState->getBusProtocol())
        {
            _ttl_interface->addSingleCommandToQueue(std::make_unique<StepperTtlSingleCmd>(
                                                      StepperTtlSingleCmd(EStepperCommandType::CMD_TYPE_TORQUE,
                                                                            motor_id, {status})));
        }
    }
}

/**
 * @brief CalibrationManager::initVelocityProfiles
 */
void CalibrationManager::initVelocityProfiles()
{
    _ttl_interface->waitSyncQueueFree();

    if ("ned2" == _hardware_version)
    {
        for (auto param : _calibration_params_map)
        {
            // CMD_TYPE_VELOCITY_PROFILE cmd
            StepperTtlSingleCmd cmd_profile(
                        EStepperCommandType::CMD_TYPE_VELOCITY_PROFILE,
                        param.first,
                        param.second.profile.to_list());
            _ttl_interface->addSingleCommandToQueue(std::make_unique<StepperTtlSingleCmd>(cmd_profile));
        }
    }
}

/**
 * @brief CalibrationManager::resetVelocityProfiles
 */
void CalibrationManager::resetVelocityProfiles()
{
    _ttl_interface->waitSyncQueueFree();

    if ("ned2" == _hardware_version)
    {
        for (auto const& jState : _joint_states_list)
        {
            if (jState && jState->isStepper() && EBusProtocol::TTL == jState->getBusProtocol())
            {
                auto stepperState = std::dynamic_pointer_cast<StepperMotorState>(jState);
                if (stepperState)
                {
                    // CMD_TYPE_VELOCITY_PROFILE cmd
                    StepperTtlSingleCmd cmd_profile(
                                EStepperCommandType::CMD_TYPE_VELOCITY_PROFILE,
                                stepperState->getId(),
                                stepperState->getVelocityProfile().to_list());
                    _ttl_interface->addSingleCommandToQueue(std::make_unique<StepperTtlSingleCmd>(cmd_profile));
                }
            }
        }

        _ttl_interface->waitSingleQueueFree();
    }
}

/**
 * @brief CalibrationManager::moveRobotBeforeCalibration
 */
void CalibrationManager::moveRobotBeforeCalibration()
{
    // 0. activate torque for all motors
    activateTorque(true);

    _ttl_interface->waitSyncQueueFree();

    // 1. Relative Move Motor 1 (can only)
    if (_can_interface &&
        _joint_states_list.at(0)->isStepper() &&
        common::model::EBusProtocol::CAN == _joint_states_list.at(0)->getBusProtocol())
    {
        uint8_t motor_id = _joint_states_list.at(0)->getId();
        int steps = -500 * _joint_states_list.at(0)->getDirection();
        int delay = 200;

        _can_interface->addSingleCommandToQueue(std::make_unique<StepperSingleCmd>(
                                                    StepperSingleCmd(EStepperCommandType::CMD_TYPE_RELATIVE_MOVE,
                                                                     motor_id, {steps, delay})));
    }

    // 2. Relative Move Motor 3
    if (_joint_states_list.at(2)->isStepper())
    {
        uint8_t motor_id = _joint_states_list.at(2)->getId();

        if (EBusProtocol::CAN == _joint_states_list.at(2)->getBusProtocol())
        {
            int steps = _joint_states_list.at(2)->to_motor_pos(0.25);
            int delay = 500;

            _can_interface->addSingleCommandToQueue(std::make_unique<StepperSingleCmd>(
                                                      StepperSingleCmd(EStepperCommandType::CMD_TYPE_RELATIVE_MOVE,
                                                                       motor_id, {steps, delay})));
        }
        else if (EBusProtocol::TTL == _joint_states_list.at(2)->getBusProtocol())
        {
            auto steps = static_cast<uint32_t>(_joint_states_list.at(2)->getPosition() + 10 * _joint_states_list.at(2)->getDirection());

            _ttl_interface->addSingleCommandToQueue(std::make_unique<StepperTtlSingleCmd>(
                                                     StepperTtlSingleCmd(EStepperCommandType::CMD_TYPE_POSITION,
                                                                         motor_id, {steps})));
        }
    }

    _ttl_interface->waitSingleQueueFree();

    // 3. Move All Dynamixel to Home Position
    if (_ttl_interface)
    {
        // set torque on
        DxlSyncCmd dynamixel_cmd(EDxlCommandType::CMD_TYPE_POSITION);

        for (auto const& jState : _joint_states_list)
        {
            if (jState && jState->isDynamixel())
            {
                dynamixel_cmd.addMotorParam(jState->getHardwareType(), jState->getId(),
                                            static_cast<uint32_t>(jState->to_motor_pos(jState->getHomePosition())));
            }
        }

        _ttl_interface->addSyncCommandToQueue(std::make_unique<DxlSyncCmd>(dynamixel_cmd));
    }

    // Wait a little bit for all dxl go to home before calibration
    _ttl_interface->waitSyncQueueFree();
}

/**
 * @brief CalibrationManager::moveSteppersToHome
 * // move all steppers to home
 */
void CalibrationManager::moveSteppersToHome()
{
    // 2. move all steppers to Home Position
    StepperTtlSyncCmd stepper_ttl_cmd(EStepperCommandType::CMD_TYPE_POSITION);

    for (auto const& jState : _joint_states_list)
    {
        if (jState && jState->isStepper())
        {
            uint8_t motor_id = jState->getId();
            int steps{0};

            if (EBusProtocol::CAN == jState->getBusProtocol())
            {
                // after calibration, joint 1 2 is at limit max but joint 3 is at limit min
                if (motor_id != 3)
                    steps = jState->to_motor_pos(jState->getHomePosition()) - jState->to_motor_pos(jState->getLimitPositionMax());
                else
                    steps = jState->to_motor_pos(jState->getHomePosition()) - jState->to_motor_pos(jState->getLimitPositionMin());
                int delay = 550;
                _can_interface->addSingleCommandToQueue(std::make_unique<StepperSingleCmd>(StepperSingleCmd(
                                                            EStepperCommandType::CMD_TYPE_RELATIVE_MOVE, motor_id, {steps, delay})));
            }
            else if (EBusProtocol::TTL == jState->getBusProtocol())
            {
                steps = jState->to_motor_pos(jState->getHomePosition());
                stepper_ttl_cmd.addMotorParam(jState->getHardwareType(), motor_id, static_cast<uint32_t>(steps));
            }
        }
    }

    if  (stepper_ttl_cmd.isValid())
        _ttl_interface->addSyncCommandToQueue(std::make_unique<StepperTtlSyncCmd>(stepper_ttl_cmd));
}

/**
 * @brief CalibrationManager::sendCalibrationToSteppers
 */
void CalibrationManager::sendCalibrationToSteppers()
{
    // 2. for each stepper, configure and send calibration cmd
    for (auto const& jState : _joint_states_list)
    {
        if (jState && jState->isStepper())  // only steppers can be calibrated
        {
            auto pStepperMotorState = std::dynamic_pointer_cast<StepperMotorState>(jState);
            if (pStepperMotorState && pStepperMotorState->isValid())
            {
                uint8_t id = pStepperMotorState->getId();
                int8_t direction{};
                uint8_t stall_threshold{};
                int32_t delay{};

                if  (_calibration_params_map.count(id))
                {
                    stall_threshold = _calibration_params_map.at(id).stall_threshold;
                    direction = pStepperMotorState->getDirection() * _calibration_params_map.at(id).direction;
                    delay = _calibration_params_map.at(id).delay;
                }

                if (_can_interface && EBusProtocol::CAN == pStepperMotorState->getBusProtocol())
                {
                    int32_t offset;
                    // max limit in robot is correspond to the position initial of motor, with other motors
                    // min limit is correspond to the position initial
                    if (jState->getId() == 3)
                        offset = pStepperMotorState->to_motor_pos(pStepperMotorState->getLimitPositionMin());
                    else
                        offset = pStepperMotorState->to_motor_pos(pStepperMotorState->getLimitPositionMax());

                    _can_interface->addSingleCommandToQueue(std::make_unique<StepperSingleCmd>(
                                                            StepperSingleCmd(EStepperCommandType::CMD_TYPE_CALIBRATION, id,
                                                                                       {offset, delay, direction, _calibration_timeout})));
                }
                else if (_ttl_interface && EBusProtocol::TTL == pStepperMotorState->getBusProtocol())
                {
                    // for stepper TTL 0 is decreasing direction
                    uint8_t ttl_direction = (direction < 0) ? 0 : 1;

                    // send config before calibrate
                    _ttl_interface->addSingleCommandToQueue(std::make_unique<StepperTtlSingleCmd>(
                                                            StepperTtlSingleCmd(EStepperCommandType::CMD_TYPE_CALIBRATION_SETUP,
                                                                                id, {ttl_direction, stall_threshold})));

                    _ttl_interface->addSingleCommandToQueue(std::make_unique<StepperTtlSingleCmd>(
                                                            StepperTtlSingleCmd(EStepperCommandType::CMD_TYPE_CALIBRATION,
                                                                                id)));
                }
            }
        }
    }

    _ttl_interface->waitSingleQueueFree();
}

/**
 * @brief CalibrationManager::activateTorque
 * @param activated
 */
void CalibrationManager::activateTorque(bool activated)
{
    _ttl_interface->waitSingleQueueFree();

    ROS_DEBUG("CalibrationManager::activateTorque - activate learning mode");

    DxlSyncCmd dxl_cmd(EDxlCommandType::CMD_TYPE_TORQUE);
    StepperTtlSyncCmd stepper_ttl_cmd(EStepperCommandType::CMD_TYPE_TORQUE);

    for (auto const& jState : _joint_states_list)
    {
        if (jState)
        {
            if (jState->getBusProtocol() == EBusProtocol::TTL)
            {
                if (jState->isDynamixel())
                  dxl_cmd.addMotorParam(jState->getHardwareType(), jState->getId(), activated);
                else
                  stepper_ttl_cmd.addMotorParam(jState->getHardwareType(), jState->getId(), activated);
            }
            else
            {
                StepperSingleCmd stepper_cmd(EStepperCommandType::CMD_TYPE_TORQUE);

                stepper_cmd.setId(jState->getId());
                stepper_cmd.setParams({activated});
                if (_can_interface)
                    _can_interface->addSingleCommandToQueue(std::make_unique<StepperSingleCmd>(stepper_cmd));
            }
        }
    }

    if (_ttl_interface)
    {
        if (dxl_cmd.isValid())
          _ttl_interface->addSyncCommandToQueue(std::make_unique<DxlSyncCmd>(dxl_cmd));

        if (stepper_ttl_cmd.isValid())
          _ttl_interface->addSyncCommandToQueue(std::make_unique<StepperTtlSyncCmd>(stepper_ttl_cmd));
    }
}

//********************
//  file IO
//********************

/**
 * @brief CalibrationManager::saveCalibrationOffsetsToFile
 * @param motor_id_list
 * @param steps_list
 * @return
 */
bool CalibrationManager::saveCalibrationOffsetsToFile(const std::vector<int> &motor_id_list,
                                                     const std::vector<int> &steps_list)
{
    bool res = false;
    if (motor_id_list.size() == steps_list.size())
    {
        size_t found = _calibration_file_name.find_last_of('/');
        std::string folder_name = _calibration_file_name.substr(0, found);

        boost::filesystem::path filepath(_calibration_file_name);
        boost::filesystem::path directory(folder_name);

        // Create dir if not exist
        boost::system::error_code returned_error;
        boost::filesystem::create_directories(directory, returned_error);

        if (!returned_error)
        {
            // Create text to write
            std::string text_to_write;
            for (size_t i = 0; i < motor_id_list.size(); i++)
            {
                text_to_write += std::to_string(motor_id_list.at(i));
                text_to_write += ":";
                text_to_write += std::to_string(steps_list.at(i));
                if (i < motor_id_list.size() - 1)
                {
                    text_to_write += "\n";
                }
            }

            // Write to file
            std::ofstream offset_file(_calibration_file_name.c_str());
            if (offset_file.is_open())
            {
                ROS_DEBUG("CalibrationManager::set_motors_calibration_offsets - Writing calibration offsets to file : \n%s",
                          text_to_write.c_str());

                offset_file << text_to_write.c_str();
                offset_file.close();

                res = true;
            }
            else
            {
                ROS_WARN("CalibrationManager::set_motors_calibration_offsets - Unable to open file : %s",
                         _calibration_file_name.c_str());
            }
        }
        else
        {
            ROS_WARN("CalibrationManager::set_motors_calibration_offsets - Could not create directory : %s",
                     folder_name.c_str());
        }
    }
    else
    {
        ROS_ERROR("CalibrationManager::set_motors_calibration_offsets - Corrupted command"
                  ": motors id list and params list size mismatch");
    }

    return res;
}

/**
 * @brief CalibrationManager::readCalibrationOffsetsFromFile
 * @param motor_id_list
 * @param steps_list
 * @return
 */
bool CalibrationManager::readCalibrationOffsetsFromFile(std::vector<int> &motor_id_list,
                                                        std::vector<int> &steps_list)
{
    bool res = false;

    std::vector<std::string> lines;
    std::string current_line;

    motor_id_list.clear();
    steps_list.clear();

    std::ifstream offset_file(_calibration_file_name.c_str());

    if (offset_file.is_open())
    {
        // read all lines
        while (getline(offset_file, current_line))
        {
            try
            {
                size_t index = current_line.find(':');
                motor_id_list.emplace_back(stoi(current_line.substr(0, index)));
                steps_list.emplace_back(stoi(current_line.erase(0, index + 1)));
            }
            catch (...)
            {
                ROS_ERROR("CalibrationManager::getMotorsCalibrationOffsets - Exception caught during file reading");
            }
        }

        offset_file.close();
        res = true;
    }
    else
    {
        ROS_WARN("Motor Offset - Unable to open file : %s", _calibration_file_name.c_str());
    }

    return res;
}

}  // namespace joints_interface
