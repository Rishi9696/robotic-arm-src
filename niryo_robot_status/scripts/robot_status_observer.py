#!/usr/bin/env python

import rospy

# - Messages
from std_msgs.msg import Bool
from std_msgs.msg import Header
from sensor_msgs.msg import JointState

from niryo_robot_msgs.msg import HardwareStatus
from niryo_robot_arm_commander.msg import PausePlanExecution
from niryo_robot_programs_manager.msg import ProgramIsRunning

# - Services
from niryo_robot_arm_commander.srv import GetJointLimits


class RobotStatusObserver(object):

    def __init__(self, robot_status_handler):
        """

        :type robot_status_handler: RobotStatusHandler
        """
        self.__robot_status_handler = robot_status_handler

        self.out_of_bounds = False
        self.hardware_status = HardwareStatus()
        self.motion_goal_is_active = False
        self.is_debug_motor_active = False
        self.jog_is_enabled = False
        self.program_is_running = False
        self.program_error = False
        self.program_error_message = ""
        self.is_tcp_client_connected = False
        self.learning_mode_state = False
        self.joint_limits = None
        self.pause_state = PausePlanExecution.STANDBY
        self.collision_detected = False

        # - Subscribers
        self.hardware_status_sub = rospy.Subscriber('/niryo_robot_hardware_interface/hardware_status',
                                                    HardwareStatus, self.__callback_hardware_status)

        self.__pause_state = PausePlanExecution.STANDBY
        self.__pause_state_sub = rospy.Subscriber('/niryo_robot_rpi/pause_state',
                                                  PausePlanExecution, self.__callback_pause)

        self.motion_goal_is_active_sub = rospy.Subscriber('/niryo_robot_arm_commander/is_active',
                                                          Bool, self.__callback_motion_goal_is_active)

        self.is_debug_motor_active_sub = rospy.Subscriber('/niryo_robot_arm_commander/is_debug_motor_active',
                                                          Bool, self.__callback_is_debug_motor_active)

        self.jog_is_enabled_sub = rospy.Subscriber('/niryo_robot/jog_interface/is_enabled',
                                                   Bool, self.__callback_jog_is_enabled)

        self.program_is_running_sub = rospy.Subscriber('/niryo_robot_programs_manager/program_is_running',
                                                       ProgramIsRunning, self.__callback_program_is_running)

        self.is_tcp_client_connected_sub = rospy.Subscriber('/niryo_robot_user_interface/is_client_connected',
                                                            Bool, self.__callback_is_client_connected)

        self.learning_mode_state_sub = rospy.Subscriber('/niryo_robot/learning_mode/state',
                                                        Bool, self.__callback_learning_mode_state)

        self.__collision_detected_sub = rospy.Subscriber('/niryo_robot_arm_commander/collision_detected',
                                                         Bool, self.__callback_collision_detected)

        self.__joint_state_sub = rospy.Subscriber('/joint_states', JointState, self.__callback_joint_states)

    def __callback_pause(self, msg):
        if self.__pause_state != msg.state:
            self.__pause_state = msg.state
            self.__robot_status_handler.advertise_new_state()

    def __callback_motion_goal_is_active(self, msg):
        if self.motion_goal_is_active != msg.data:
            self.motion_goal_is_active = msg.data
            self.__robot_status_handler.advertise_new_state()

    def __callback_is_debug_motor_active(self, msg):
        if self.is_debug_motor_active != msg.data:
            self.is_debug_motor_active = msg.data
            self.__robot_status_handler.advertise_new_state()

    def __callback_program_is_running(self, msg):
        if self.program_is_running != msg.program_is_running:
            self.program_is_running = msg.program_is_running
            if not msg.program_is_running:
                self.program_error = True if msg.last_execution_status == msg.EXECUTION_ERROR else False
                self.program_error_message = msg.last_execution_msg
            self.__robot_status_handler.advertise_new_state()

            # Clean error after
            self.program_error = False
            self.program_error_message = ""

    def __callback_jog_is_enabled(self, msg):
        if self.jog_is_enabled != msg.data:
            self.jog_is_enabled = msg.data
            self.__robot_status_handler.advertise_new_state()

    def __callback_hardware_status(self, msg):
        # Clean up unused attributes to avoid unwanted callbacks due to their fluctuation.
        msg.header = Header()
        msg.rpi_temperature = 0
        msg.temperatures = []
        msg.voltages = []
        if self.hardware_status != msg:
            self.hardware_status = msg
            self.__robot_status_handler.advertise_new_state()

    def __callback_learning_mode_state(self, msg):
        if self.learning_mode_state != msg.data:
            self.learning_mode_state = msg.data
            self.__robot_status_handler.advertise_new_state()

    def __callback_is_client_connected(self, msg):
        if self.is_tcp_client_connected != msg.data:
            self.is_tcp_client_connected = msg.data
            self.__robot_status_handler.advertise_new_state()

    def __callback_collision_detected(self, msg):
        self.collision_detected = msg.data

    def __callback_joint_states(self, msg):
        if not self.joint_limits:
            return

        current_out_of_bounds_state = False
        if not self.hardware_status.calibration_in_progress and not self.hardware_status.calibration_needed:
            for joint_name, joint_pose in zip(msg.name, msg.position):
                if not (self.joint_limits[joint_name].min <= joint_pose <= self.joint_limits[joint_name].max):
                    current_out_of_bounds_state = True
                    break

        if self.out_of_bounds != current_out_of_bounds_state:
            self.out_of_bounds = current_out_of_bounds_state
            self.__robot_status_handler.advertise_out_of_bounds()

    def read_joint_limits(self):
        tries = 0
        while not rospy.is_shutdown() and tries < 10:
            try:
                rospy.wait_for_service('/niryo_robot_arm_commander/get_joints_limit', timeout=5)
                joint_limits_service = rospy.ServiceProxy('/niryo_robot_arm_commander/get_joints_limit', GetJointLimits)
                self.joint_limits = {joint_limit.name: joint_limit for joint_limit in
                                     joint_limits_service.call().joint_limits}
            except rospy.ROSException:
                rospy.logwarn("Waiting for '/niryo_robot_arm_commander/get_joints_limit' service")
            tries += 1
