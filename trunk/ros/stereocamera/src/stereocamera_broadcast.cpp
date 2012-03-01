/*
    ROS driver to broadcast stereo images from a stereo webcam (eg. Minoru)
    This doesn't do any stsreo correspondence.  It merely broadcasts the images.
    Copyright (C) 2012 Bob Mottram
    fuzzgun@gmail.com

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

#include <ros/ros.h>
#include <std_msgs/String.h>
#include <sensor_msgs/Image.h>
#include <sensor_msgs/SetCameraInfo.h>
#include <image_transport/image_transport.h>
#include <sstream>

#include <iostream>
#include <stdio.h>

#include "libcam.h"

std::string dev_left = "";
std::string dev_right = "";
int fps = 30;

Camera *left_camera = NULL;
Camera *right_camera = NULL;
sensor_msgs::Image left_image;
sensor_msgs::Image right_image;

IplImage *l=NULL;
IplImage *r=NULL;
unsigned char *l_=NULL;
unsigned char *r_=NULL;
unsigned char * buffer=NULL;

bool flip_left_image=false;
bool flip_right_image=false;

std::string left_camera_filename = "left_camera.txt";
std::string right_camera_filename = "right_camera.txt";
std::string stereo_camera_filename = "stereo_camera.txt";

image_transport::Publisher left_pub, right_pub;

bool save_camera_info(std::string filename, sensor_msgs::SetCameraInfo::Request &info)
{
  FILE * fp;

  fp = fopen(filename.c_str(),"w");
  if (fp!=NULL) {
    fprintf(fp,"%d,%d,\n",info.camera_info.width,info.camera_info.height);
    fprintf(fp,"%s,\n",info.camera_info.distortion_model.c_str());
    for (int i = 0; i<info.camera_info.D.size();i++) {
      fprintf(fp,"%.10f,",info.camera_info.D[i]);
    }
    fprintf(fp,"\n");
    for (int i = 0;i<9;i++) {
      fprintf(fp,"%.10f,",info.camera_info.K[i]);
    }
    fprintf(fp,"\n");
    for (int i = 0;i<9;i++) {
      fprintf(fp,"%.10f,",info.camera_info.R[i]);
    }
    fprintf(fp,"\n");
    for (int i = 0;i<12;i++) {
      fprintf(fp,"%.10f,",info.camera_info.P[i]);
    }
    fprintf(fp,"\n");
    fprintf(fp,"%d,%d\n",info.camera_info.binning_x,info.camera_info.binning_y);
    fclose(fp);
    return true;
  }

  return false;
}

bool set_camera_info_left(
			  sensor_msgs::SetCameraInfo::Request &req,
			  sensor_msgs::SetCameraInfo::Response &res)
{
  ROS_INFO("Set camera info left");
  res.status_message = "";
  res.success = true;
  return save_camera_info(left_camera_filename,req);
}

bool set_camera_info_right(
			   sensor_msgs::SetCameraInfo::Request &req,
			   sensor_msgs::SetCameraInfo::Response &res)
{
  ROS_INFO("Set camera info right");
  res.status_message = "";
  res.success = true;
  return save_camera_info(right_camera_filename,req);
}

bool set_camera_info(
		     sensor_msgs::SetCameraInfo::Request &req,
		     sensor_msgs::SetCameraInfo::Response &res)
{
  ROS_INFO("Set camera info");
  res.success = true;
  res.status_message = "";
  return save_camera_info(stereo_camera_filename,req);
}

/*!
 * \brief stop the stereo camera
 * \param left_camera left camera object
 * \param right_camera right camera object
 */
void stop_cameras(
		  Camera *&left_camera,
		  Camera *&right_camera)
{
  if (left_camera != NULL) {
    delete left_camera;
    delete right_camera;
    left_camera = NULL;
    right_camera = NULL;
  }
}

void start_cameras(
		   Camera *&left_camera,
		   Camera *&right_camera,
		   std::string dev_left, std::string dev_right,
		   int width, int height,
		   int fps)
{
  if (left_camera != NULL) {
    stop_cameras(left_camera,right_camera);
  }

  ros::NodeHandle n;
  image_transport::ImageTransport it(n);

  std::string topic_str = "stereocamera/left/image_raw";
  left_pub = it.advertise(topic_str.c_str(), 1);

  topic_str = "stereocamera/right/image_raw";
  right_pub = it.advertise(topic_str, 1);

  left_image.width  = width;
  left_image.height = height;
  left_image.step = width * 3;
  left_image.encoding = "bgr8";
  left_image.data.resize(width*height*3);

  right_image.width  = width;
  right_image.height = height;
  right_image.step = width * 3;
  right_image.encoding = "bgr8";
  right_image.data.resize(width*height*3);

  l = cvCreateImage(cvSize(width, height), 8, 3);
  r = cvCreateImage(cvSize(width, height), 8, 3);

  l_=(unsigned char *)l->imageData;
  r_=(unsigned char *)r->imageData;

  left_camera = new Camera(dev_left.c_str(), width, height, fps);
  right_camera = new Camera(dev_right.c_str(), width, height, fps);
}

// flip the given image so that the camera can be mounted upside down
void flip(unsigned char* raw_image, unsigned char* flipped_frame_buf) {
  int max = left_image.width * left_image.height * 3;
  for (int i = 0; i < max; i += 3) {
    flipped_frame_buf[i] = raw_image[(max - 3 - i)];
    flipped_frame_buf[i + 1] = raw_image[(max - 3 - i + 1)];
    flipped_frame_buf[i + 2] = raw_image[(max - 3 - i + 2)];
  }
  memcpy(raw_image, flipped_frame_buf, max * sizeof(unsigned char));
}

bool grab_images()
{
  if ((left_camera==NULL) || (right_camera==NULL)) return false;

  // Read image data
  while ((left_camera->Get() == 0) || (right_camera->Get() == 0)) {
    usleep(100);
  }

  // Convert to IplImage
  left_camera->toIplImage(l);
  right_camera->toIplImage(r);

  // flip images
  if (flip_left_image) {
    if (buffer == NULL) {
      buffer = new unsigned char[left_image.width * left_image.height * 3];
    }
    flip(l_, buffer);
  }

  if (flip_right_image) {
    if (buffer == NULL) {
      buffer = new unsigned char[left_image.width * left_image.height * 3];
    }
    flip(r_, buffer);
  }

  return true;
}

void publish_images()
{
  // Convert to sensor_msgs::Image
  memcpy ((void*)(&left_image.data[0]), (void*)l_, left_image.width*left_image.height*3);
  memcpy ((void*)(&right_image.data[0]), (void*)r_, right_image.width*right_image.height*3);

  left_pub.publish(left_image);
  right_pub.publish(right_image);
}

void cleanup()
{
  if (l_ != NULL) {
    cvReleaseImage(&l);
    cvReleaseImage(&r);
  }

  if (buffer != NULL) delete[] buffer;
    
  stop_cameras(left_camera,right_camera);
}

int main(int argc, char** argv)
{
  ros::init(argc, argv, "stereocamera_broadcast");
  ros::NodeHandle nh("~");

  // set some default values
  int val=0;
  int width=320;
  int height=240;
  std::string str="";
  dev_left = "/dev/video1";
  dev_right = "/dev/video0";
  fps = 30;

  nh.getParam("width", val);
  if (val>0) width=val;
  nh.getParam("height", val);
  if (val>0) height=val;
  nh.getParam("fps", val);
  if (val>0) fps=val;
  nh.getParam("dev_left", str);
  if (str!="") dev_left=str;
  nh.getParam("dev_right", str);
  if (str!="") dev_right=str;
  nh.getParam("flip_left", flip_left_image);
  nh.getParam("flip_right", flip_right_image);

  nh.getParam("left_camera_filename", str);
  if (str!="") left_camera_filename=str;
  nh.getParam("right_camera_filename", str);
  if (str!="") right_camera_filename=str;
  nh.getParam("stereo_camera_filename", str);
  if (str!="") stereo_camera_filename=str;
  ROS_INFO("%s",stereo_camera_filename.c_str());

  start_cameras(left_camera,right_camera,
		dev_left, dev_right,
		width, height, fps);

  ros::NodeHandle n;

  ros::ServiceServer set_cam_info_left = n.advertiseService("stereocamera/left/set_camera_info", set_camera_info_left);
  ros::ServiceServer set_cam_info_right = n.advertiseService("stereocamera/right/set_camera_info", set_camera_info_right);
  ros::ServiceServer set_cam_info = n.advertiseService("stereocamera/set_camera_info", set_camera_info);

  bool publishing = false;
  while (n.ok()) {
    if (grab_images()) {
      if (!publishing) {
	ROS_INFO("Publishing stereo images...");
	publishing = true;
      }
      publish_images();
    }
    ros::spinOnce();
  }

  cleanup();
  ROS_INFO("Stereo camera stopped");
}

