/*
    dxl_tools.h
    Copyright (C) 2018 Niryo
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

#ifndef DXL_TOOLS_H
#define DXL_TOOLS_H

#include "dynamixel_sdk/dynamixel_sdk.h"
#include <string>
#include <vector>
#include <memory>

namespace robotDebug {
    class DxlTools {


        public:
            DxlTools();
            DxlTools(std::shared_ptr<dynamixel::PortHandler> portHandler, std::shared_ptr<dynamixel::PacketHandler> packetHandler);

            int setupDxlBus(int baudrate);
            void broadcastPing();
            void ping(int id);
            int setRegister(uint8_t id, uint8_t reg_address,
                             uint32_t value, uint8_t byte_number);
            int getRegister(uint8_t id, uint8_t reg_address,
                            uint32_t &value, uint8_t byte_number);

            void closePort();

        protected:
            std::shared_ptr<dynamixel::PortHandler> _portHandler;
            std::shared_ptr<dynamixel::PacketHandler> _packetHandler;

    };
} // namespace robotDebug

#endif
