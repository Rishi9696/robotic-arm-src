/*
stepper_motor_state.h
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

#ifndef STEPPER_MOTOR_STATE_H
#define STEPPER_MOTOR_STATE_H

#include <string>
#include "joint_state.hpp"
#include "stepper_calibration_status_enum.hpp"

namespace common
{
namespace model
{

/**
 * @brief The StepperMotorState class
 */
class StepperMotorState : public JointState
{

    public:
        StepperMotorState();
        StepperMotorState(EHardwareType type, EComponentType component_type,
                          EBusProtocol bus_proto, uint8_t id);
        StepperMotorState(std::string name, EHardwareType type, EComponentType component_type,
                          EBusProtocol bus_proto, uint8_t id);
        StepperMotorState(const StepperMotorState& state);

        virtual ~StepperMotorState() override;

        int stepsPerRev();

        // setters
        void updateLastTimeRead();
        void setHwFailCounter(double fail_counter);
        void setGearRatio(double gear_ratio);
        void setMaxEffort(double max_effort);

        void setCalibration(const common::model::EStepperCalibrationStatus &calibration_state,
                            const int32_t &calibration_value);

        // getters
        double getLastTimeRead() const;
        double getHwFailCounter() const;

        double getGearRatio() const;
        double getMaxEffort() const;

        common::model::EStepperCalibrationStatus getCalibrationState() const;
        int32_t getCalibrationValue() const;

        // tests
        bool isConveyor() const;
        bool isTimeout() const;

        // JointState interface
        virtual void reset() override;
        virtual bool isValid() const override;
        virtual std::string str() const override;

        virtual int to_motor_pos(double pos_rad) override;
        virtual double to_rad_pos(int pos) override;

protected:
        double _last_time_read{-1.0};
        double _hw_fail_counter{0.0};

        double _gear_ratio{1.0};
        double _max_effort{0.0};
        double _micro_steps{8.0};

        common::model::EStepperCalibrationStatus _calibration_state{common::model::EStepperCalibrationStatus::CALIBRATION_UNINITIALIZED};
        int32_t _calibration_value{0};

    private:
        static constexpr double STEPPERS_MOTOR_STEPS_PER_REVOLUTION = 200.0;
};

/**
 * @brief StepperMotorState::getMaxEffort
 * @return
 */
inline
double StepperMotorState::getMaxEffort() const
{
    return _max_effort;
}

/**
 * @brief StepperMotorState::getCalibrationState
 * @return
 */
inline
EStepperCalibrationStatus StepperMotorState::getCalibrationState() const
{
    return _calibration_state;
}

/**
 * @brief StepperMotorState::getCalibrationValue
 * @return
 */
inline
int32_t StepperMotorState::getCalibrationValue() const
{
    return _calibration_value;
}

/**
 * @brief StepperMotorState::isConveyor
 * @return
 */
inline
bool StepperMotorState::isConveyor() const
{
    return (getComponentType() == common::model::EComponentType::CONVEYOR);
}

/**
 * @brief StepperMotorState::getGearRatio
 * @return
 */
inline
double StepperMotorState::getGearRatio() const
{
    return _gear_ratio;
}

/**
 * @brief StepperMotorState::getLastTimeRead
 * @return
 */
inline
int StepperMotorState::stepsPerRev() {
    return int(_micro_steps * STEPPERS_MOTOR_STEPS_PER_REVOLUTION);
}

inline
double StepperMotorState::getLastTimeRead() const
{
    return _last_time_read;
}

/**
 * @brief StepperMotorState::getHwFailCounter
 * @return
 */
inline
double StepperMotorState::getHwFailCounter() const
{
    return _hw_fail_counter;
}


} // model
} // common

#endif
