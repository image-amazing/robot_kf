#include <Eigen/Geometry>
#include <ros/ros.h>
#include <tf/transform_datatypes.h>
#include <tf/transform_broadcaster.h>
#include <tf/transform_listener.h>
#include <geometry_msgs/QuaternionStamped.h>
#include <nav_msgs/Odometry.h>
#include <sensor_msgs/Imu.h>
#include <robot_kf/robot_kf.h>

// TODO: Deal with TF frames.

static double const big = 99999.0;

static boost::shared_ptr<tf::TransformListener>    sub_tf;
static boost::shared_ptr<tf::TransformBroadcaster> pub_tf;

static ros::Subscriber sub_compass;
static ros::Subscriber sub_encoders;
static ros::Subscriber sub_gps;
static ros::Publisher  pub_fused;
static robot_kf::KalmanFilter kf;

static bool watch_compass;
static bool watch_encoders;
static bool watch_gps;
static std::string frame_id, child_frame_id;

static void publish(ros::Time stamp)
{
    Eigen::Vector3d const state = kf.getState();
    Eigen::Matrix3d const cov = kf.getCovariance();

    // Publish the odometry message.
    nav_msgs::Odometry msg;
    msg.header.stamp = ros::Time::now();
    msg.header.frame_id = frame_id;
    msg.child_frame_id = child_frame_id;
    msg.pose.pose.position.x = state[0];
    msg.pose.pose.position.y = state[1];
    tf::quaternionTFToMsg(tf::createQuaternionFromYaw(state[2]),
                          msg.pose.pose.orientation);

    Eigen::Map<Eigen::Matrix<double, 6, 6> > cov_raw(&msg.pose.covariance[0]);
    cov_raw << cov(0,0), cov(0,1), 0.0, 0.0, 0.0, cov(0,2),
               cov(1,0), cov(1,1), 0.0, 0.0, 0.0, cov(1,2),
               0.0,      0.0,      big, 0.0, 0.0, 0.0,
               0.0,      0.0,      0.0, big, 0.0, 0.0,
               0.0,      0.0,      0.0, 0.0, big, 0.0,
               cov(2,0), cov(2,1), 0.0, 0.0, 0.0, cov(2,2);
    msg.twist.covariance[0] = -1;
    pub_fused.publish(msg);

    // Publish a TF transform for the fused odometry.
    geometry_msgs::TransformStamped transform;
    transform.header.stamp    = stamp;
    transform.header.frame_id = frame_id;
    transform.child_frame_id  = child_frame_id;
    transform.transform.translation.x = state[0];
    transform.transform.translation.y = state[1];
    transform.transform.translation.z = 0.0;
    tf::quaternionTFToMsg(tf::createQuaternionFromYaw(state[2]),
                          transform.transform.rotation);
    pub_tf->sendTransform(transform);
}

static void updateCompass(sensor_msgs::Imu const &msg)
{
    double const yaw = tf::getYaw(msg.orientation);
    double const cov = msg.orientation_covariance[8];

    kf.update_compass(yaw, cov);
    if (watch_compass) publish(msg.header.stamp);
}

static void updateEncoders(nav_msgs::Odometry const &msg)
{
    Eigen::Vector3d const z = (Eigen::Vector3d() <<
        msg.pose.pose.position.x,
        msg.pose.pose.position.y,
        tf::getYaw(msg.pose.pose.orientation)
    ).finished();

    Eigen::Map<Eigen::Matrix<double, 6, 6> const> cov_raw(
        &msg.pose.covariance.front()
    );

    Eigen::Matrix3d cov_z = Eigen::Matrix3d::Zero();
    cov_z.topLeftCorner<2, 2>() = cov_raw.topLeftCorner<2, 2>();
    cov_z(2, 2) = cov_raw(5, 5);

    kf.update_encoders(z, cov_z);
    if (watch_encoders) publish(msg.header.stamp);
}

static void updateGps(nav_msgs::Odometry const &msg)
{
    Eigen::Vector2d const z = (Eigen::Vector2d() <<
        msg.pose.pose.position.x,
        msg.pose.pose.position.y
    ).finished();

    Eigen::Map<Eigen::Matrix<double, 6, 6> const> cov_raw(
        &msg.pose.covariance.front()
    );
    Eigen::Matrix2d const cov_z = cov_raw.topLeftCorner<2, 2>();

    kf.update_gps(z, cov_z);
    if (watch_gps) publish(msg.header.stamp);
}

int main(int argc, char **argv)
{
    ros::init(argc, argv, "robot_kf_node");

    ros::NodeHandle nh, nh_node("~");
    nh_node.param<bool>("watch_compass",  watch_compass,  true);
    nh_node.param<bool>("watch_encoders", watch_encoders, true);
    nh_node.param<bool>("watch_gps",      watch_gps,      true);
    nh_node.param<std::string>("frame_id", frame_id, "/map");
    nh_node.param<std::string>("child_frame_id", child_frame_id, "/odom");

    sub_tf = boost::make_shared<tf::TransformListener>();
    pub_tf = boost::make_shared<tf::TransformBroadcaster>();

    sub_compass  = nh.subscribe("compass", 1, &updateCompass);
    sub_encoders = nh.subscribe("odom", 1, &updateEncoders);
    sub_gps      = nh.subscribe("gps", 1, &updateGps);
    pub_fused    = nh.advertise<nav_msgs::Odometry>("odom_fused", 100);

    ros::spin();
    return 0;
}
