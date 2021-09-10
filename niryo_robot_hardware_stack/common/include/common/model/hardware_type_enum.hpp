/*
hardware_type_enum.hpp
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

#ifndef MOTOR_TYPE_ENUM
#define MOTOR_TYPE_ENUM

#include <map>
#include <string>

#include "common/common_defs.hpp"
#include "abstract_enum.hpp"

namespace common
{
namespace model
{

enum class EComponentType
{
    TOOL,
    CONVEYOR,
    JOINT,
    END_EFFECTOR,
    UNKNOWN
};

/**
 * @brief The EHardwareType enum
 */
enum class EHardwareType {
                        STEPPER=1,
                        XL430=2,
                        XL320=3,
                        XL330=4,
                        XC430=5,
                        FAKE_DXL_MOTOR=6,
                        FAKE_STEPPER_MOTOR=7,
                        END_EFFECTOR=10,
                        UNKNOWN=100
                      };

/**
 * @brief Specialization of AbstractEnum for Acknowledge status enum
 */
class HardwareTypeEnum : public AbstractEnum<HardwareTypeEnum, EHardwareType>
{
public:
    HardwareTypeEnum(EHardwareType e=EHardwareType::UNKNOWN);
    HardwareTypeEnum(const char* const str);
    ~HardwareTypeEnum() {}

private:
    friend class AbstractEnum<HardwareTypeEnum, EHardwareType>;
    static std::map<EHardwareType, std::string> initialize();
};

} // model
} // common

#endif // MOTOR_TYPE_ENUM
