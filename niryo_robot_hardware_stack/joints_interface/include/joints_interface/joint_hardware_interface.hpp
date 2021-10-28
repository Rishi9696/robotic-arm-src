/*
joint_hardware_interface.hpp
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

#ifndef JOINT_HARDWARE_INTERFACE_HPP
#define JOINT_HARDWARE_INTERFACE_HPP

// std
#include <memory>
#include <algorithm>

// ros
#include <ros/ros.h>

#include <hardware_interface/joint_state_interface.h>
#include <hardware_interface/joint_command_interface.h>
#include <hardware_interface/robot_hw.h>

// niryo
#include "joints_interface/calibration_manager.hpp"
#include "can_driver/can_interface_core.hpp"
#include "ttl_driver/ttl_interface_core.hpp"
#include "common/model/joint_state.hpp"

#include <dynamic_reconfigure/server.h>
#include <joints_interface/stepper1Config.h>
#include <joints_interface/stepper2Config.h>
#include <joints_interface/stepper3Config.h>

namespace joints_interface
{

class JointHardwareInterface : public hardware_interface::RobotHW
{

    public:
        JointHardwareInterface(ros::NodeHandle& rootnh,
                               ros::NodeHandle& robot_hwnh,
                               std::shared_ptr<ttl_driver::TtlInterfaceCore> ttl_interface,
                               std::shared_ptr<can_driver::CanInterfaceCore> can_interface);

        void sendInitMotorsParams(bool learningMode);
        void setSteppersProfiles();
        int calibrateJoints(int mode, std::string &result_message);
        void setNeedCalibration();
        void activateLearningMode(bool activated);
        void synchronizeMotors(bool synchronize);

        void setCommandToCurrentPosition();

        bool needCalibration() const;
        bool isCalibrationInProgress() const;

        const std::vector<std::shared_ptr<common::model::JointState> >& getJointsState() const;

        // RobotHW interface
    public:
        bool init(ros::NodeHandle& rootnh, ros::NodeHandle &robot_hwnh) override;

        void read(const ros::Time &/*time*/, const ros::Duration &/*period*/) override;
        void write(const ros::Time &/*time*/, const ros::Duration &/*period*/) override;

    private:

        void config1Callback(joints_interface::stepper1Config &config, uint32_t level);
        void config2Callback(joints_interface::stepper2Config &config, uint32_t level);
        void config3Callback(joints_interface::stepper3Config &config, uint32_t level);

        bool initStepper(ros::NodeHandle &robot_hwnh,
                         const std::shared_ptr<common::model::StepperMotorState>& stepperState,
                         const std::string& currentNamespace) const;
        bool initDxl(ros::NodeHandle &robot_hwnh,
                     const std::shared_ptr<common::model::DxlMotorState>& dxlState,
                     const std::string& currentNamespace) const;

    private:
        dynamic_reconfigure::Server<joints_interface::stepper1Config> _dr_srv_1;
        dynamic_reconfigure::Server<joints_interface::stepper2Config> _dr_srv_2;
        dynamic_reconfigure::Server<joints_interface::stepper3Config> _dr_srv_3;

        hardware_interface::JointStateInterface _joint_state_interface;
        hardware_interface::PositionJointInterface _joint_position_interface;

        std::shared_ptr<ttl_driver::TtlInterfaceCore> _ttl_interface;
        std::shared_ptr<can_driver::CanInterfaceCore> _can_interface;

        std::unique_ptr<CalibrationManager> _calibration_manager;

        std::map<uint8_t, std::string> _map_stepper_name;
        std::map<uint8_t, std::string> _map_dxl_name;

        std::vector<std::shared_ptr<common::model::JointState> > _joint_list;
};

/**
 * @brief JointHardwareInterface::getJointsState
 * @return
 */
inline
bool JointHardwareInterface::isCalibrationInProgress() const
{
    return _calibration_manager->CalibrationInprogress();
}

/**
 * @brief JointHardwareInterface::getJointsState
 * @return
 */
inline
const std::vector<std::shared_ptr<common::model::JointState> >&
JointHardwareInterface::getJointsState() const
{
    return _joint_list;
}

} // JointsInterface

#endif
