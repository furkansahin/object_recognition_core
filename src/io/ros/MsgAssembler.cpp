/*
 * Software License Agreement (BSD License)
 *
 *  Copyright (c) 2009, Willow Garage, Inc.
 *  All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above
 *     copyright notice, this list of conditions and the following
 *     disclaimer in the documentation and/or other materials provided
 *     with the distribution.
 *   * Neither the name of Willow Garage, Inc. nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 *  FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 *  COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 *  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 *  BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 *  LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 *  CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 *  LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 *  ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 *  POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include <vector>

#include <boost/foreach.hpp>

#include <ecto/ecto.hpp>

#include <Eigen/Geometry>

// ROS includes
#include <ros/publisher.h>
#include <std_msgs/String.h>
#include <geometry_msgs/PoseArray.h>
#include <sensor_msgs/Image.h>
#include <visualization_msgs/MarkerArray.h>

#include <opencv2/core/core.hpp>

#include <object_recognition_core/common/pose_result.h>
#include <object_recognition_core/common/types.h>
#include <object_recognition_core/db/db.h>
#include <object_recognition_core/RecognizedObjectArray.h>

namespace bp = boost::python;
using object_recognition_core::db::ObjectId;
using object_recognition_core::common::PoseResult;

namespace
{
  // see
  // http://en.wikipedia.org/wiki/HSL_and_HSV#Converting_to_RGB
  // for points on a dark background you want somewhat lightened
  // colors generally... back off the saturation (s)
  static void
  hsv2rgb(float h, float s, float v, float& r, float& g, float& b)
  {
    float c = v * s;
    float hprime = h / 60.0;
    float x = c * (1.0 - fabs(fmodf(hprime, 2.0f) - 1));

    r = g = b = 0;

    if (hprime < 1)
    {
      r = c;
      g = x;
    }
    else if (hprime < 2)
    {
      r = x;
      g = c;
    }
    else if (hprime < 3)
    {
      g = c;
      b = x;
    }
    else if (hprime < 4)
    {
      g = x;
      b = c;
    }
    else if (hprime < 5)
    {
      r = x;
      b = c;
    }
    else if (hprime < 6)
    {
      r = c;
      b = x;
    }

    float m = v - c;
    r += m;
    g += m;
    b += m;
  }
}

namespace object_recognition_core
{
  /** Cell that takes the results of object recognition and fills the official ROS message
   */
  struct MsgAssembler
  {
    typedef geometry_msgs::PoseArrayConstPtr PoseArrayMsgPtr;
    typedef visualization_msgs::MarkerArrayConstPtr MarkerArrayMsgPtr;
    typedef visualization_msgs::MarkerArray MarkerArrayMsg;
    typedef geometry_msgs::PoseArray PoseArrayMsg;
    typedef std_msgs::StringConstPtr ObjectIdsMsgPtr;
    typedef std_msgs::String ObjectIdsMsg;

    static void
    declare_params(ecto::tendrils& params)
    {
    }

    static void
    declare_io(const ecto::tendrils& params, ecto::tendrils& inputs, ecto::tendrils& outputs)
    {
      inputs.declare<sensor_msgs::ImageConstPtr>("image_message", "the image message to get the header");
      inputs.declare(&MsgAssembler::pose_results_, "pose_results", "The results of object recognition");

      outputs.declare<PoseArrayMsgPtr>("pose_message", "The poses");
      outputs.declare<ObjectIdsMsgPtr>("object_ids_message", "The poses");
      outputs.declare<MarkerArrayMsgPtr>("marker_message", "Visualization markers for ROS.");
    }

    void
    configure(const ecto::tendrils& params, const ecto::tendrils& inputs, const ecto::tendrils& outputs)
    {
      ecto::py::scoped_call_back_to_python scb;

      image_message_ = inputs["image_message"];
      bp::object mapping;
    }

    int
    process(const ecto::tendrils& inputs, const ecto::tendrils& outputs)
    {
      PoseArrayMsg pose_array_msg;
      ObjectIdsMsg object_ids_msg;
      // Publish the info
      ros::Time time = ros::Time::now();
      if (*image_message_)
      {
        std::string frame_id = (*image_message_)->header.frame_id;
        pose_array_msg.header.frame_id = frame_id;
      }
      pose_array_msg.header.stamp = time;
      MarkerArrayMsg marker_array;

      BOOST_FOREACH(const common::PoseResult & pose_result, *pose_results_)
        if (object_id_to_index_.find(pose_result.object_id()) == object_id_to_index_.end())
          object_id_to_index_[pose_result.object_id()] = object_id_to_index_.size();

      // Create poses and fill them in the message
      {
        std::vector<geometry_msgs::Pose> &poses = pose_array_msg.poses;
        poses.resize(pose_results_->size());

        unsigned int marker_id = 0;
        BOOST_FOREACH(const common::PoseResult & pose_result, *pose_results_)
        {
          cv::Mat_<float> T = pose_result.T<cv::Mat_<float> >(), R = pose_result.R<cv::Mat_<float> >();

          geometry_msgs::Pose & msg_pose = poses[marker_id];

          Eigen::Matrix3f rotation_matrix;
          for (unsigned int j = 0; j < 3; ++j)
            for (unsigned int i = 0; i < 3; ++i)
              rotation_matrix(j, i) = R(j, i);

          Eigen::Quaternion<float> quaternion(rotation_matrix);

          msg_pose.position.x = T(0);
          msg_pose.position.y = T(1);
          msg_pose.position.z = T(2);
          msg_pose.orientation.x = quaternion.x();
          msg_pose.orientation.y = quaternion.y();
          msg_pose.orientation.z = quaternion.z();
          msg_pose.orientation.w = quaternion.w();

          visualization_msgs::Marker marker;
          marker.pose = msg_pose;
          marker.type = visualization_msgs::Marker::MESH_RESOURCE;
          marker.action = visualization_msgs::Marker::ADD;
          marker.lifetime = ros::Duration(30);
          marker.header = pose_array_msg.header;
          marker.scale.x = 1;
          marker.scale.y = 1;
          marker.scale.z = 1;

          float hue = (360.0 / object_id_to_index_.size()) * object_id_to_index_[pose_result.object_id()];

          float r, g, b;
          hsv2rgb(hue, 0.7, 1, r, g, b);

          marker.color.a = 0.75;
          marker.color.g = g;
          marker.color.b = b;
          marker.color.r = r;
          marker.id = marker_id;
          marker.mesh_resource = pose_result.get_attribute<std::string>("mesh_uri");
          marker_array.markers.push_back(marker);
          marker.type = visualization_msgs::Marker::TEXT_VIEW_FACING;
          marker.text = pose_result.get_attribute<std::string>("name");
          marker.color.a = 1;
          marker.color.g = 1;
          marker.color.b = 1;
          marker.color.r = 1;
          marker.scale.z = 0.03;
          marker.lifetime = ros::Duration(10);

          marker_array.markers.push_back(marker);
          ++marker_id;
        }
      }

      // Add the object ids to the message
      {
        or_json::mObject object_ids_param_tree;

        std::vector<or_json::mValue> object_ids_array;
        BOOST_FOREACH(const PoseResult & pose_result, *pose_results_)
          object_ids_array.push_back(or_json::mValue(pose_result.object_id()));
        object_ids_param_tree["object_ids"] = or_json::mValue(object_ids_array);

        std::stringstream ssparams;

        or_json::mValue value(object_ids_param_tree);
        or_json::write(value, ssparams);
        object_ids_msg.data = ssparams.str();
      }

      outputs["pose_message"] << PoseArrayMsgPtr(new PoseArrayMsg(pose_array_msg));
      outputs["object_ids_message"] << ObjectIdsMsgPtr(new ObjectIdsMsg(object_ids_msg));
      outputs["marker_message"] << MarkerArrayMsgPtr(new MarkerArrayMsg(marker_array));
      return 0;
    }
  private:
    ecto::spore<std::vector<common::PoseResult> > pose_results_;
    ecto::spore<sensor_msgs::ImageConstPtr> image_message_;

    static std::map<ObjectId, unsigned int> object_id_to_index_;
  };
  std::map<ObjectId, unsigned int> MsgAssembler::object_id_to_index_;
}

ECTO_CELL(io_ros, object_recognition_core::MsgAssembler, "MsgAssembler",
    "Given object ids and poses, fill the object recognition message.");