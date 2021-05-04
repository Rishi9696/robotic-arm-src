/*
    dxl_motor_state.cpp
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
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "model/dxl_motor_state.hpp"

#include <sstream>
#include <math.h>
#include <cassert>
#include <cmath>

using namespace std;

namespace common {
    namespace model {

        DxlMotorState::DxlMotorState()
            : JointState()
        {
            reset();
        }

        DxlMotorState::DxlMotorState(EMotorType type, uint8_t id, bool isTool) :
            DxlMotorState("unknown", type, id, isTool)
        {
        }

        DxlMotorState::DxlMotorState(string name, EMotorType type, uint8_t id, bool isTool) :
            JointState(name, type, id),
            _isTool(isTool)
        {
            setNeedCalibration(false);
        }

        DxlMotorState::~DxlMotorState()
        {

        }

        //*********************
        //  JointState Interface
        //********************

        void DxlMotorState::reset()
        {
            AbstractMotorState::reset();
            _isTool = false;
        }

        bool DxlMotorState::isValid() const
        {
            return (0 != getId() && EMotorType::MOTOR_TYPE_UNKNOWN != getType());
        }

        string DxlMotorState::str() const
        {
            ostringstream ss;

            ss << "DxlMotorState : ";
            ss << "\n---\n";
            ss << "type: " << MotorTypeEnum(_type).toString() << ", ";
            ss << "isTool: " << (_isTool ? "true" : "false") << "\n";
            ss << "p gain: " << _p_gain << ", ";
            ss << "i gain: " << _i_gain << ", ";
            ss << "d gain: " << _d_gain << ", ";
            ss << "ff1 gain: " << _ff1_gain << ", ";
            ss << "ff2 gain: " << _ff2_gain << ", ";

            ss << "\n";
            ss << JointState::str();

            return ss.str();
        }

        int DxlMotorState::to_motor_pos(double pos_rad)
        {
            double denom = getTotalAngle();
            assert(0.0 != denom);
            int converted = getMiddlePosition() + static_cast<int>(((pos_rad) * RADIAN_TO_DEGREE * getTotalRangePosition()) / denom);
            //return (uint32_t)((double)XL430_MIDDLE_POSITION + (position_rad * RADIAN_TO_DEGREE * (double)XL430_TOTAL_RANGE_POSITION) / (double)XL430_TOTAL_ANGLE);
            return converted;
        }

        double DxlMotorState::to_rad_pos(int position_dxl)
        {
            /*double denom = (RADIAN_TO_DEGREE * getTotalRangePosition());
            assert(0.0 != denom);
            double dxl_pose = _offset_position + ((static_cast<int>(_position_state) - getMiddlePosition()) * getTotalAngle()) / denom;

            return std::fmod(dxl_pose, 2 * M_PI); //fmod computes the floating-point remainder of the division operation x/y
            */
            return (double)((((double)position_dxl - getMiddlePosition()) * (double)getTotalAngle()) / (RADIAN_TO_DEGREE * (double)getTotalRangePosition()));

        }

        uint32_t DxlMotorState::getPGain() const
        {
            return _p_gain;
        }

        void DxlMotorState::setPGain(uint32_t p_gain)
        {
            _p_gain = p_gain;
        }

        uint32_t DxlMotorState::getIGain() const
        {
            return _i_gain;
        }

        void DxlMotorState::setIGain(uint32_t i_gain)
        {
            _i_gain = i_gain;
        }

        uint32_t DxlMotorState::getDGain() const
        {
            return _d_gain;
        }

        void DxlMotorState::setDGain(uint32_t d_gain)
        {
            _d_gain = d_gain;
        }

        uint32_t DxlMotorState::getFF1Gain() const
        {
            return _ff1_gain;
        }

        void DxlMotorState::setFF1Gain(uint32_t ff1_gain)
        {
            _ff1_gain = ff1_gain;
        }

        uint32_t DxlMotorState::getFF2Gain() const
        {
            return _ff2_gain;
        }

        void DxlMotorState::setFF2Gain(uint32_t value)
        {
            _ff2_gain = value;
        }

    } // namespace model
} // namespace common
