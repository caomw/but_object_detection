/**
 * Developed by dcgm-robotics@FIT group
 * Author: Tomas Hodan (xhodan04@stud.fit.vutbr.cz)
 * Date: 01.04.2012 (version 1.0)
 *
 * License: BUT OPEN SOURCE LICENSE
 *
 * Description:
 * Sample detector demonstrating how to wrap a detector using ObjDet API into ROS.
 *------------------------------------------------------------------------------
 */

#include <ros/ros.h> // Main header of ROS
#include <sensor_msgs/Image.h> // Image message

// OpenCV is available within a vision_opencv ROS stack
#include <opencv2/highgui/highgui.hpp>
#include <opencv2/imgproc/imgproc.hpp>
#include <cv_bridge/cv_bridge.h>

// ObjDet API
#include "but_objdet/but_objdet.h" // Main objects of ObjDet API
#include "but_objdet/services_list.h" // Names of services provided by but_objdet package
#include "but_objdet/convertor/convertor.h" // Translator from but_objdet messages to standard C++ structures
#include "but_objdet/matcher/matcher_overlap.h" // Matcher (based on overlap)
#include "but_objdet/PredictDetections.h" // Autogenerated service class
#include "but_objdet_msgs/DetectionArray.h" // Message transfering detections/predictions

#include "but_sample_detector/sample_detector_node.h"

using namespace cv;
using namespace std;
using namespace but_objdet_msgs;


/* -----------------------------------------------------------------------------
 * Constructor
 */
SampleDetectorNode::SampleDetectorNode()
{   
    sampleDetector = new SampleDetector(); // Detector
    matcherOverlap = new MatcherOverlap(); // Matcher
    
    // Create a window to show the incoming video and set its mouse event handler
    namedWindow("Sample detector", CV_WINDOW_AUTOSIZE);
    
    rosInit(); // ROS-related initialization
}


/* -----------------------------------------------------------------------------
 * Destructor
 */
SampleDetectorNode::~SampleDetectorNode()
{
    delete sampleDetector;
    delete matcherOverlap;
}


/* -----------------------------------------------------------------------------
 * ROS-related initialization
 */
void SampleDetectorNode::rosInit()
{
    // Create a client for the service for predictions of detections
    // (the name of the service is defined in but_objdet/services_list.h)
    predictClient = nh.serviceClient<but_objdet::PredictDetections>(BUT_OBJDET_PredictDetections_SRV);

    // Advertise that this node is going to publish on the specified topic
    // (the second argument is the size of publishing queue)
    detectionsPub = nh.advertise<but_objdet_msgs::DetectionArray>("/but_objdet/detections", 10);
    
    // Subscribe to the /cam3d/rgb/image_raw topic (just example for this sample
    // detector, you can subscribe to any other topics)
    dataSub = nh.subscribe("/cam3d/rgb/image_raw", 10, &SampleDetectorNode::newDataCallback, this);
    
    // Inform that the detector is running (it will be written into console)
    ROS_INFO("Sample detector is running...");
}


/* -----------------------------------------------------------------------------
 * Function called when an Image message is received on the subscribed topic
 */
void SampleDetectorNode::newDataCallback(const sensor_msgs::ImageConstPtr &imageMsg)
{   
    //ROS_INFO("New data.");

    // Get an OpenCV Mat from the image message
    Mat image;
    try {
        image = cv_bridge::toCvCopy(imageMsg)->image;
    }
    catch (cv_bridge::Exception& e) {
        ROS_ERROR("cv_bridge exception: %s", e.what());
        return;
    }
    
    // 1) Obtain predictions from tracker via service
    //--------------------------------------------------------------------------
    // When using simulated Clock time, now() returns time 0 until first message
    // has been received on /clock topic => wait for that.
    ros::Time reqTime;
    do {
        reqTime = ros::Time::now();
    } while(reqTime.sec == 0);
    
    // Instance of the autogenerated service class providing service
	// for prediction of detections
	but_objdet::PredictDetections predictSrv;

    // Create a request
    // (in this example, class_id nor object_id is specified, so predictions
    // for all detections is returned)
    //---------------------------------
    predictSrv.request.header.stamp = reqTime;
    predictSrv.request.object_id = -1;
    predictSrv.request.class_id = -1;
    
    // Send request to tracker (ROS node) to obtain predictions
    // (using PredictDetections service)
    //---------------------------------
    // Call the service (calls are blocking, it returns once the call is done)
    if(predictClient.call(predictSrv)) {
        // Translate Detection msgs to butObjects
        predictions = Convertor::detectionsToButObjects(predictSrv.response.predictions);
    }
    else {
        std::string errMsg = "Failed to call service " + BUT_OBJDET_PredictDetections_SRV + ".";
        ROS_ERROR("%s", errMsg.c_str());
    }
    
    // 2) Provide predictions to detector (so it can consider it during
    // detection process)
    //--------------------------------------------------------------------------
    sampleDetector->prediction(predictions, 0);

    // 3) Detection (sample detector returns always just one fake detection)
    //--------------------------------------------------------------------------
    sampleDetector->detect(image, Mat(), detections, 0);
    
    // 4) Match detections and predictions
    // To each detection is assigned the most similar prediction or none, if
    // there is no prediction, where the overlapping area represents at least
    // minOverlap% of both, detection BB and prediction BB (BB = Bounding Box).
    // The assigned prediction must have the same value of m_class (= class ID)
    // as the detection.
    //--------------------------------------------------------------------------
    vector<TMatch> matches;
    matcherOverlap->setMinOverlap(50); // minOverlap = 50%
    matcherOverlap->match(detections, predictions, matches);

    // 5) Modify m_id and m_class of each detection based on matched prediction
    //--------------------------------------------------------------------------
    for(unsigned int i = 0; i < matches.size(); i++) {
    
        if(matches[i].predId != -1) {
            detections[i].m_id = predictions[matches[i].predId].m_id;
        }
        else {
            // If there is no matched prediction, generate a new unique ID for it
            // (it is considered as a new, so far unseen object)
            detections[i].m_id = getNewObjectID();
        }
    }
    
    // 6) Publish new detections (it is subscribed by tracker)
    //--------------------------------------------------------------------------
    DetectionArray detArray;
    detArray.header = imageMsg->header;

    // Translate butObjects to Detection msgs
    detArray.detections = Convertor::butObjectsToDetections(detections, imageMsg->header);
    detectionsPub.publish(detArray);

    // Show the fake bounding box - just to demonstrate that the sample detector
    // works within ROS!
    //--------------------------------------------------------------------------
    cv::Rect bb = detections[0].m_bb;
	rectangle(
	    image,
	    cvPoint(bb.x, bb.y),
	    cvPoint(bb.x + bb.width, bb.y + bb.height),
	    cvScalar(255,255,255)
	);
	imshow("Sample detector", image);
}


/* =============================================================================
 * Generates a new object ID
 */
int SampleDetectorNode::getNewObjectID()
{
    // Limit the range of possible IDs
    if(lastObjectID >= 100000) lastObjectID = 0;
    
    return ++lastObjectID;
}


/* =============================================================================
 * Main function
 */
int main(int argc, char **argv)
{
    // ROS initialization (the last argument is the name of a ROS node)
    ros::init(argc, argv, "but_sample_detector");

    // Create the object managing connection with ROS system
    SampleDetectorNode *sdm = new SampleDetectorNode();
    
    // Enters a loop
    // (you can replace the following while-loop with ros::spin(); if you do not
    // want to open any window or e.g. handle a key press event)
    //--------------------------------------------------------------------------
    //ros::spin();
    while(ros::ok()) {
        waitKey(10); // Process window events
        
        // You can do some other stuff here (e.g. handle a key press event)
        ros::spinOnce(); // Call all the message callbacks waiting to be called
    }
    
    delete sdm;
    
    return 0;
}

