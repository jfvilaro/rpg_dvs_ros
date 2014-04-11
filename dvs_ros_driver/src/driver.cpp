#include "ros/ros.h"

// boost
#include <boost/thread.hpp>
#include <boost/thread/thread_time.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>

// messages
#include <dvs_msgs/Event.h>
#include <dvs_msgs/EventArray.h>

// DVS driver
#include <dvs_driver/dvs_driver.h>

// dynamic reconfigure
#include <dynamic_reconfigure/server.h>
#include <dvs_ros_driver/DVS_ROS_DriverConfig.h>


boost::posix_time::time_duration delta;
ros::Publisher* event_array_pub;
dvs::DVS_Driver* driver;

dvs_ros_driver::DVS_ROS_DriverConfig current_config;

bool parameter_update_required = false;

void change_dvs_parameters() {
  while(true) {
    try {
      if (parameter_update_required) {
        parameter_update_required = false;
        driver->change_parameters(current_config.cas, current_config.injGnd, current_config.reqPd, current_config.puX,
                                  current_config.diffOff, current_config.req, current_config.refr, current_config.puY,
                                  current_config.diffOn, current_config.diff, current_config.foll, current_config.pr);
      }

      boost::this_thread::sleep(boost::posix_time::milliseconds(10));
    } 
    catch(boost::thread_interrupted&) {
      return;
    }
  }
}

void callback(dvs_ros_driver::DVS_ROS_DriverConfig &config, uint32_t level) {
  // did any DVS bias setting change?
   if (current_config.cas != config.cas || current_config.injGnd != config.injGnd ||
       current_config.reqPd != config.reqPd || current_config.puX != config.puX ||
       current_config.diffOff != config.diffOff || current_config.req != config.req ||
       current_config.refr != config.refr || current_config.puY != config.puY ||
       current_config.diffOn != config.diffOn || current_config.diff != config.diff ||
       current_config.foll != config.foll || current_config.pr != config.pr) {

     current_config.cas = config.cas;
     current_config.injGnd = config.injGnd;
     current_config.reqPd = config.reqPd;
     current_config.puX = config.puX;
     current_config.diffOff = config.diffOff;
     current_config.req = config.req;
     current_config.refr = config.refr;
     current_config.puY = config.puY;
     current_config.diffOn = config.diffOn;
     current_config.diff = config.diff;
     current_config.foll = config.foll;
     current_config.pr = config.pr;

     parameter_update_required = true;
   }

   // change streaming rate, if necessary
   if (current_config.streaming_rate != config.streaming_rate) {
     current_config.streaming_rate = config.streaming_rate;
     delta = boost::posix_time::microseconds(1e6/current_config.streaming_rate);
   }
}

void readout() {
  std::vector<dvs::Event> events;
  dvs_msgs::EventArray msg;

  boost::posix_time::ptime next_send_time = boost::posix_time::microsec_clock::local_time();

  while(true) {
    try {
      events = driver->get_events();

      for (int i=0; i<events.size(); ++i) {
        dvs_msgs::Event e;
        e.x = events[i].x;
        e.y = events[i].y;
        e.time = events[i].timestamp;
        e.polarity = events[i].polarity;

        msg.events.push_back(e);
      }

      if (boost::posix_time::microsec_clock::local_time() > next_send_time)
      {
        event_array_pub->publish(msg);
        ros::spinOnce();
        events.clear();
        msg.events.clear();

        next_send_time += delta;
      }

      boost::this_thread::sleep(delta/10.0);
    }
    catch(boost::thread_interrupted&) {
      return;
    }
  }
}

int main(int argc, char* argv[]) {
  ros::init(argc, argv, "dvs_ros_driver");

  ros::NodeHandle nh;

  current_config.streaming_rate = 30;
  delta = boost::posix_time::microseconds(1e6/current_config.streaming_rate);

  ros::Publisher event_array_pub_instance = nh.advertise<dvs_msgs::EventArray>("dvs_events", 1);
  event_array_pub = &event_array_pub_instance;

  // Dynamic reconfigure
  dynamic_reconfigure::Server<dvs_ros_driver::DVS_ROS_DriverConfig> server;
  dynamic_reconfigure::Server<dvs_ros_driver::DVS_ROS_DriverConfig>::CallbackType f;
  f = boost::bind(&callback, _1, _2);
  server.setCallback(f);

  // Load driver
  driver = new dvs::DVS_Driver();

  // start threads
  boost::thread parameter_thread(&change_dvs_parameters);
  boost::thread readout_thread(&readout);

  // spin ros
  ros::spin();

  // end threads
  parameter_thread.interrupt();
  readout_thread.interrupt();

  parameter_thread.join();
  readout_thread.join();

  return 0;
}
