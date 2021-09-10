/*
idriver_core.hpp
Copyright (C) 2017 Niryo
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

#ifndef I_DriverCore_H
#define I_DriverCore_H

// C++
#include <string>
#include <thread>
#include <mutex>

// niryo
#include "niryo_robot_msgs/BusState.h"
#include "hardware_type_enum.hpp"
#include "common/model/joint_state.hpp"
#include "common/model/conveyor_state.hpp"
#include "common/model/abstract_single_motor_cmd.hpp"
#include "common/model/abstract_synchronize_motor_cmd.hpp"
#include "common/model/stepper_calibration_status_enum.hpp"

namespace common
{
namespace model
{

/**
 * @brief The IDriverCore class
 */
class IDriverCore
{
    public:
        virtual ~IDriverCore() = 0;

        virtual common::model::EBusProtocol getBusProtocol() const = 0;

        virtual void startControlLoop() = 0;
        virtual bool isConnectionOk() const = 0;
        virtual bool scanMotorId(uint8_t motor_to_find) = 0;
        virtual void addSingleCommandToQueue(const std::shared_ptr<common::model::ISingleMotorCmd>& cmd) = 0;
        virtual void addSingleCommandToQueue(const std::vector<std::shared_ptr<common::model::ISingleMotorCmd> >& cmd) = 0;
        virtual void setSyncCommand(const std::shared_ptr<common::model::ISynchronizeMotorCmd>& cmd) = 0;

        // driver for conveyor
        virtual int setConveyor(const common::model::ConveyorState& state) = 0;
        virtual void unsetConveyor(uint8_t motor_id) = 0;

        // calibration
        virtual void startCalibration() = 0;
        virtual void resetCalibration() = 0;
        virtual bool isCalibrationInProgress() const = 0;
        virtual int32_t getCalibrationResult(uint8_t id) const = 0;
        virtual common::model::EStepperCalibrationStatus getCalibrationStatus() const = 0;

        virtual void activeDebugMode(bool mode) = 0;

        virtual int launchMotorsReport() = 0;
        virtual niryo_robot_msgs::BusState getBusState() const = 0;

        virtual std::vector<std::shared_ptr<common::model::JointState> > getJointStates() const = 0;
        virtual common::model::JointState getJointState(uint8_t motor_id) const = 0;
        
    private:
        virtual void resetHardwareControlLoopRates() = 0;
        virtual void controlLoop() = 0;
        virtual void _executeCommand() = 0;
};

/**
 * @brief IDriverCore::~IDriverCore
 */
inline
IDriverCore::~IDriverCore()
{
}

} // namespace model
} // namespace common

#endif // I_DRIVER_CORE_H
