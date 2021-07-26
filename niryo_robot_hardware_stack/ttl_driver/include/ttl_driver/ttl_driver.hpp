/*
ttl_driver.hpp
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

#ifndef TTL_DRIVER_HPP
#define TTL_DRIVER_HPP

#include <memory>
#include <ros/ros.h>
#include <string>
#include <thread>
#include <queue>
#include <functional>
#include <algorithm>
#include <set>

#include "dynamixel_sdk/dynamixel_sdk.h"

#include "ttl_driver/MotorCommand.h"
#include "niryo_robot_msgs/MotorHeader.h"
#include "niryo_robot_msgs/SetInt.h"
#include "niryo_robot_msgs/CommandStatus.h"

#include "common/model/idriver.hpp"

// drivers
#include "ttl_driver/abstract_motor_driver.hpp"
#include "common/model/dxl_motor_state.hpp"
#include "common/model/synchronize_motor_cmd.hpp"
#include "common/model/single_motor_cmd.hpp"
#include "common/model/stepper_calibration_status_enum.hpp"

namespace ttl_driver
{

/**
 * Parameters for DXL
*/
constexpr float DXL_BUS_PROTOCOL_VERSION = 2.0;
constexpr int DXL_FAIL_OPEN_PORT         = -4500;

constexpr int DXL_FAIL_PORT_SET_BAUDRATE = -4501;
constexpr int DXL_FAIL_SETUP_GPIO        = -4502;

constexpr int DXL_SCAN_OK                = 0;
constexpr int DXL_SCAN_MISSING_MOTOR     = -50;
constexpr int DXL_SCAN_UNALLOWED_MOTOR   = -51;
constexpr int DXL_WRONG_TYPE             = -52;

/**
 * Parameters for Stepper
*/

/**
 * @brief The TtlDriver class
 */
class TtlDriver : public common::model::IDriver
{
    public:
        enum class EType
        {
            TOOL,
            CONVOYER,
            JOINT,
        };
    public:
        TtlDriver(ros::NodeHandle& nh);
        virtual ~TtlDriver() override;

        bool init(ros::NodeHandle& nh) override;

        // commands
        void addMotor(common::model::EMotorType type,
                      uint8_t id, EType type_used);
        
        template<typename Type>
        int readSynchronizeCommand(std::shared_ptr<common::model::SynchronizeMotorCmdI> cmd);
        
        template<typename Type>
        int readSingleCommand(std::shared_ptr<common::model::SingleMotorCmdI> cmd);
            
        void executeJointTrajectoryCmd(std::vector<std::pair<uint8_t, uint32_t> > cmd_vec);

        int rebootMotors();
        int rebootMotor(uint8_t motor_id);

        int setLeds(int led, common::model::EMotorType type);

        int sendCustomCommand(common::model::EMotorType motor_type, uint8_t id, int reg_address, int value, int byte_number);
        int readCustomCommand(common::model::EMotorType motor_type, uint8_t id, int32_t reg_address, int &value, int byte_number);

        void readPositionStatus();
        void readHwStatus();

        int getAllIdsOnBus(std::vector<uint8_t> &id_list);

        void startCalibration();
        void resetCalibration();
        bool isCalibrationInProgress() const;
        int32_t getCalibrationResult(uint8_t id) const;
        common::model::EStepperCalibrationStatus getCalibrationStatus() const;

        // getters
        uint32_t getPosition(common::model::JointState& motor_state);
        int getLedState() const;

        std::vector<std::shared_ptr<common::model::JointState> > getMotorsStates() const;
        template<class T>
        T getMotorState(uint8_t motor_id) const;

        std::vector<uint8_t> getRemovedMotorList() const;

        // IDriver Interface
        void removeMotor(uint8_t id) override;
        bool isConnectionOk() const override;

        int scanAndCheck() override;
        bool ping(uint8_t id) override;

        size_t getNbMotors() const override;
        void getBusState(bool& connection_state, std::vector<uint8_t>& motor_id, std::string& debug_msg) const override;
        std::string getErrorMessage() const override;
    private:
        bool hasMotors() override;

        int setupCommunication();

        void checkRemovedMotors();

        int _syncWrite(int (AbstractMotorDriver::*syncWriteFunction)(const std::vector<uint8_t> &, const std::vector<uint32_t> &),
                              std::shared_ptr<common::model::SynchronizeMotorCmdI> cmd);
        
        int _singleWrite(int (AbstractMotorDriver::*singleWriteFunction)(uint8_t id, uint32_t), common::model::EMotorType motor_type,
                              std::shared_ptr<common::model::SingleMotorCmdI> cmd);
    private:
        std::shared_ptr<dynamixel::PortHandler> _PortHandler;
        std::shared_ptr<dynamixel::PacketHandler> _PacketHandler;

        std::string _device_name;
        int _uart_baudrate;

        std::vector<uint8_t> _all_motor_connected; // with all dxl motors connected (including the tool)
        std::vector<uint8_t> _removed_motor_id_list;

        std::map<uint8_t, std::shared_ptr<common::model::JointState> > _state_map;
        std::map<common::model::EMotorType, std::vector<uint8_t> > _ids_map;
        std::map<common::model::EMotorType, std::shared_ptr<AbstractMotorDriver> > _driver_map;

        double _calibration_timeout{30.0};
        common::model::EStepperCalibrationStatus _calibration_status;
        // for hardware control
        bool _is_connection_ok;
        std::string _debug_error_message;

        int _hw_fail_counter_read;
        
        int _led_state;

        static constexpr int MAX_HW_FAILURE = 25;
};

// inline getters

/**
 * @brief TtlDriver::getMotorState
 * @param motor_id
 * @return
 */
template<class T>
T TtlDriver::getMotorState(uint8_t motor_id) const
{
    if (!_state_map.count(motor_id) && _state_map.at(motor_id))
        throw std::out_of_range("TtlDriver::getMotorsState: Unknown motor id");

    return *(dynamic_cast<T*>(_state_map.at(motor_id).get()));
}

inline
bool TtlDriver::isConnectionOk() const
{
    return _is_connection_ok;
}

/**
 * @brief TtlDriver::getNbMotors
 * @return
 */
inline
size_t TtlDriver::getNbMotors() const
{
    return _state_map.size();
}

/**
 * @brief TtlDriver::getRemovedMotorList
 * @return
 */
inline
std::vector<uint8_t> TtlDriver::getRemovedMotorList() const
{
    return _removed_motor_id_list;
}

/**
 * @brief TtlDriver::getErrorMessage
 * @return
 */
inline
std::string TtlDriver::getErrorMessage() const
{
    return _debug_error_message;
}

/**
 * @brief TtlDriver::getLedState
 * @return
 */
inline
int TtlDriver::getLedState() const
{
    return _led_state;
}

/**
 * @brief TtlDriver::getBusState
 * @param connection_state
 * @param motor_id
 * @param debug_msg
 */
inline
void TtlDriver::getBusState(bool &connection_state, std::vector<uint8_t> &motor_id,
                            std::string &debug_msg) const
{
    debug_msg = _debug_error_message;
    motor_id = _all_motor_connected;
    connection_state = isConnectionOk();
}

/**
 * @brief TtlDriver::hasMotors
 * @return
 */
inline
bool TtlDriver::hasMotors()
{
    return _state_map.size() > 0;
}

// ******************
//  Write operations
// ******************

/**
 * @brief TtlDriver::readSynchronizeCommand
 * @param cmd
 */
template<typename Type>
int TtlDriver::readSynchronizeCommand(std::shared_ptr<common::model::SynchronizeMotorCmdI> cmd)
{
    int result = COMM_TX_ERROR;
    ROS_DEBUG_THROTTLE(0.5, "TtlDriver::readSynchronizeCommand:  %s", cmd->str().c_str());

    if (cmd->isValid())
    {
        switch (Type(cmd->getTypeCmd()))
        {
            case Type::CMD_TYPE_POSITION:
                result = _syncWrite(&AbstractMotorDriver::syncWritePositionGoal, cmd);
            break;
            case Type::CMD_TYPE_VELOCITY:
                result = _syncWrite(&AbstractMotorDriver::syncWriteVelocityGoal, cmd);
            break;
            case Type::CMD_TYPE_EFFORT:
                result = _syncWrite(&AbstractMotorDriver::syncWriteTorqueGoal, cmd);
            break;
            case Type::CMD_TYPE_TORQUE:
                result = _syncWrite(&AbstractMotorDriver::syncWriteTorqueEnable, cmd);
            break;
            case Type::CMD_TYPE_LEARNING_MODE:
                result = _syncWrite(&AbstractMotorDriver::syncWriteTorqueEnable, cmd);
            break;
            default:
                ROS_ERROR("TtlDriver::readSynchronizeCommand - Unsupported command type: %d",
                                cmd->getTypeCmd());
            break;
        }
    }
    else
{
        ROS_ERROR("TtlDriver::readSynchronizeCommand - Invalid command");
    }

    return result;
}

/**
 * @brief TtlDriver::readSingleCommand
 * @param cmd
 */
template<typename Type>
int TtlDriver::readSingleCommand(std::shared_ptr<common::model::SingleMotorCmdI> cmd)
{
    int result = COMM_TX_ERROR;
    uint8_t id = cmd->getId();
    
    if (cmd->isValid())
    {
        int counter = 0;

        ROS_DEBUG_THROTTLE(0.5, "TtlDriver::readSingleCommand:  %s", cmd->str().c_str());

        if (_state_map.count(id) != 0)
        {
            auto state = _state_map.at(id);

            while ((COMM_SUCCESS != result) && (counter < 50))
            {
                switch ((Type)cmd->getTypeCmd())
                {
                case Type::CMD_TYPE_VELOCITY:
                    result = _singleWrite(&AbstractMotorDriver::setGoalVelocity, state->getType(), cmd);
                    break;
                case Type::CMD_TYPE_POSITION:
                    result = _singleWrite(&AbstractMotorDriver::setGoalPosition, state->getType(), cmd);
                    break;
                case Type::CMD_TYPE_EFFORT:
                    result = _singleWrite(&AbstractMotorDriver::setGoalTorque, state->getType(), cmd);
                    break;
                case Type::CMD_TYPE_TORQUE:
                    result = _singleWrite(&AbstractMotorDriver::setTorqueEnable, state->getType(), cmd);
                    break;
                case Type::CMD_TYPE_POSITION_P_GAIN:
                    result = _singleWrite(&AbstractMotorDriver::setPositionPGain, state->getType(), cmd);
                    break;
                case Type::CMD_TYPE_POSITION_I_GAIN:
                    result = _singleWrite(&AbstractMotorDriver::setPositionIGain, state->getType(), cmd);
                    break;
                case Type::CMD_TYPE_POSITION_D_GAIN:
                    result = _singleWrite(&AbstractMotorDriver::setPositionDGain, state->getType(), cmd);
                    break;
                case Type::CMD_TYPE_VELOCITY_P_GAIN:
                    result = _singleWrite(&AbstractMotorDriver::setVelocityPGain, state->getType(), cmd);
                    break;
                case Type::CMD_TYPE_VELOCITY_I_GAIN:
                    result = _singleWrite(&AbstractMotorDriver::setVelocityIGain, state->getType(), cmd);
                    break;
                case Type::CMD_TYPE_FF1_GAIN:
                    result = _singleWrite(&AbstractMotorDriver::setff1Gain, state->getType(), cmd);
                    break;
                case Type::CMD_TYPE_FF2_GAIN:
                    result = _singleWrite(&AbstractMotorDriver::setff2Gain, state->getType(), cmd);
                    break;
                case Type::CMD_TYPE_PING:
                    result = ping(state->getId()) ? COMM_SUCCESS : COMM_TX_FAIL;
                    break;
                default:
                    break;
                }

                counter += 1;

                ros::Duration(TIME_TO_WAIT_IF_BUSY).sleep();
            }
        }
    }

    if (result != COMM_SUCCESS)
    {
        ROS_WARN("TtlDriver::readSingleCommand - Failed to write a single command on dxl motor id : %d", id);
        _debug_error_message = "TtlDriver - Failed to write a single command";
    }

    return result;
}

}

#endif // TTLDRIVER_HPP
