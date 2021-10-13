/*
    hardware_interface.cpp
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

#include <functional>
#include <string>
#include <vector>

#include "niryo_robot_hardware_interface/hardware_interface.hpp"

#include "common/model/hardware_type_enum.hpp"

#include "common/util/util_defs.hpp"


using ::common::model::EBusProtocol;

namespace niryo_robot_hardware_interface
{
/**
 * @brief HardwareInterface::HardwareInterface
 * @param nh
 */
HardwareInterface::HardwareInterface(ros::NodeHandle &nh) :
    _nh(nh)
{
    /*for(int i = 0; i < 30; ++i)
    {
      ros::Duration(1).sleep();
      ROS_WARN("sleeping for %d", i);
    }*/
    init(nh);
}

/**
 * @brief HardwareInterface::init
 * @param nh
 * @return
 */
bool HardwareInterface::init(ros::NodeHandle &nh)
{
    ROS_DEBUG("HardwareInterface::init - Initializing parameters...");
    initParameters(nh);

    ROS_DEBUG("HardwareInterface::init - Init Nodes...");
    initNodes(nh);

    ROS_DEBUG("HardwareInterface::init - Starting services...");
    startServices(nh);

    ROS_DEBUG("HardwareInterface::init - Starting publishers...");
    startPublishers(nh);

    ROS_DEBUG("HardwareInterface::init - Starting subscribers...");
    startSubscribers(nh);

    return true;
}

/**
 * @brief HardwareInterface::initParameters
 * @param nh
 */
void HardwareInterface::initParameters(ros::NodeHandle &nh)
{
    double hw_status_frequency{1.0};
    double sw_version_frequency{1.0};
    nh.getParam("publish_hw_status_frequency", hw_status_frequency);
    nh.getParam("publish_software_version_frequency", sw_version_frequency);

    ROS_DEBUG("HardwareInterface::initParameters - publish_hw_status_frequency : %f",
              hw_status_frequency);
    ROS_DEBUG("HardwareInterface::initParameters - publish_software_version_frequency : %f",
                            sw_version_frequency);

    assert(hw_status_frequency);
    assert(sw_version_frequency);

    _hw_status_publisher_duration = ros::Duration(1.0 / hw_status_frequency);
    _sw_version_publisher_duration = ros::Duration(1.0 / sw_version_frequency);

    nh.getParam("/niryo_robot/info/image_version", _rpi_image_version);
    nh.getParam("/niryo_robot/info/ros_version", _ros_niryo_robot_version);
    nh.getParam("hardware_version", _hardware_version);

    nh.getParam("gazebo", _gazebo);

    nh.getParam("can_enabled", _can_enabled);
    nh.getParam("ttl_enabled", _ttl_enabled);

    _rpi_image_version.erase(_rpi_image_version.find_last_not_of(" \n\r\t") + 1);
    _ros_niryo_robot_version.erase(_ros_niryo_robot_version.find_last_not_of(" \n\r\t") + 1);

    std::string conveyor_bus_str = "can";
    nh.getParam("conveyor/bus", conveyor_bus_str);
    if ("can" == conveyor_bus_str)
        _conveyor_bus = EBusProtocol::CAN;
    else if ("ttl" == conveyor_bus_str)
        _conveyor_bus = EBusProtocol::TTL;
    else
        _conveyor_bus = EBusProtocol::UNKNOWN;

    // end effector is enabled if an id is defined
    _end_effector_enabled = nh.hasParam("end_effector_interface/end_effector_id");

    ROS_DEBUG("HardwareInterface::initParameters - image_version : %s",
                            _rpi_image_version.c_str());
    ROS_DEBUG("HardwareInterface::initParameters - ros_version : %s",
                            _ros_niryo_robot_version.c_str());

    ROS_DEBUG("HardwareInterface::initParameters - gazebo : %s", _gazebo ? "true" : "false");

    ROS_DEBUG("HardwareInterface::initParameters - can_enabled : %s", _can_enabled ? "true" : "false");
    ROS_DEBUG("HardwareInterface::initParameters - ttl_enabled : %s", _ttl_enabled ? "true" : "false");
    ROS_DEBUG("HardwareInterface::initParameters - end_effector_enabled : %s", _end_effector_enabled ? "true" : "false");
}

/**
 * @brief HardwareInterface::initNodes
 * @param nh
 */
void HardwareInterface::initNodes(ros::NodeHandle &nh)
{
    ROS_DEBUG("HardwareInterface::initNodes - Init Nodes");

    if (_ttl_enabled)
    {
        ROS_DEBUG("HardwareInterface::initNodes - Start Dynamixel Driver Node");
        ros::NodeHandle nh_ttl(nh, "ttl_driver");
        _ttl_interface = std::make_shared<ttl_driver::TtlInterfaceCore>(nh_ttl);
        ros::Duration(0.25).sleep();

        ROS_DEBUG("HardwareInterface::initNodes - Start Tools Interface Node");
        ros::NodeHandle nh_tool(nh, "tools_interface");
        _tools_interface = std::make_shared<tools_interface::ToolsInterfaceCore>(nh_tool,
                                                                                 _ttl_interface);
        ros::Duration(0.25).sleep();

        if (_end_effector_enabled)
        {
            ROS_DEBUG("HardwareInterface::initNodes - Start End Effector Interface Node");
            ros::NodeHandle nh_ee(nh, "end_effector_interface");
            _end_effector_interface = std::make_shared<end_effector_interface::EndEffectorInterfaceCore>(nh_ee,
                                                                                                         _ttl_interface);
        }
        ros::Duration(0.25).sleep();
    }
    else
    {
        ROS_WARN("HardwareInterface::initNodes - DXL communication is disabled for debug purposes");
    }

    if (_can_enabled)
    {
        ROS_DEBUG("HardwareInterface::initNodes - Start CAN Driver Node");
        ros::NodeHandle nh_can(nh, "can_driver");
        _can_interface = std::make_shared<can_driver::CanInterfaceCore>(nh_can);
        ros::Duration(0.25).sleep();
    }
    else
    {
        ROS_DEBUG("HardwareInterface::initNodes - CAN communication is disabled for debug purposes");
    }

    ROS_DEBUG("HardwareInterface::initNodes - Start Conveyor Interface Node");
    ros::NodeHandle nh_conveyor(nh, "conveyor");
    _conveyor_interface = std::make_shared<conveyor_interface::ConveyorInterfaceCore>(nh_conveyor,
                                                                                      _ttl_interface, _can_interface);
    ros::Duration(0.25).sleep();

    ROS_DEBUG("HardwareInterface::initNodes - Start Joints Interface Node");
    ros::NodeHandle nh_joints(nh, "joints_interface");
    _joints_interface = std::make_shared<joints_interface::JointsInterfaceCore>(nh,
                                                                                nh_joints,
                                                                                _ttl_interface,
                                                                                _can_interface);
    ros::Duration(0.25).sleep();

    ROS_DEBUG("HardwareInterface::initNodes - Start CPU Interface Node");
    ros::NodeHandle nh_cpu(nh, "cpu_interface");
    _cpu_interface = std::make_shared<cpu_interface::CpuInterfaceCore>(nh_cpu);
    ros::Duration(0.25).sleep();
}

/**
 * @brief HardwareInterface::startServices
 * @param nh
 */
void HardwareInterface::startServices(ros::NodeHandle& nh)
{
    _motors_report_service = nh.advertiseService("/niryo_robot_hardware_interface/launch_motors_report",
                                                  &HardwareInterface::_callbackLaunchMotorsReport, this);

    _stop_motors_report_service = nh.advertiseService("/niryo_robot_hardware_interface/stop_motors_report",
                                                       &HardwareInterface::_callbackStopMotorsReport, this);

    _reboot_motors_service = nh.advertiseService("/niryo_robot_hardware_interface/reboot_motors",
                                                  &HardwareInterface::_callbackRebootMotors, this);
}

/**
 * @brief HardwareInterface::startPublishers
 */
void HardwareInterface::startPublishers(ros::NodeHandle &nh)
{
    _hw_status_publisher = nh.advertise<niryo_robot_msgs::HardwareStatus>(
                                            "/niryo_robot_hardware_interface/hardware_status", 10);

    _hw_status_publisher_timer = nh.createTimer(_hw_status_publisher_duration,
                                                &HardwareInterface::_publishHardwareStatus,
                                                this);

    _sw_version_publisher = nh.advertise<niryo_robot_msgs::SoftwareVersion>(
                                            "/niryo_robot_hardware_interface/software_version", 10);

    _sw_version_publisher_timer = nh.createTimer(_sw_version_publisher_duration,
                                                 &HardwareInterface::_publishSoftwareVersion,
                                                 this);
}

/**
 * @brief HardwareInterface::startSubscribers
 * @param nh
 */
void HardwareInterface::startSubscribers(ros::NodeHandle& /*nh*/)
{
    ROS_DEBUG("HardwareInterface::startSubscribers - no subscribers to start");
}

// ********************
//  Callbacks
// ********************

/**
 * @brief HardwareInterface::_callbackStopMotorsReport
 * @param req
 * @param res
 * @return
 */
bool HardwareInterface::_callbackStopMotorsReport(niryo_robot_msgs::Trigger::Request & /*req*/,
                                                  niryo_robot_msgs::Trigger::Response &res)
{
    res.status = niryo_robot_msgs::CommandStatus::FAILURE;

    ROS_INFO("Hardware Interface - Stop Motor Report");

    if (_can_interface)
        _can_interface->activeDebugMode(false);

    if (_ttl_interface)
        _ttl_interface->activeDebugMode(false);

    res.status = niryo_robot_msgs::CommandStatus::SUCCESS;
    res.message = "";

    return true;
}

/**
 * @brief HardwareInterface::_callbackLaunchMotorsReport
 * @param req
 * @param res
 * @return
 */
bool HardwareInterface::_callbackLaunchMotorsReport(niryo_robot_msgs::Trigger::Request & /*req*/,
                                                    niryo_robot_msgs::Trigger::Response &res)
{
    res.status = niryo_robot_msgs::CommandStatus::FAILURE;

    ROS_INFO("Hardware Interface - Start Motors Report");

    int can_status = niryo_robot_msgs::CommandStatus::FAILURE;
    int ttl_status = niryo_robot_msgs::CommandStatus::FAILURE;

    if (_ttl_interface)
    {
        _ttl_interface->activeDebugMode(true);
        ttl_status = _ttl_interface->launchMotorsReport();
        _ttl_interface->activeDebugMode(false);
    }

    if (_can_interface)
    {
        _can_interface->activeDebugMode(true);
        can_status = _can_interface->launchMotorsReport();
        _can_interface->activeDebugMode(false);
        ROS_WARN("Hardware Interface - Motors report ended");

        if ((niryo_robot_msgs::CommandStatus::SUCCESS == can_status) &&
            (niryo_robot_msgs::CommandStatus::SUCCESS == ttl_status))
        {
            res.status = niryo_robot_msgs::CommandStatus::SUCCESS;
            res.message = "Hardware interface seems working properly";
            ROS_INFO("Hardware Interface - Motors report ended");
            return true;
        }
    }
    else {
        if (niryo_robot_msgs::CommandStatus::SUCCESS == ttl_status)
        {
            res.status = niryo_robot_msgs::CommandStatus::SUCCESS;
            res.message = "Hardware interface seems working properly";
            ROS_INFO("Hardware Interface - Motors report ended");
            return true;
        }
    }

    res.status = niryo_robot_msgs::CommandStatus::FAILURE;
    res.message = "Steppers status: ";
    res.message += (can_status == niryo_robot_msgs::CommandStatus::SUCCESS) ? "Ok" : "Error";
    res.message += ", Dxl status: ";
    res.message += (ttl_status == niryo_robot_msgs::CommandStatus::SUCCESS) ? "Ok" : "Error";

    ROS_INFO("Hardware Interface - Motors report ended");

    return true;
}

/**
 * @brief HardwareInterface::_callbackRebootMotors
 * @param req
 * @param res
 * @return
 */
bool HardwareInterface::_callbackRebootMotors(niryo_robot_msgs::Trigger::Request & /*req*/,
                                              niryo_robot_msgs::Trigger::Response &res)
{
    res.status = niryo_robot_msgs::CommandStatus::FAILURE;

    if (_ttl_interface)
        res.status = _ttl_interface->rebootMotors();

    if (niryo_robot_msgs::CommandStatus::SUCCESS == res.status)
    {
        res.message = "Reboot motors done";

        _joints_interface->sendMotorsParams();

        int resp_learning_mode_status = 0;
        std::string resp_learning_mode_message;
        _joints_interface->activateLearningMode(false, resp_learning_mode_status, resp_learning_mode_message);
        _joints_interface->activateLearningMode(true, resp_learning_mode_status, resp_learning_mode_message);
    }
    else
    {
        res.message = "Reboot motors Problems";
    }

    return true;
}

/**
 * @brief HardwareInterface::_publishHardwareStatus
 * Called every _hw_status_publisher_duration via the _hw_status_publisher_timer
 */
void HardwareInterface::_publishHardwareStatus(const ros::TimerEvent&)
{
    niryo_robot_msgs::BusState ttl_bus_state;
    niryo_robot_msgs::BusState can_bus_state;

    bool need_calibration = false;
    bool calibration_in_progress = false;

    int cpu_temperature = 0;

    niryo_robot_msgs::HardwareStatus msg;
    msg.header.stamp = ros::Time::now();
    msg.connection_up = true;

    std::vector<std::string> motor_names;
    std::vector<int32_t> temperatures;
    std::vector<double> voltages;
    std::vector<std::string> motor_types;

    std::vector<int32_t> hw_errors;
    std::vector<std::string> hw_errors_msg;

    if (_can_interface)
    {
        can_bus_state = _can_interface->getBusState();
        msg.connection_up = msg.connection_up && can_bus_state.connection_status;
    }

    if (_ttl_interface)
    {
        ttl_bus_state = _ttl_interface->getBusState();
        msg.connection_up = msg.connection_up && ttl_bus_state.connection_status;
    }

    if (_joints_interface)
    {
        _joints_interface->getCalibrationState(need_calibration, calibration_in_progress);

        auto joints_states = _joints_interface->getJointsState();
        for (const std::shared_ptr<common::model::JointState>& jState : joints_states)
        {
          motor_names.emplace_back(jState->getName());
          voltages.emplace_back(jState->getVoltage());
          temperatures.emplace_back(jState->getTemperature());
          hw_errors.emplace_back(jState->getHardwareError());
          hw_errors_msg.emplace_back(jState->getHardwareErrorMessage());
          motor_types.emplace_back(common::model::HardwareTypeEnum(jState->getHardwareType()).toString());
        }
    }

    if (_end_effector_interface)
    {
        auto state = _end_effector_interface->getEndEffectorState();
        motor_names.emplace_back("End Effector");
        voltages.emplace_back(state->getVoltage());
        temperatures.emplace_back(state->getTemperature());
        hw_errors.emplace_back(state->getHardwareError());
        hw_errors_msg.emplace_back(state->getHardwareErrorMessage());
        motor_types.emplace_back(common::model::HardwareTypeEnum(state->getHardwareType()).toString());
    }

    if (_conveyor_interface)
    {
        auto conveyor_states = _conveyor_interface->getConveyorStates();
        for (const std::shared_ptr<common::model::ConveyorState>& cState : conveyor_states)
        {
            {
                motor_names.emplace_back("Conveyor");
                voltages.emplace_back(cState->getVoltage());
                temperatures.emplace_back(cState->getTemperature());
                hw_errors.emplace_back(cState->getHardwareError());
                hw_errors_msg.emplace_back(cState->getHardwareErrorMessage());
                motor_types.emplace_back(common::model::HardwareTypeEnum(cState->getHardwareType()).toString());
            }
        }
    }

    cpu_temperature = _cpu_interface->getCpuTemperature();

    msg.rpi_temperature = cpu_temperature;
    msg.hardware_version = _hardware_version;

    std::string error_message = can_bus_state.error;
    if (!ttl_bus_state.error.empty())
    {
        error_message += "\n";
        error_message += ttl_bus_state.error;
    }

    msg.error_message = error_message;

    msg.calibration_needed = need_calibration;
    msg.calibration_in_progress = calibration_in_progress;

    msg.motor_names = motor_names;
    msg.motor_types = motor_types;

    msg.temperatures = temperatures;
    msg.voltages = voltages;
    msg.hardware_errors = hw_errors;
    msg.hardware_errors_message = hw_errors_msg;

    _hw_status_publisher.publish(msg);
}

/**
 * @brief HardwareInterface::_publishSoftwareVersion
 * Called every _sw_version_publisher_duration via the _sw_version_publisher_timer
 */
void HardwareInterface::_publishSoftwareVersion(const ros::TimerEvent&)
{
    std::vector<std::string> motor_names;
    std::vector<std::string> firmware_versions;

    if (_joints_interface)
    {
        auto joints_states = _joints_interface->getJointsState();
        for (const std::shared_ptr<common::model::JointState>& jState : joints_states)
        {
            motor_names.emplace_back(jState->getName());
            firmware_versions.emplace_back(jState->getFirmwareVersion());
        }
    }

    if (_end_effector_interface)
    {
        auto state = _end_effector_interface->getEndEffectorState();
        motor_names.emplace_back("End Effector");
        firmware_versions.emplace_back(state->getFirmwareVersion());
    }

    niryo_robot_msgs::SoftwareVersion msg;
    msg.motor_names = motor_names;
    msg.stepper_firmware_versions = firmware_versions;
    msg.rpi_image_version = _rpi_image_version;
    msg.ros_niryo_robot_version = _ros_niryo_robot_version;
    msg.robot_version = _hardware_version;

    _sw_version_publisher.publish(msg);
}

}  // namespace niryo_robot_hardware_interface
