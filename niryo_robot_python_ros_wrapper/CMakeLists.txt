cmake_minimum_required(VERSION 3.0.2)
project(niryo_robot_python_ros_wrapper)
message( STATUS "\n## Reading ${PROJECT_NAME}/CMakeLists.txt...")

#retrieve architecture
execute_process( COMMAND
            uname -m COMMAND tr -d '\n'
        OUTPUT_VARIABLE
            ARCHITECTURE
    )

find_package(catkin REQUIRED COMPONENTS
  actionlib_msgs
  geometry_msgs
  niryo_robot_arm_commander
  niryo_robot_msgs
  niryo_robot_tools_commander
)

# enable python module support
catkin_python_setup()

catkin_package(
  CATKIN_DEPENDS
  rospy
  actionlib_msgs
  geometry_msgs
  niryo_robot_arm_commander
  niryo_robot_msgs
  niryo_robot_tools_commander
)

include_directories(
  ${catkin_INCLUDE_DIRS}
)

catkin_install_python(
    PROGRAMS src/${PROJECT_NAME}/ros_wrapper.py
    DESTINATION ${CATKIN_PACKAGE_BIN_DESTINATION}
)

#############
##   Doc   ##
#############

find_program(rosdoc_lite_FOUND rosdoc_lite)
if(rosdoc_lite_FOUND)
    add_custom_target (${PROJECT_NAME}_doc ALL
        rosdoc_lite -o ${CMAKE_CURRENT_BINARY_DIR}/doc ${CMAKE_CURRENT_LIST_DIR}
    )
else()
  message(WARNING "rosdoc_lite not found. Skipping documentation building")
endif()

#############
## Testing ##
#############

if(CATKIN_ENABLE_TESTING)
  find_package(rostest REQUIRED)

  if ("${ARCHITECTURE}" MATCHES "^(arm.*|aarch64.*|arm64.*)$")
      add_rostest(test/rpi_python_wrapper.test)
  else()
      add_rostest(test/simu_gazebo_python_wrapper.test)
      add_rostest(test/simu_rviz_python_wrapper.test)
  endif()

  ##########################
  ## Static code analysis ##
  ##########################
  find_package(roslint QUIET)

  if(roslint_FOUND)
      set(ROSLINT_PYTHON_OPTS "1>&2")
      roslint_python()
  else()
      message(WARNING "roslint not found. Skipping roslint target building")
  endif()
endif()


#############
## Install ##
#############

# no script to install

# Mark other files for installation
install(
  DIRECTORY examples/
  DESTINATION ${CATKIN_PACKAGE_SHARE_DESTINATION}/examples
)

if (rosdoc_lite_FOUND)
  install(
    DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/doc/
    DESTINATION ${CATKIN_PACKAGE_SHARE_DESTINATION}/doc
  )
endif()
