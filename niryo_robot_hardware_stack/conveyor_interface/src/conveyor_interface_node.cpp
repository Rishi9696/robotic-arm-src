#include <ros/ros.h>
#include "conveyor_interface/conveyor_interface_core.hpp"
#include "stepper_driver/stepper_driver_core.hpp"

using namespace ConveyorInterface;

int main(int argc, char **argv) 
{
    ros::init(argc, argv, "conveyor_interface_node");
  
    ROS_DEBUG("Launching conveyor_interface_node");

    ros::AsyncSpinner spinner(4);
    spinner.start();
    
    ros::NodeHandle nh;
   
    std::shared_ptr<StepperDriver::StepperDriverCore> stepper = std::make_shared<StepperDriver::StepperDriverCore>();
    ros::Duration(1).sleep();
    std::shared_ptr<ConveyorInterfaceCore> conveyor_interface = std::make_shared<ConveyorInterfaceCore>(stepper);
    ros::Duration(1).sleep();
    
    ros::spin();

    return 0;
}
