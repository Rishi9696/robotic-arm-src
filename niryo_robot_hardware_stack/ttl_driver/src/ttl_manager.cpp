/*
    ttl_driver.cpp
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

#include "ttl_driver/ttl_manager.hpp"

// cpp
#include <utility>
#include <vector>
#include <set>
#include <string>
#include <algorithm>
#include <sstream>
#include <unordered_map>

// niryo
#include "common/model/hardware_type_enum.hpp"
#include "common/model/dxl_command_type_enum.hpp"
#include "common/model/tool_state.hpp"
#include "common/model/stepper_motor_state.hpp"
#include "common/model/conveyor_state.hpp"
#include "common/model/end_effector_state.hpp"

#include "ttl_driver/dxl_driver.hpp"
#include "ttl_driver/stepper_driver.hpp"
#include "ttl_driver/end_effector_driver.hpp"

#include "ttl_driver/stepper_reg.hpp"
#include "ttl_driver/end_effector_reg.hpp"



using ::std::shared_ptr;
using ::std::vector;
using ::std::string;
using ::std::ostringstream;
using ::std::to_string;
using ::std::set;

using ::common::model::EStepperCalibrationStatus;
using ::common::model::StepperMotorState;
using ::common::model::ConveyorState;
using ::common::model::EndEffectorState;
using ::common::model::JointState;
using ::common::model::EHardwareType;
using ::common::model::DxlMotorState;
using ::common::model::ToolState;
using ::common::model::HardwareTypeEnum;
using ::common::model::SynchronizeMotorCmd;
using ::common::model::SingleMotorCmd;
using ::common::model::EDxlCommandType;
using ::common::model::EHardwareType;
using ::common::model::EBusProtocol;

namespace ttl_driver
{
/**
 * @brief TtlManager::TtlManager
 */
TtlManager::TtlManager(ros::NodeHandle& nh) :
    _debug_error_message("TtlManager - No connection with Dynamixel motors has been made yet")
{
    ROS_DEBUG("TtlManager - ctor");

    init(nh);

    if (COMM_SUCCESS != setupCommunication())
        ROS_WARN("TtlManager - Dynamixel Communication Failed");
}

/**
 * @brief TtlManager::~TtlManager
 */
TtlManager::~TtlManager()
{
    // we use an "init()" in the ctor. Thus there should be some kind of "uninit" in the dtor
}

/**
 * @brief TtlManager::init
 * @return
 */
bool TtlManager::init(ros::NodeHandle& nh)
{
    // get params from rosparams
    nh.getParam("bus_params/uart_device_name", _device_name);
    nh.getParam("bus_params/baudrate", _uart_baudrate);

    _portHandler.reset(dynamixel::PortHandler::getPortHandler(_device_name.c_str()));
    _packetHandler.reset(dynamixel::PacketHandler::getPacketHandler(TTL_BUS_PROTOCOL_VERSION));

    ROS_DEBUG("TtlManager::init - Dxl : set port name (%s), baudrate(%d)", _device_name.c_str(), _uart_baudrate);

    // retrieve motor config
    vector<int> idList;
    vector<string> typeList;

    nh.getParam("motors_params/motor_id_list", idList);
    nh.getParam("motors_params/motor_type_list", typeList);

    // check that the two lists have the same size
    if (idList.size() != typeList.size())
        ROS_ERROR("TtlManager::init - wrong motors configuration. "
                  "Please check your configuration file motor_id_list, motor_type_list");

    // debug - display info
    ostringstream ss;
    ss << "[";
    for (size_t i = 0; i < idList.size() ; i++)
        ss << " id " << idList.at(i) << ": " << typeList.at(i) << ",";

    string motor_string_list = ss.str();
    motor_string_list.pop_back();  // remove last ","
    motor_string_list += "]";

    ROS_INFO("TtlManager::init - motor list: %s ", motor_string_list.c_str());

    // put everything in maps
    for (size_t i = 0; i < idList.size(); i++)
    {
        uint8_t id = static_cast<uint8_t>(idList.at(i));
        EHardwareType type = HardwareTypeEnum(typeList.at(i).c_str());

        if (_state_map.count(id))
        {
            ROS_ERROR("TtlManager::init - duplicate id %d. Please check your configuration file "
                      "(niryo_robot_hardware_stack/ttl_driver/config/motors_params.yaml)",
                      id);
        }
        else
        {
            if (EHardwareType::UNKNOWN != type)
                addHardwareComponent(type, id, EType::JOINT);
            else
                ROS_ERROR("TtlManager::init - unknown type %s. Please check your configuration file "
                          "(niryo_robot_hardware_stack/ttl_driver/config/motors_params.yaml)",
                          typeList.at(id).c_str());
        }
    }

    // display internal data for debug
    for (auto const &s : _state_map)
        ROS_DEBUG("TtlManager::init - State map: %d => %s", s.first, s.second->str().c_str());

    for (auto const &id : _ids_map)
    {
        string listOfId;
        for (auto const &i : id.second) listOfId += to_string(i) + " ";

        ROS_DEBUG("TtlManager::init - Id map: %s => %s", HardwareTypeEnum(id.first).toString().c_str(), listOfId.c_str());
    }

    for (auto const &d : _driver_map)
    {
        ROS_DEBUG("TtlManager::init - Driver map: %s => %s",
                  HardwareTypeEnum(d.first).toString().c_str(),
                  d.second->str().c_str());
    }

    return COMM_SUCCESS;
}

/**
 * @brief TtlManager::addHardwareComponent
 * @param type
 * @param id
 * @param type_used
 */
void TtlManager::addHardwareComponent(EHardwareType hardware_type, uint8_t id, EType type_used)
{
    ROS_DEBUG("TtlManager::addMotor - Add motor id: %d", id);

    // if not already instanciated
    if (!_state_map.count(id))
    {
      switch (hardware_type)
      {
        case EHardwareType::STEPPER:
          if (EType::CONVOYER == type_used)
              _state_map.insert(make_pair(id, std::make_shared<ConveyorState>(EBusProtocol::TTL, id)));
          else
              _state_map.insert(make_pair(id, std::make_shared<StepperMotorState>(EBusProtocol::TTL, id)));
          break;
        case EHardwareType::END_EFFECTOR:
            _state_map.insert(make_pair(id, std::make_shared<EndEffectorState>(id)));
          break;
        case EHardwareType::XC430:
        case EHardwareType::XL320:
        case EHardwareType::XL330:
        case EHardwareType::XL430:
          if (type_used == EType::TOOL)
              _state_map.insert(make_pair(id, std::make_shared<ToolState>("auto", hardware_type, id)));
          else
              _state_map.insert(make_pair(id, std::make_shared<DxlMotorState>(hardware_type, EBusProtocol::TTL, id)));
        break;
        default:
          ROS_WARN("Unknown hardware type: %d", static_cast<int>(hardware_type));
          break;
      }
    }

    // if not already instanciated
    if (!_ids_map.count(hardware_type))
    {
        _ids_map.insert(make_pair(hardware_type, vector<uint8_t>({id})));
    }
    else
    {
        _ids_map.at(hardware_type).push_back(id);
    }

    // if not already instanciated
    if (!_driver_map.count(hardware_type))
    {
        switch (hardware_type)
        {
            case EHardwareType::STEPPER:
                _driver_map.insert(make_pair(hardware_type, std::make_shared<StepperDriver<> >(_portHandler, _packetHandler)));
            break;
            case EHardwareType::XL430:
                _driver_map.insert(make_pair(hardware_type, std::make_shared<DxlDriver<XL430Reg> >(_portHandler, _packetHandler)));
            break;
            case EHardwareType::XC430:
                _driver_map.insert(make_pair(hardware_type, std::make_shared<DxlDriver<XC430Reg> >(_portHandler, _packetHandler)));
            break;
            case EHardwareType::XL320:
                _driver_map.insert(make_pair(hardware_type, std::make_shared<DxlDriver<XL320Reg> >(_portHandler, _packetHandler)));
            break;
            case EHardwareType::XL330:
                _driver_map.insert(make_pair(hardware_type, std::make_shared<DxlDriver<XL330Reg> >(_portHandler, _packetHandler)));
            break;
            case EHardwareType::END_EFFECTOR:
                _driver_map.insert(make_pair(hardware_type, std::make_shared<EndEffectorDriver<> >(_portHandler, _packetHandler)));
            break;
            default:
                ROS_ERROR("TtlManager - Unable to instanciate driver, unknown type");
            break;
        }
    }
}

/**
 * @brief TtlManager::changeId
 * @param motor_type
 * @param old_id
 * @param new_id
 * @return
 */
int TtlManager::changeId(common::model::EHardwareType motor_type, uint8_t old_id, uint8_t new_id)
{
    auto driver = std::dynamic_pointer_cast<AbstractMotorDriver>(_driver_map.at(motor_type));
    if (driver)
      return driver->changeId(old_id, new_id);

    return -1;
}

/**
 * @brief TtlManager::removeMotor
 * @param id
 */
void TtlManager::removeMotor(uint8_t id)
{
    ROS_DEBUG("TtlManager::removeMotor - Remove motor id: %d", id);

    if (_state_map.count(id) && _state_map.at(id))
    {
        EHardwareType type = _state_map.at(id)->getType();

        // std::remove to remove hypothetic duplicates too
        if (_ids_map.count(type))
        {
            auto& ids = _ids_map.at(type);
            ids.erase(std::remove(ids.begin(), ids.end(), id), ids.end());
            if (ids.empty())
            {
                _ids_map.erase(type);
            }
        }

        _state_map.erase(id);
    }

    _removed_motor_id_list.erase(std::remove(_removed_motor_id_list.begin(),
                                             _removed_motor_id_list.end(), id),
                                             _removed_motor_id_list.end());
}

/**
 * @brief TtlManager::setupCommunication
 * @return
 */
int TtlManager::setupCommunication()
{
    int ret = COMM_NOT_AVAILABLE;

    ROS_DEBUG("TtlManager::setupCommunication - initializing connection...");

    // Dxl bus setup
    if (_portHandler)
    {
        _debug_error_message.clear();

        // setup half-duplex direction GPIO
        // see schema http:// support.robotis.com/en/product/actuator/dynamixel_x/xl-series_main.htm
        if (!_portHandler->setupGpio())
        {
            ROS_ERROR("TtlManager::setupCommunication - Failed to setup direction GPIO pin "
                      "for Dynamixel half-duplex serial");
            _debug_error_message = "TtlManager -  Failed to setup direction GPIO pin "
                                   "for Dynamixel half-duplex serial";
            return TTL_FAIL_SETUP_GPIO;
        }

        // Open port
        if (!_portHandler->openPort())
        {
            ROS_ERROR("TtlManager::setupCommunication - Failed to open Uart port for Dynamixel bus");
            _debug_error_message = "TtlManager - Failed to open Uart port for Dynamixel bus";
            return TTL_FAIL_OPEN_PORT;
        }

        // Set baudrate
        if (!_portHandler->setBaudRate(_uart_baudrate))
        {
            ROS_ERROR("TtlManager::setupCommunication - Failed to set baudrate for Dynamixel bus");
            _debug_error_message = "TtlManager - Failed to set baudrate for Dynamixel bus";
            return TTL_FAIL_PORT_SET_BAUDRATE;
        }

        // wait a bit to be sure the connection is established
        ros::Duration(0.1).sleep();
        ret = COMM_SUCCESS;
    }
    else
        ROS_ERROR("TtlManager::setupCommunication - Invalid port handler");

    // TODO(Thuc): set up for stepper bus
    return ret;
}

// ****************
//  commands
// ****************

/**
 * @brief TtlManager::scanAndCheck
 * @return
 */
int TtlManager::scanAndCheck()
{
    ROS_DEBUG("TtlManager::scanAndCheck");
    int result = COMM_PORT_BUSY;

    _all_motor_connected.clear();
    _is_connection_ok = false;

    for (int counter = 0; counter < 50 && COMM_SUCCESS != result; ++counter)
    {
        result = getAllIdsOnBus(_all_motor_connected);
        ROS_DEBUG_COND(COMM_SUCCESS != result, "TtlManager::scanAndCheck status: %d (counter: %d)", result, counter);
        ros::Duration(TIME_TO_WAIT_IF_BUSY).sleep();
    }

    if (COMM_SUCCESS == result)
    {
        checkRemovedMotors();

        if (_removed_motor_id_list.empty())
        {
            _is_connection_ok = true;
            _debug_error_message = "";
            result = TTL_SCAN_OK;
        }
        else
        {
            _debug_error_message = "Motor(s):";
            for (auto const &id : _removed_motor_id_list)
            {
                _debug_error_message += " " + to_string(id);
            }
            _debug_error_message += " do not seem to be connected";
        }
    }
    else
    {
        _debug_error_message = "TtlManager - Failed to scan motors, physical bus is too busy. Will retry...";
        ROS_WARN_THROTTLE(1, "TtlManager::scanAndCheck - Failed to scan motors, physical bus is too busy");
    }

    return result;
}

/**
 * @brief TtlManager::ping
 * @param id
 * @return
 * The ping method is identical to all drivers, we can just use the first one
 * (same behaviour in getAllIdsOnBus with scan method)
 */
bool TtlManager::ping(uint8_t id)
{
    int result = false;

    auto it = _driver_map.begin();
    if (it != _driver_map.end() && it->second)
    {
        result = (COMM_SUCCESS == it->second->ping(id));
    }
    else
        ROS_ERROR_THROTTLE(1, "TtlManager::ping - the dynamixel drivers seeems uninitialized");

    return result;
}

/**
 * @brief TtlManager::rebootMotors
 * @return
 */
int TtlManager::rebootMotors()
{
    int return_value = niryo_robot_msgs::CommandStatus::FAILURE;

    for (auto const &it : _state_map)
    {
        EHardwareType type = it.second->getType();
        ROS_DEBUG("TtlManager::rebootMotors - Reboot Dxl motor with ID: %d", it.first);
        if (_driver_map.count(type))
        {
            int result = _driver_map.at(type)->reboot(it.first);
            if (COMM_SUCCESS == result)
            {
                ROS_DEBUG("TtlManager::rebootMotors - Reboot motor successfull");
                return_value = niryo_robot_msgs::CommandStatus::SUCCESS;
            }
            else
            {
                ROS_WARN("TtlManager::rebootMotors - Failed to reboot motor: %d", result);
                return_value = result;
            }
        }
    }

    return return_value;
}

/**
 * @brief TtlManager::rebootMotor
 * @param motor_id
 * @return
 */
int TtlManager::rebootMotor(uint8_t motor_id)
{
    int return_value = COMM_TX_FAIL;

    if (_state_map.count(motor_id) && _state_map.at(motor_id))
    {
        EHardwareType type = _state_map.at(motor_id)->getType();
        ROS_DEBUG("TtlManager::rebootMotors - Reboot motor with ID: %d", motor_id);
        if (_driver_map.count(type))
        {
            return_value = _driver_map.at(type)->reboot(motor_id);
            if (COMM_SUCCESS == return_value)
            {
                ros::Time start_time = ros::Time::now();
                uint32_t tmp = 0;
                int wait_result = _driver_map.at(type)->readTemperature(motor_id, tmp);
                while (COMM_SUCCESS != wait_result || !tmp)
                {
                    if ((ros::Time::now() - start_time).toSec() > 1)
                        break;
                    ros::Duration(0.1).sleep();
                    wait_result = _driver_map.at(type)->readTemperature(motor_id, tmp);
                }
            }
            ROS_WARN_COND(COMM_SUCCESS != return_value,
                          "TtlManager::rebootMotors - Failed to reboot motor: %d",
                          return_value);
        }
    }

    return return_value;
}

// ******************
//  Read operations
// ******************

/**
 * @brief TtlManager::getPosition
 * @param motor_state
 * @return
 */
uint32_t TtlManager::getPosition(JointState &motor_state)
{
    uint32_t position = 0;
    EHardwareType hardware_type = motor_state.getType();
    if (_driver_map.count(hardware_type) && _driver_map.at(hardware_type))
    {
        for (_hw_fail_counter_read = 0; _hw_fail_counter_read < MAX_HW_FAILURE; ++_hw_fail_counter_read)
        {
            auto driver = std::dynamic_pointer_cast<AbstractMotorDriver>(_driver_map.at(hardware_type));

            if (driver && COMM_SUCCESS == driver->readPosition(motor_state.getId(), position))
            {
                _hw_fail_counter_read = 0;
                break;
            }
        }

        if (0 < _hw_fail_counter_read)
        {
            ROS_ERROR_THROTTLE(1, "TtlManager::getPosition - motor connection problem - Failed to read from bus");
            _debug_error_message = "TtlManager - Connection problem with Bus.";
            _hw_fail_counter_read = 0;
            _is_connection_ok = false;
        }
    }
    else
    {
        ROS_ERROR_THROTTLE(1, "TtlManager::getPosition - Driver not found for requested motor id");
        _debug_error_message = "TtlManager::getPosition - Driver not found for requested motor id";
    }
    return position;
}

/**
 * @brief TtlManager::readPositionState
 */
bool TtlManager::readPositionStatus()
{
    bool res = false;
    unsigned int hw_errors_increment = 0;

    // syncread from all drivers for all motors
    for (auto const& it : _driver_map)
    {
        EHardwareType type = it.first;
        shared_ptr<AbstractMotorDriver> driver = std::dynamic_pointer_cast<AbstractMotorDriver>(it.second);

        if (driver && _ids_map.count(type))
        {
            // we retrieve all the associated id for the type of the current driver
            vector<uint8_t> id_list = _ids_map.at(type);
            vector<uint32_t> position_list;

            if (COMM_SUCCESS == driver->syncReadPosition(id_list, position_list))
            {
                if (id_list.size() == position_list.size())
                {
                    // set motors states accordingly
                    for (size_t i = 0; i < id_list.size(); ++i)
                    {
                        uint8_t id = id_list.at(i);
                        int position = static_cast<int>(position_list.at(i));

                        if (_state_map.count(id))
                        {
                            auto state = std::dynamic_pointer_cast<common::model::AbstractMotorState>(_state_map.at(id));
                            if (state)
                            {
                                state->setPositionState(position);
                            }
                        }
                    }
                }
                else
                {
                    ROS_ERROR("TtlManager::readPositionStatus : Fail to sync read position - "
                                "vector mismatch (id_list size %d, position_list size %d)",
                                static_cast<int>(id_list.size()),
                                static_cast<int>(position_list.size()));
                    hw_errors_increment++;
                }
            }
            else
            {
                hw_errors_increment++;
            }
        }
    }  // for driver_map

    // we reset the global error variable only if no errors
    if (0 == hw_errors_increment)
    {
        _hw_fail_counter_read = 0;
        res = true;
    }
    else
    {
        _hw_fail_counter_read += hw_errors_increment;
    }

    if (_hw_fail_counter_read > MAX_HW_FAILURE)
    {
        ROS_ERROR_THROTTLE(1, "TtlManager::readPositionStatus - motor connection problem - "
                                "Failed to read from bus");
        _hw_fail_counter_read = 0;
        _is_connection_ok = false;
        _debug_error_message = "TtlManager - Connection problem with physical Bus.";
    }

    return res;
}

/**
 * @brief TtlManager::readEndEffectorStatus
 */
bool TtlManager::readEndEffectorStatus()
{
    bool res = false;

    // if has end effector driver
    if (_driver_map.count(EHardwareType::END_EFFECTOR))
    {
        unsigned int hw_errors_increment = 0;

        shared_ptr<EndEffectorDriver<EndEffectorReg> > driver = std::dynamic_pointer_cast<EndEffectorDriver<EndEffectorReg> >(_driver_map.at(EHardwareType::END_EFFECTOR));

        if (driver && _ids_map.count(EHardwareType::END_EFFECTOR))
        {
            // we retrieve the associated id for the end effector
            uint8_t id = _ids_map.at(EHardwareType::END_EFFECTOR).front();
            uint32_t value = 0;

            if (_state_map.count(id))
            {
                auto state = std::dynamic_pointer_cast<EndEffectorState>(_state_map.at(id));
                if (state)
                {
                    // free drive button
                    if (COMM_SUCCESS == driver->readButton1Status(id, value))
                    {
                        EndEffectorState::EActionType action = driver->interpreteActionValue(value);
                        state->setButtonStatus(1, action);
                    }
                    else
                    {
                        hw_errors_increment++;
                    }

                    // save pos button
                    if (COMM_SUCCESS == driver->readButton2Status(id, value))
                    {
                        EndEffectorState::EActionType action = driver->interpreteActionValue(value);
                        state->setButtonStatus(2, action);
                    }
                    else
                    {
                        hw_errors_increment++;
                    }

                    // custom button
                    if (COMM_SUCCESS == driver->readButton3Status(id, value))
                    {
                        EndEffectorState::EActionType action = driver->interpreteActionValue(value);
                        state->setButtonStatus(3, action);
                    }
                    else
                    {
                        hw_errors_increment++;
                    }
                }
            }
        }  // for driver_map

        // we reset the global error variable only if no errors
        if (0 == hw_errors_increment)
        {
            _hw_fail_counter_read = 0;
            res = true;
        }
        else
        {
            _hw_fail_counter_read += hw_errors_increment;
        }

        if (_hw_fail_counter_read > MAX_HW_FAILURE)
        {
            ROS_ERROR_THROTTLE(1, "TtlManager::readEndEffectorStatus - motor connection problem - "
                                  "Failed to read from bus");
            _hw_fail_counter_read = 0;
            _debug_error_message = "TtlManager - Connection problem with physical Bus.";
        }
    }
    else
    {
        ROS_DEBUG_THROTTLE(2, "TtlManager::readEndEffectorStatus - No end effector found");
    }

    return res;
}

/**
 * @brief TtlManager::readHwStatus
 */
bool TtlManager::readHwStatus()
{
    bool res = false;

    unsigned int hw_errors_increment = 0;

    // syncread from all drivers for all motors
    for (auto const& it : _driver_map)
    {
        EHardwareType type = it.first;
        shared_ptr<AbstractTtlDriver> driver = it.second;

        if (driver && _ids_map.count(type))
        {
            // we retrieve all the associated id for the type of the current driver
            vector<uint8_t> id_list = _ids_map.at(type);
            size_t id_list_size = id_list.size();

            // **************  temperature
            vector<uint32_t> temperature_list;

            if (COMM_SUCCESS != driver->syncReadTemperature(id_list, temperature_list))
            {
                // this operation can fail, it is normal, so no error message
                hw_errors_increment++;
            }
            else if (id_list_size != temperature_list.size())
            {
                // however, if we have a mismatch here, it is not normal

                ROS_ERROR("TtlManager::readHwStatus : syncReadTemperature failed - "
                            "vector mistmatch (id_list size %d, temperature_list size %d)",
                            static_cast<int>(id_list_size), static_cast<int>(temperature_list.size()));

                hw_errors_increment++;
            }

            // **********  voltage
            vector<uint32_t> voltage_list;

            if (COMM_SUCCESS != driver->syncReadVoltage(id_list, voltage_list))
            {
                hw_errors_increment++;
            }
            else if (id_list_size != voltage_list.size())
            {
                ROS_ERROR("TtlManager::readHwStatus : syncReadTemperature failed - "
                            "vector mistmatch (id_list size %d, voltage_list size %d)",
                            static_cast<int>(id_list_size), static_cast<int>(voltage_list.size()));

                hw_errors_increment++;
            }

            // **********  error state
            vector<uint32_t> hw_status_list;

            if (COMM_SUCCESS != driver->syncReadHwErrorStatus(id_list, hw_status_list))
            {
                hw_errors_increment++;
            }
            else if (id_list_size != hw_status_list.size())
            {
                ROS_ERROR("TtlManager::readHwStatus : syncReadTemperature failed - "
                            "vector mistmatch (id_list size %d, hw_status_list size %d)",
                            static_cast<int>(id_list_size), static_cast<int>(hw_status_list.size()));

                hw_errors_increment++;
            }

            // **********  conveyor state
            if (type == EHardwareType::STEPPER)
            {
                for (auto id : _ids_map.at(type))
                {
                    if (std::dynamic_pointer_cast<StepperMotorState>(_state_map.at(id))->isConveyor())
                    {
                        shared_ptr<ttl_driver::AbstractStepperDriver> stepper_driver = std::dynamic_pointer_cast<ttl_driver::AbstractStepperDriver>(driver);
                        bool state;
                        uint32_t speed;
                        int8_t direction;
                        if (COMM_SUCCESS != stepper_driver->readConveyorSpeed(id, speed))
                        {
                            hw_errors_increment++;
                        }
                        if (COMM_SUCCESS != stepper_driver->readConveyorDirection(id, direction))
                        {
                            hw_errors_increment++;
                        }
                        if (COMM_SUCCESS != stepper_driver->readConveyorState(id, state))
                        {
                            hw_errors_increment++;
                        }
                        auto cState = std::dynamic_pointer_cast<ConveyorState>(_state_map.at(id));
                        // TODO(thuc): handle datas before set in state of conveyor - type data in ttl conveyor is different with data in can conveyor
                        cState->setDirection(direction);
                        cState->setSpeed(speed);
                        cState->setState(state);
                    }
                }
            }
            // set motors states accordingly
            for (size_t i = 0; i < id_list_size; ++i)
            {
                uint8_t id = id_list.at(i);

                if (_state_map.count(id))
                {
                    auto State = _state_map.at(id);

                    // **************  temperature
                    if (temperature_list.size() > i)
                    {
                        State->setTemperatureState(static_cast<int>(temperature_list.at(i)));
                    }

                    // **********  voltage
                    if (voltage_list.size() > i)
                    {
                        State->setVoltageState(static_cast<int>(voltage_list.at(i)));
                    }

                    // **********  error state
                    if (hw_status_list.size() > i)
                    {
                        State->setHardwareError(static_cast<int>(hw_status_list.at(i)));
                        string hardware_message = driver->interpreteErrorState(hw_status_list.at(i));
                        State->setHardwareError(hardware_message);
                    }
                }
            }  // for id_list
        }
    }  // for driver_map

    // we reset the global error variable only if no errors
    if (0 == hw_errors_increment)
    {
        _hw_fail_counter_read = 0;
        res = true;
    }
    else
    {
        _hw_fail_counter_read += hw_errors_increment;
    }

    // if too much errors, disconnect
    if (_hw_fail_counter_read > MAX_HW_FAILURE )
    {
        ROS_ERROR_THROTTLE(1, "TtlManager::readHwStatus - motor connection problem - Failed to read from physical bus");
        _hw_fail_counter_read = 0;
        res = false;
        _is_connection_ok = false;
        _debug_error_message = "TtlManager - Connection problem with physical Bus.";
    }


    return res;
}

/**
 * @brief TtlManager::getAllIdsOnDxlBus
 * @param id_list
 * @return
 * The scan method is identical to all drivers, we can just use the first one
 * (same behaviour in ping with ping method)
 */
int TtlManager::getAllIdsOnBus(vector<uint8_t> &id_list)
{
    int result = COMM_RX_FAIL;

    // 1. Get all ids from ttl bus. We can use any driver for that
    auto it = _driver_map.begin();
    if (it != _driver_map.end() && it->second)
    {
        result = it->second->scan(id_list);

        string ids_str;
        for (auto const &id : id_list)
            ids_str += to_string(id) + " ";

        ROS_DEBUG_THROTTLE(1, "TtlManager::getAllIdsOnDxlBus - Found ids (%s) on bus using first driver (type: %s)",
                              ids_str.c_str(),
                              HardwareTypeEnum(it->first).toString().c_str());

        if (COMM_SUCCESS != result)
        {
            if (COMM_RX_TIMEOUT != result)
            {  // -3001
                _debug_error_message = "TtlManager - No motor found. "
                                       "Make sure that motors are correctly connected and powered on.";
            }
            else
            {  // -3002 or other
                _debug_error_message = "TtlManager - Failed to scan bus.";
            }
            ROS_WARN_THROTTLE(1, "TtlManager::getAllIdsOnDxlBus - Broadcast ping failed, "
                              "result : %d (-3001: timeout, -3002: corrupted packet)",
                              result);
        }
    }

    return result;
}

/**
 * @brief TtlManager::startCalibration
 */
void TtlManager::startCalibration()
{
    ROS_DEBUG("TtlManager::startCalibration: starting...");

    for (auto const& s : _state_map)
    {
        if (s.second && EHardwareType::STEPPER == s.second->getType() && !std::dynamic_pointer_cast<StepperMotorState>(s.second)->isConveyor())
            std::dynamic_pointer_cast<StepperMotorState>(s.second)->setCalibration(EStepperCalibrationStatus::CALIBRATION_IN_PROGRESS, 0);
    }

    _calibration_status = EStepperCalibrationStatus::CALIBRATION_IN_PROGRESS;
}

/**
 * @brief TtlManager::resetCalibration
 */
void TtlManager::resetCalibration()
{
    ROS_DEBUG("TtlManager::resetCalibration: reseting...");

    _calibration_status = EStepperCalibrationStatus::CALIBRATION_UNINITIALIZED;
}

/**
 * @brief TtlManager::isCalibrationInProgress
 * @return
 */
bool TtlManager::isCalibrationInProgress() const {
    return common::model::EStepperCalibrationStatus::CALIBRATION_IN_PROGRESS == _calibration_status;
}

/**
 * @brief TtlManager::getCalibrationResult
 * @param motor_id
 * @return
 */
int32_t TtlManager::getCalibrationResult(uint8_t motor_id) const
{
    if (!_state_map.count(motor_id) && _state_map.at(motor_id))
        throw std::out_of_range("TtlManager::getMotorsState: Unknown motor id");

    return std::dynamic_pointer_cast<StepperMotorState>(_state_map.at(motor_id))->getCalibrationValue();
}

/**
 * @brief TtlManager::getCalibrationStatus
 * @return
 */
common::model::EStepperCalibrationStatus TtlManager::getCalibrationStatus() const
{
    return _calibration_status;
}
// ******************
//  Write operations
// ******************

/**
 * @brief TtlManager::setLeds
 * @param led
 * @return
 */
int TtlManager::setLeds(int led)
{
    int ret = niryo_robot_msgs::CommandStatus::TTL_WRITE_ERROR;
    _led_state = led;
    // TODO(CC) retrieve type from register
    EHardwareType mType = common::model::EHardwareType::XL320;

    // get list of motors of the given type
    vector<uint8_t> id_list;
    if (_ids_map.count(mType) && _driver_map.count(mType))
    {
        id_list = _ids_map.at(mType);

        auto driver = std::dynamic_pointer_cast<AbstractDxlDriver>(_driver_map.at(mType));

        // sync write led state
        vector<uint32_t> command_led_id(id_list.size(), static_cast<uint32_t>(led));
        if (0 <= led && 7 >= led)
        {
            int result = COMM_TX_FAIL;
            for (int error_counter = 0; result != COMM_SUCCESS && error_counter < 5; ++error_counter)
            {
                ros::Duration(TIME_TO_WAIT_IF_BUSY).sleep();
                result = driver->syncWriteLed(id_list, command_led_id);
            }

            if (COMM_SUCCESS == result)
                ret = niryo_robot_msgs::CommandStatus::SUCCESS;
            else
                ROS_WARN("TtlManager::setLeds - Failed to write LED");
        }
    }

    return ret;
}

/**
 * @brief TtlManager::sendCustomCommand
 * @param motor_type
 * @param id
 * @param reg_address
 * @param value
 * @param byte_number
 * @return
 */
int TtlManager::sendCustomCommand(EHardwareType motor_type, uint8_t id,
                                 int reg_address, int value,  int byte_number)
{
    int result = COMM_TX_FAIL;
    ROS_DEBUG("TtlManager::sendCustomCommand:\n"
              "\t\t Motor type: %d, ID: %d, Value: %d, Address: %d, Size: %d",
              static_cast<int>(motor_type), static_cast<int>(id), value,
              reg_address, byte_number);

    if (_driver_map.count(motor_type) && _driver_map.at(motor_type))
    {
        result = _driver_map.at(motor_type)->write(static_cast<uint8_t>(reg_address),
                                                   static_cast<uint8_t>(byte_number),
                                                   id,
                                                   static_cast<uint32_t>(value));
        if (result != COMM_SUCCESS)
        {
            ROS_WARN("TtlManager::sendCustomCommand - Failed to write custom command: %d", result);
            // TODO(Thuc): change TTL_WRITE_ERROR -> WRITE_ERROR
            result = niryo_robot_msgs::CommandStatus::TTL_WRITE_ERROR;
        }
    }
    else
    {
        ROS_ERROR_THROTTLE(1, "TtlManager::sendCustomCommand - driver for motor %s not available",
                           HardwareTypeEnum(motor_type).toString().c_str());
        result = niryo_robot_msgs::CommandStatus::WRONG_MOTOR_TYPE;
    }
    ros::Duration(0.005).sleep();
    return result;
}

/**
 * @brief TtlManager::readCustomCommand
 * @param motor_type
 * @param id
 * @param reg_address
 * @param value
 * @param byte_number
 * @return
 */
int TtlManager::readCustomCommand(EHardwareType motor_type, uint8_t id,
                                 int32_t reg_address, int& value, int byte_number)
{
    int result = COMM_RX_FAIL;
    ROS_DEBUG("TtlManager::readCustomCommand: Motor type: %d, ID: %d, Address: %d, Size: %d",
              static_cast<int>(motor_type), static_cast<int>(id),
              static_cast<int>(reg_address), byte_number);

    if (_driver_map.count(motor_type) && _driver_map.at(motor_type))
    {
        uint32_t data = 0;
        result = _driver_map.at(motor_type)->read(static_cast<uint8_t>(reg_address),
                                                  static_cast<uint8_t>(byte_number),
                                                  id,
                                                  data);
        value = static_cast<int>(data);

        if (result != COMM_SUCCESS)
        {
            ROS_WARN("TtlManager::readCustomCommand - Failed to read custom command: %d", result);
            result = niryo_robot_msgs::CommandStatus::TTL_READ_ERROR;
        }
    }
    else
    {
        ROS_ERROR_THROTTLE(1, "TtlManager::readCustomCommand - driver for motor %s not available",
                           HardwareTypeEnum(motor_type).toString().c_str());
        result = niryo_robot_msgs::CommandStatus::WRONG_MOTOR_TYPE;
    }
    ros::Duration(0.005).sleep();
    return result;
}

// ********************
//  Private
// ********************

/**
 * @brief TtlManager::checkRemovedMotors
 */
void TtlManager::checkRemovedMotors()
{
    // get list of ids
    std::vector<uint8_t> motor_list;
    for (auto const& istate : _state_map)
    {
        auto it = find(_all_motor_connected.begin(), _all_motor_connected.end(), istate.first);
        if (it == _all_motor_connected.end())
            motor_list.emplace_back(istate.first);
    }
    _removed_motor_id_list = motor_list;
}

/**
 * @brief TtlManager::getMotorsStates
 * @return only the joints states
 */
std::vector<std::shared_ptr<JointState> >
TtlManager::getMotorsStates() const
{
    std::vector<std::shared_ptr<JointState> > states;
    for (auto it : _state_map)
    {
      if (EHardwareType::UNKNOWN != it.second->getType() && EHardwareType::END_EFFECTOR != it.second->getType())
      {
          states.push_back(std::dynamic_pointer_cast<JointState>(it.second));
      }
    }

    return states;
}

/**
 * @brief TtlManager::executeJointTrajectoryCmd
 * @param cmd_vec
 */
void TtlManager::executeJointTrajectoryCmd(std::vector<std::pair<uint8_t, uint32_t> > cmd_vec)
{
    for (auto const& it : _driver_map)
    {
        // build list of ids and params for this motor
        std::vector<uint8_t> ids;
        std::vector<uint32_t> params;
        for (auto const& cmd : cmd_vec)
        {
            if (_state_map.count(cmd.first) && it.first == _state_map.at(cmd.first)->getType())
            {
                ids.emplace_back(cmd.first);
                params.emplace_back(cmd.second);
            }
        }

        // syncwrite for this driver. The driver is responsible for sync write only to its associated motors
        auto driver = std::dynamic_pointer_cast<AbstractMotorDriver>(it.second);

        // if successful, don't process this driver in the next loop
        if (!driver || COMM_SUCCESS != driver->syncWritePositionGoal(ids, params))
        {
          ROS_WARN("TtlManager::executeJointTrajectoryCmd - Failed to write position");
          _debug_error_message = "TtlManager - Failed to write position";
        }
    }
}

/**
 * @brief TtlManager::writeSynchronizeCommand
 * @param cmd
 * @return
 */
int TtlManager::writeSynchronizeCommand(const std::shared_ptr<common::model::AbstractTtlSynchronizeMotorCmd>& cmd)
{
    int result = COMM_TX_ERROR;
    ROS_DEBUG_THROTTLE(0.5, "TtlManager::writeSynchronizeCommand:  %s", cmd->str().c_str());

    if (cmd->isValid())
    {
        std::set<common::model::EHardwareType> typesToProcess = cmd->getMotorTypes();

        // process all the motors using each successive drivers
        for (int counter = 0; counter < MAX_HW_FAILURE; ++counter)
        {
            ROS_DEBUG_THROTTLE(0.5, "TtlManager::writeSynchronizeCommand: try to sync write (counter %d)", counter);

            for (auto const& it : _driver_map)
            {
                if (typesToProcess.count(it.first) != 0)
                {
                    result = COMM_TX_ERROR;

                    // syncwrite for this driver. The driver is responsible for sync write only to its associated motors
                    auto driver = std::dynamic_pointer_cast<AbstractMotorDriver>(it.second);
                    if (driver)
                    {
                      result = driver->writeSyncCmd(cmd->getCmdType(),
                                                    cmd->getMotorsId(it.first),
                                                    cmd->getParams(it.first));

                      ros::Duration(0.05).sleep();
                    }

                    // if successful, don't process this driver in the next loop
                    if (COMM_SUCCESS == result)
                    {
                        typesToProcess.erase(typesToProcess.find(it.first));
                    }
                    else
                    {
                        ROS_ERROR("TtlManager::writeSynchronizeCommand : unable to sync write function : %d", result);
                    }
                }
            }

            // if all drivers are processed, go out of for loop
            if (typesToProcess.empty())
            {
                result = COMM_SUCCESS;
                break;
            }

            ros::Duration(TIME_TO_WAIT_IF_BUSY).sleep();
        }
    }
    else
    {
        ROS_ERROR("TtlManager::writeSynchronizeCommand - Invalid command");
    }


    if (COMM_SUCCESS != result)
    {
        ROS_ERROR_THROTTLE(0.5, "TtlManager::writeSynchronizeCommand - Failed to write synchronize position");
        _debug_error_message = "TtlManager - Failed to write synchronize position";
    }

    return result;
}

/**
 * @brief TtlManager::writeSingleCommand
 * @param cmd
 * @return
 */
int TtlManager::writeSingleCommand(const std::shared_ptr<common::model::AbstractTtlSingleMotorCmd >& cmd)
{
    int result = COMM_TX_ERROR;

    uint8_t id = cmd->getId();

    if (cmd->isValid())
    {
        int counter = 0;

        ROS_DEBUG_THROTTLE(0.5, "TtlManager::writeSingleCommand:  %s", cmd->str().c_str());

        if (_state_map.count(id) != 0)
        {
            auto state = _state_map.at(id);

            while ((COMM_SUCCESS != result) && (counter < 50))
            {
                common::model::EHardwareType hardware_type = state->getType();
                result = COMM_TX_ERROR;

                if (_driver_map.count(hardware_type) && _driver_map.at(hardware_type))
                {
                      result = _driver_map.at(hardware_type)->writeSingleCmd(cmd);
                }

                counter += 1;

                ros::Duration(TIME_TO_WAIT_IF_BUSY).sleep();
            }
        }
    }

    if (result != COMM_SUCCESS)
    {
        ROS_WARN("TtlManager::writeSingleCommand - Failed to write a single command on motor id : %d", id);
        _debug_error_message = "TtlManager - Failed to write a single command";
    }

    return result;
}

}  // namespace ttl_driver
