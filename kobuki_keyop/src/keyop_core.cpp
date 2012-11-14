/**
 * @file /kobuki_keyop/src/keyop_core.cpp
 *
 * @brief Creates a node for remote controlling parts of robot_core.
 *
 * @date 24/05/2010
 **/

/*****************************************************************************
** Includes
*****************************************************************************/

#include <ros/ros.h>
#include <ecl/time.hpp>
#include <std_srvs/Empty.h>

#include "../include/keyop_core/keyop_core.hpp"


/*****************************************************************************
** Namespaces
*****************************************************************************/

namespace keyop_core {

/*****************************************************************************
** Implementation
*****************************************************************************/
/**
 * @brief Default constructor, needs initialisation.
 */
KeyOpCore::KeyOpCore() :
	accept_incoming(true),
	power_status(false),
	cmd(new geometry_msgs::Twist()),
	cmd_stamped(new geometry_msgs::TwistStamped()),
	power_cmd(new std_msgs::String()),
	linear_vel_step(0.1),
	linear_vel_max(3.4),
	angular_vel_step(0.02),
	angular_vel_max(1.2),
	mode("full"),
	quit_requested(false),
	key_file_descriptor(0),
	thread(&KeyOpCore::keyboardInputLoop,*this)
{
  tcgetattr(key_file_descriptor,&original_terminal_state); // get terminal properties
}
KeyOpCore::~KeyOpCore() {
  tcsetattr(key_file_descriptor, TCSANOW, &original_terminal_state);
}
/**
 * @brief Initialises the node.
 */
void KeyOpCore::init() {

	ros::NodeHandle nh("~");

	name = nh.getUnresolvedNamespace();

	/*********************
	** Parameters
	**********************/
	nh.getParam("linear_vel_step" , linear_vel_step );
	nh.getParam("linear_vel_max"  , linear_vel_max  );
	nh.getParam("angular_vel_step", angular_vel_step);
	nh.getParam("angular_vel_max" , angular_vel_max );
	nh.getParam("mode" , mode );


	ROS_INFO_STREAM("KeyOpCore : using linear  vel step [" << linear_vel_step  << "].");
	ROS_INFO_STREAM("KeyOpCore : using linear  vel max  [" << linear_vel_max   << "].");
	ROS_INFO_STREAM("KeyOpCore : using angular vel step [" << angular_vel_step << "].");
	ROS_INFO_STREAM("KeyOpCore : using angular vel max  [" << angular_vel_max  << "].");

	/*********************
	** Subscribers
	**********************/
	keyinput_subscriber = nh.subscribe("teleop",1,&KeyOpCore::remoteKeyInputReceived,this);

	/*********************
	** Publishers
	**********************/
	velocity_publisher = nh.advertise<geometry_msgs::Twist>("cmd_vel",1);
	stamped_velocity_publisher = nh.advertise<geometry_msgs::TwistStamped>("cmd_vel_stamped",1);
	enable_publisher = nh.advertise<std_msgs::String>("enable",1);
	disable_publisher = nh.advertise<std_msgs::String>("disable",1);
	reset_odometry_client = nh.serviceClient<std_srvs::Empty>("reset_odometry");

	power_cmd->data = "all";

	/*********************
	** Velocities
	**********************/
	cmd->linear.x = 0.0;
	cmd->linear.y = 0.0;
	cmd->linear.z = 0.0;
	cmd->angular.x = 0.0;
	cmd->angular.y = 0.0;
	cmd->angular.z = 0.0;

	cmd_stamped->header.stamp = ros::Time::now();
	cmd_stamped->header.frame_id = ros::this_node::getName();//"kobuki_keyop";
	cmd_stamped->twist.linear.x = 0.0;
	cmd_stamped->twist.linear.y = 0.0;
	cmd_stamped->twist.linear.z = 0.0;
	cmd_stamped->twist.angular.x = 0.0;
	cmd_stamped->twist.angular.y = 0.0;
	cmd_stamped->twist.angular.z = 0.0;

	/*********************
	** Wait for connection
	**********************/
	if( mode == "simple" )
	  return;

	ecl::MilliSleep millisleep;
	int count = 0;
	bool connected = false;
	while ( !connected ) {
		if ( enable_publisher.getNumSubscribers() > 0 ) {
			connected = true;
			break;
		}
		if ( count == 6 ) {
			connected = false;
			break;
		} else {
			ROS_WARN("KeyOp: could not connect, trying again after 500ms...");
			millisleep(500);
			++count;
		}
	}
	if ( !connected ) {
		ROS_ERROR("KeyOp: could not connect.");
		ROS_ERROR("KeyOp: check remappings for enable/disable topics).");
	} else {
		enable_publisher.publish(power_cmd);
		ROS_INFO("KeyOp: connected.");
		power_status = true;
	}
}

/*****************************************************************************
** Implementation [Spin]
*****************************************************************************/
/**
 * @brief Worker thread loop, mostly empty, but provides clean exit mechanisms.
 *
 * Process ros functions as well as aborting when requested.
 */
void KeyOpCore::spin() {

	ros::Rate loop_rate(10);

	while ( !quit_requested && ros::ok() ) {
		velocity_publisher.publish(cmd);
		cmd_stamped->header.stamp = ros::Time::now();
		cmd_stamped->twist.linear.x =  cmd->linear.x ;
		cmd_stamped->twist.linear.y =  cmd->linear.y ;
		cmd_stamped->twist.linear.z =  cmd->linear.z ;
		cmd_stamped->twist.angular.x = cmd->angular.x;
		cmd_stamped->twist.angular.y = cmd->angular.y;
		cmd_stamped->twist.angular.z = cmd->angular.z;
		stamped_velocity_publisher.publish(cmd_stamped);
		accept_incoming = true;
		ros::spinOnce();
		loop_rate.sleep();
	}
	if ( quit_requested) { // ros node is still ok, send a disable command
		disable();
	} else {
          // just in case we got here not via a keyboard quit request
          quit_requested = true;
          thread.cancel();
	}
	thread.join();
}


/*****************************************************************************
** Implementation [Keyboard]
*****************************************************************************/

/**
 * @brief The worker thread function that accepts input keyboard commands.
 *
 * This is ok here - but later it might be a good idea to make a node which
 * posts keyboard events to a topic. Recycle common code if used by many!
 */
void KeyOpCore::keyboardInputLoop() {
	struct termios raw;
	memcpy(&raw, &original_terminal_state, sizeof(struct termios));

	raw.c_lflag &=~ (ICANON | ECHO);
	// Setting a new line, then end of file
	raw.c_cc[VEOL] = 1;
	raw.c_cc[VEOF] = 2;
	tcsetattr(key_file_descriptor, TCSANOW, &raw);

	puts("Reading from keyboard");
	puts("---------------------------");
	puts("Forward/back arrows : linear velocity incr/decr.");
	puts("Right/left arrows : angular velocity incr/decr.");
	puts("Spacebar : reset linear/angular velocities.");
	puts("d : disable motors.");
	puts("e : enable motors.");
	puts("q : quit.");
	char c;
	while ( !quit_requested ) {
	    if(read(key_file_descriptor, &c, 1) < 0) {
	      perror("read char failed():");
	      exit(-1);
	    }
	    processKeyboardInput(c);
	}
}

/**
 * @brief Callback function for remote keyboard inputs subscriber.
 */
void KeyOpCore::remoteKeyInputReceived(const kobuki_msgs::KeyboardInput& key) {

	processKeyboardInput(key.pressedKey);
}

/**
 * @brief Process individual keyboard inputs.
 *
 * @param c keyboard input.
 */
void KeyOpCore::processKeyboardInput(char c) {

    /*
     * Arrow keys are a bit special, they are escape characters - meaning they
     * trigger a sequence of keycodes. In this case, 'esc-[-Keycode_xxx'. We
     * ignore the esc-[ and just parse the last one. So long as we avoid using
     * the last one for its actual purpose (e.g. left arrow corresponds to
     * esc-[-D) we can keep the parsing simple.
     */
	switch(c) {
	    case kobuki_msgs::KeyboardInput::KeyCode_Left: {
	    	incrementAngularVelocity();
	    	break;
	    }
	    case kobuki_msgs::KeyboardInput::KeyCode_Right: {
	    	decrementAngularVelocity();
	    	break;
	    }
	    case kobuki_msgs::KeyboardInput::KeyCode_Up: {
	    	incrementLinearVelocity();
	    	break;
	    }
	    case kobuki_msgs::KeyboardInput::KeyCode_Down: {
	    	decrementLinearVelocity();
	    	break;
	    }
	    case kobuki_msgs::KeyboardInput::KeyCode_Space: {
	    	resetVelocity();
	    	break;
	    }
            case 'q': {
	    	quit_requested = true;
	    	break;
	    }
	    case 'd': {
	    	disable();
	    	break;
	    }
	    case 'e': {
	    	enable();
	    	break;
	    }
	    default: {
	    	break;
	    }
	}
}

/*****************************************************************************
** Implementation [Commands]
*****************************************************************************/
/**
 * @brief Disables commands to the navigation system.
 *
 * This does the following things:
 *
 * - Disables power to the navigation motors (via device_manager).
 * @param msg
 */
void KeyOpCore::disable() {

	cmd->linear.x = 0.0;
	cmd->angular.z = 0.0;
	velocity_publisher.publish(cmd);
	accept_incoming = false;

	if ( power_status ) {
		disable_publisher.publish(power_cmd);
		ROS_INFO("KeyOp: die, die, die (disabling power to the device subsystem).");
		power_status = false;
	} else {
		ROS_WARN("KeyOp: motors are already powered down.");
	}
}
/**
 * @brief Reset/re-enable the navigation system.
 *
 * - resets the command velocities.
 * - resets the odometry.
 * - reenable power if not enabled.
 */
void KeyOpCore::enable() {

	accept_incoming = false;

	cmd->linear.x = 0.0;
	cmd->angular.z = 0.0;
	velocity_publisher.publish(cmd);

	std_srvs::Empty odometry;
	if ( ! reset_odometry_client.call(odometry) ) {
		ROS_WARN("KeyOp: could not contact the mobile base model to reset the odometry.");
		ROS_WARN_STREAM("KeyOp: " << ros::names::resolve("reset_odometry"));
	}

	if ( !power_status ) {
		enable_publisher.publish(power_cmd);
		ROS_INFO("KeyOp: resetting odometry and enabling power to the device subsystem.");
		power_status = true;
	} else {
		ROS_INFO("KeyOp: resetting commands and odometry (mobile_base).");
	}
}

/**
 * @brief If not already maxxed, increment the command velocities..
 */
void KeyOpCore::incrementLinearVelocity() {

	if ( power_status ) {
		if ( cmd->linear.x <= linear_vel_max ) {
			cmd->linear.x += linear_vel_step;
		}
		ROS_INFO_STREAM("KeyOp: linear  velocity incremented [" << cmd->linear.x << "|" << cmd->angular.z << "]");
		velocity_publisher.publish(cmd);
	} else {
		ROS_WARN_STREAM("KeyOp: motors are not yet powered up.");
	}
}

/**
 * @brief If not already minned, decrement the linear velocities..
 */
void KeyOpCore::decrementLinearVelocity() {

	if ( power_status ) {
		if ( cmd->linear.x >= -linear_vel_max ) {
			cmd->linear.x -= linear_vel_step;
		}
		ROS_INFO_STREAM("KeyOp: linear  velocity decremented [" << cmd->linear.x << "|" << cmd->angular.z << "]");
		velocity_publisher.publish(cmd);
	} else {
		ROS_WARN_STREAM("KeyOp: motors are not yet powered up.");
	}
}

/**
 * @brief If not already maxxed, increment the angular velocities..
 */
void KeyOpCore::incrementAngularVelocity() {

	if ( power_status ) {
		if ( cmd->angular.z <= angular_vel_max ) {
			cmd->angular.z += angular_vel_step;
		}
		ROS_INFO_STREAM("KeyOp: angular velocity incremented [" << cmd->linear.x << "|" << cmd->angular.z << "]");
		velocity_publisher.publish(cmd);
	} else {
		ROS_WARN_STREAM("KeyOp: motors are not yet powered up.");
	}
}

/**
 * @brief If not already mined, decrement the angular velocities..
 */
void KeyOpCore::decrementAngularVelocity() {

	if ( power_status ) {
		if ( cmd->angular.z >= -angular_vel_max ) {
			cmd->angular.z -= angular_vel_step;
		}
		ROS_INFO_STREAM("KeyOp: angular velocity decremented [" << cmd->linear.x << "|" << cmd->angular.z << "]");
		velocity_publisher.publish(cmd);
	} else {
		ROS_WARN_STREAM("KeyOp: motors are not yet powered up.");
	}
}

void KeyOpCore::resetVelocity() {
	if ( power_status ) {
		cmd->angular.z = 0.0;
		cmd->linear.x  = 0.0;
		ROS_INFO_STREAM("KeyOp: reset linear/angular velocities.");
		velocity_publisher.publish(cmd);
	} else {
		ROS_WARN_STREAM("KeyOp: motors are not yet powered up.");
	}
}


} // namespace keyop_core