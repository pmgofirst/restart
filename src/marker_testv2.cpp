#include <ros/ros.h>
#include <stdio.h> //sprintf
#include <cv_bridge/cv_bridge.h>
#include <opencv2/opencv.hpp>
#include <opencv2/highgui.hpp>
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <vector>
#include "Eigen/Eigen/Eigen"
#include "Eigen/Eigen/Geometry"

#include <vicon_bridge/Markers.h>
#include <vicon_bridge/Marker.h>
#include <restart/pos_est.h>

using namespace cv;
using namespace std;
using namespace Eigen;

#define RADIUS_SQUARE 0.4
#define VEHICLE_SIZE 0.15//depends on crazyflies size
#define VEHICLE_EDGE_THRESHOLD 0.065 
#define VEHICLE_DRIFT 0.3//depends on crazyflies maxium velocity
#define REVISE_WEIGHT 0.1

vector<int> index_sequence;
int g_vehicle_num=1;
float about_edge = 1;
vector<float> x_init_pos;
vector<float> y_init_pos;
vector<float> x_marker_init;
vector<float> y_marker_init;
float amp_coeff;
void give_index(int index)
{
	index_sequence.push_back(index);
}
void clear_index()
{
	index_sequence.clear();
}

class MarkerTest
{
private:
	std::vector<ros::Publisher> m_pos_est_v;
	std::vector<ros::Publisher> m_predict_test;
	//ros::Publisher m_pos_est_v;
	ros::Subscriber m_viconMarkersub;
	std::vector<vicon_bridge::Marker> m_markers;
	bool isFirstVicon;
	//bool isSecondVicon;
	std::vector<Vector3f> swarm_pos;//records last time vehicle position
	std::vector<Vector3f> swarm_pos_predict;//records prediction based on last time position, error, and velocity
	std::vector<Vector3f> swarm_pos_err;//records position error for correction
	std::vector<Vector3f> swarm_pos_step;//records step(only velocity) from last two positions.
	std::vector<Vector3f> m_swarm_pos;//the modifiable version of swarm_pos

	restart::pos_est m_pos_estmsg;
	restart::pos_est m_pos_predict;
	float z_ground;
	Mat src;
public:
	MarkerTest(ros::NodeHandle& nh)
	:m_pos_est_v(g_vehicle_num),
	m_predict_test(g_vehicle_num)
	{

		m_viconMarkersub = nh.subscribe<vicon_bridge::Markers>("/vicon/markers",1000,&MarkerTest::vicon_markerCallback, this);
		isFirstVicon = true;
		//isSecondVicon = true;

		char msg_name[50];
		for(int i=0;i<g_vehicle_num;i++){
			sprintf(msg_name,"/vehicle%d/pos_est", i);
			m_pos_est_v[i] = nh.advertise<restart::pos_est>(msg_name,1);//msg_name, 1);
			sprintf(msg_name,"/vehicle%d/pos_predict", i);
			m_predict_test[i] = nh.advertise<restart::pos_est>(msg_name,1);//msg_name, 1);
		}
			
		src = Mat(Size(1000,1000), CV_8UC3, Scalar(0));

		
	}

	~MarkerTest(){}

	/*void run(double frequency)
	{
		printf("********11\n");
		ros::spin();
	}*/

	void displayFunc()
	{
		float tmp_max = 0;
		for (int i = 0; i < x_init_pos.size(); ++i)
		{
			float tmp = sqrt(x_init_pos[i]*x_init_pos[i]+y_init_pos[i]*y_init_pos[i]);
			if (tmp_max < tmp)
				tmp_max = tmp;	
		}
		amp_coeff = 400.0f/tmp_max;                        
		printf("tmp_max : %f***********amp_coeff : %f\n", tmp_max, amp_coeff);   

		namedWindow("vicon_test");	
		Point p1 = Point(50,50);
		Point p2 = Point(950,950);
		rectangle(src, p1, p2, CV_RGB(0, 0, 255), -1);


		for(int i=0;i<x_marker_init.size();i++){
			circle(src, Point(500+x_marker_init[i]*amp_coeff, 500-y_marker_init[i]*amp_coeff), 2, Scalar(0, 255, 0));  
	    	printf("x1 %d: %f\n", i, x_marker_init[i]);
	    	printf("y1 %d: %f\n", i, y_marker_init[i]);
    	}


		for(int i=0;i<x_init_pos.size();i++){
			circle(src, Point(500+x_init_pos[i]*amp_coeff, 500-y_init_pos[i]*amp_coeff), 2, Scalar(0, 255, 0));  
	    	printf("x%d: %f\n", i, x_init_pos[i]);
	    	printf("y%d: %f\n", i, y_init_pos[i]);
    	}	
		imshow("vicon_test", src);
	}

	static void onMouse(int event, int x, int y, int, void* userInput)
	{
		if (event != EVENT_LBUTTONDOWN) return;
		//printf("###########onMouse x : %d\n", x);
		//printf("###########onMouse y : %d\n", y);
		int x_world = x - 500;
		int y_world = 500 - y;
		Mat *img = (Mat*)userInput;
		circle(*img, Point(x, y), 10, Scalar(0, 0, 255));
		imshow("vicon_test", *img);

		float nearest_dist=-1.0f;
		int nearest_index=0;
		for(int i=0;i<x_init_pos.size();i++){
			float sq_dist=sqrt((x_world-x_init_pos[i]*amp_coeff)*(x_world-x_init_pos[i]*amp_coeff)+(y_world-y_init_pos[i]*amp_coeff)*(y_world-y_init_pos[i]*amp_coeff));
			//printf("############# sq_dist : %f\n", sq_dist);
			if(sq_dist<nearest_dist||nearest_dist<0){
				nearest_dist=sq_dist;
				nearest_index=i;
			}
		}
		give_index(nearest_index);
	}

	void unite()
	{
		vector<bool> all_union(x_marker_init.size(),0);
		for(int i=0;i<x_marker_init.size();++i)
		{
		    int within_circle = 0;
			for(int j=1;j<x_marker_init.size();++j)
			{
				if(sqrt((x_marker_init[i]-x_marker_init[j])*(x_marker_init[i]-x_marker_init[j])+(y_marker_init[i]-y_marker_init[j])*(y_marker_init[i]-y_marker_init[j]))<about_edge)
					++within_circle;
			}
			if(within_circle<3)
				all_union[i]=1;
		}
		for(int i=0;i<x_marker_init.size();++i)
		{
			if(all_union[i])continue;
			all_union[i]=1;
			vector<int> num_of_point(3,-1);
			vector<float> min_dstc(2,-1);
		    float temp_dstc;
		    for(int j=1;j<x_marker_init.size();++j)
		    {
		    	if(all_union[j])continue;
		    	temp_dstc=(x_marker_init[i]-x_marker_init[j])*(x_marker_init[i]-x_marker_init[j])+(y_marker_init[i]-y_marker_init[j])*(y_marker_init[i]-y_marker_init[j]);
		    	if(min_dstc[0]>temp_dstc||min_dstc[0]<0)
		    	{
		    		num_of_point[0]=j;
		    		min_dstc[0]=temp_dstc;
		    	}
		    }
		    all_union[num_of_point[0]]=1;
		    for(int j=1;j<x_marker_init.size();++j)
		    {
		    	if(all_union[j])continue;
		    	temp_dstc=(x_marker_init[i]-x_marker_init[j])*(x_marker_init[i]-x_marker_init[j])+(y_marker_init[i]-y_marker_init[j])*(y_marker_init[i]-y_marker_init[j]);
		    	if(min_dstc[1]>temp_dstc||min_dstc[1]<0)
		    	{
		    		num_of_point[1]=j;
		    		min_dstc[1]=temp_dstc;
		    	}
		    }
		    all_union[num_of_point[1]]=1;
		    temp_dstc=x_marker_init[num_of_point[0]]+x_marker_init[num_of_point[1]];
		    x_init_pos.push_back(temp_dstc/2);
		    temp_dstc=y_marker_init[num_of_point[0]]+y_marker_init[num_of_point[1]];
		    y_init_pos.push_back(temp_dstc/2);

		    float temp_cross_product=-9999;
		    for(int j=1;j<x_marker_init.size();++j)
		    {
		    	if(all_union[j])continue;
		    	float cross_product;
		    	cross_product=(x_marker_init[num_of_point[0]]-x_marker_init[j])*(x_marker_init[num_of_point[1]]-x_marker_init[j])+(y_marker_init[num_of_point[0]]-y_marker_init[j])*(y_marker_init[num_of_point[1]]-y_marker_init[j]);
		    	if(cross_product<temp_cross_product||temp_cross_product<0)
		    	{
		    		num_of_point[2]=j;
		    		temp_cross_product=cross_product;
		    	}
		    }
		    all_union[num_of_point[2]]=1;
		}
	}

	void vicon_markerCallback(const vicon_bridge::Markers::ConstPtr& msg)
	{	
		/*init*/
		//printf("****************received vicon_bridge, number: %d\n", msg->markers.size());
		m_markers = msg->markers;
		//printf("received vicon_bridge, number: %d\n", msg->markers.size());
		if (isFirstVicon && msg->markers.size() != 0)
		{
			//printf("****************received vicon_bridge, number: %d\n", msg->markers.size());
			//printf("%d\n",msg->markers.size());
			for (auto& Marker : m_markers)
    		{		
    			//printf("hhhh");
    			Vector3f pos;
    			pos(0) = Marker.translation.x/1000.0f;
    			pos(1) = Marker.translation.y/1000.0f;
    			pos(2) = Marker.translation.z/1000.0f;

    			x_marker_init.push_back(pos(0));
				y_marker_init.push_back(pos(1));
				z_ground = pos(2);
			}

    		/*for(int i=0;i<x_init_pos.size();i++)//print markers position
    		{
		    	printf("x init%d: %f\n", i, x_init_pos[i]);
		    	printf("y init%d: %f\n", i, y_init_pos[i]);
		    	fflush(stdout);
		    }*/

		    /*Sequence initialization*/
			unite();//identify crazyflies and get their position into swarm_pos

			bool sequenceIsOk = false;
			while(!sequenceIsOk)//use mouse to rearrange index of swarm_pos
			{
				displayFunc();
				setMouseCallback("vicon_test", onMouse, &src);
				waitKey();
				destroyWindow("vicon_test");
				//printf("%d\n", index_sequence.size());
				/*check the click times and exit the initialization*/
				if(index_sequence.size()==g_vehicle_num){
					sequenceIsOk = true;
				}else{
					printf("Initialization fails!! Please click again!!\n");
					clear_index();
				}
			}
			
			//printf("%d*****%d\n", index_sequence.size(), index_sequence[0]);

			for (int i=0; i<index_sequence.size();i++)
			{
				Vector3f tmp_pos;
				tmp_pos(0) = x_init_pos[index_sequence[i]];
				tmp_pos(1) = y_init_pos[index_sequence[i]];
				tmp_pos(2) = z_ground;
				swarm_pos.push_back(tmp_pos);
				m_swarm_pos.push_back(tmp_pos);

	    		Vector3f tmp_zero;
	    		tmp_zero(0) = tmp_zero(1) = tmp_zero(2) = 0;
	    		swarm_pos_step.push_back(tmp_zero);
	    		swarm_pos_predict.push_back(tmp_zero);
	    		swarm_pos_err.push_back(tmp_zero);
			}
			/*for (int i = 0; i < index_sequence.size(); ++i)//print index_sequence
			{
				//printf("%d\n", 100);
				int tmp_index = index_sequence[i];
				printf("%d", tmp_index);
				Vector3f tmp;
				tmp = swarm_pos[index_sequence[i]];
				printf("%f %f\n", tmp(0), tmp(1));
				fflush(stdout);
			}*/

    		if(swarm_pos.size()==g_vehicle_num)
    			isFirstVicon = false;
		}
		else if (!isFirstVicon && msg->markers.size() != 0)
		{
			std::vector<Vector3f> consider_pos;//markers_pos;
			for (auto& Marker : m_markers)
	    	{	
	    		Vector3f pos;
	    		pos(0) = Marker.translation.x/1000.0f;
	    		pos(1) = Marker.translation.y/1000.0f;
	    		pos(2) = Marker.translation.z/1000.0f;
	    		consider_pos.push_back(pos);
	    	}

			/*grand wipe out*/
			/*std::vector<Vector3f> consider_pos;
			for (int i=0;i<markers_pos.size();i++)
	    	{	
	    		float norm;
	    		vec3f_norm(&markers_pos[i], &norm);
	    		bool isInside = true;
	    		for (int j=0;j<swarm_pos.size();j++)
	    		{
	    			float swarm_norm;
	    			vec3f_norm(&swarm_pos[j], &swarm_norm);
	    			
	    			if(fabs(norm-swarm_norm) > RADIUS_SQUARE) //max circle
						isInside = false;
	    		}
	    		if (isInside)
	    			consider_pos.push_back(markers_pos[i]);
	    	}*/
	    	//printf("*******consider_pos size : %d\n", consider_pos.size());
			/*find vehicles*/
	    	for (int i = 0; i < g_vehicle_num; ++i)//for every vehicles
	    	{
	    		/*prediction*/
	    		swarm_pos_predict[i] = swarm_pos[i] + swarm_pos_step[i] + REVISE_WEIGHT*swarm_pos_err[i];
	    		/*small wipe out*/
	    		printf("swarm_pos_predict %d: %f %f %f\n",i, swarm_pos_predict[i](0), swarm_pos_predict[i](1), swarm_pos_predict[i](2));
	    		std::vector<Vector3f> close_points;
	    		for (int j = 0; j < consider_pos.size(); ++j)//for every considered points
	    		{
	    			Vector3f tmp_diff;
	    			float tmp_norm;	  
	    			tmp_diff(0) = consider_pos[j](0) - swarm_pos_predict[i](0);
	    			tmp_diff(1) = consider_pos[j](1) - swarm_pos_predict[i](1);
	    			tmp_diff(2) = consider_pos[j](2) - swarm_pos_predict[i](2);
	    			vec3f_norm(&tmp_diff, &tmp_norm);

	    			//printf("consider_pos: %f %f %f\n", consider_pos[j](0), consider_pos[j](1), consider_pos[j](2));
	    			//printf("consider_pos: %f %f %f\n", tmp_diff(0), tmp_diff(1), tmp_diff(2));
	    			//printf("*****inside radius : %f\n", tmp_norm);
	    			if (tmp_norm < VEHICLE_SIZE)
	    				close_points.push_back(consider_pos[j]);  			
	    		}//j
	    		printf("**********close_points : %d\n", close_points.size());
	    		/*condition 1 to 4*/
	    		if (close_points.size() >= 4)// && close_points.size() <= 4*g_vehicle_num) //condition 4
	    		{
	    			bool FoundVehicle_i = false;
					/*find vehicle center from close_points*/
		    		for (int j = 0; j < close_points.size(); ++j)
		    		{
		    			//printf("********** j : %d\n", j);
		    			//printf("FoundVehicle_i : %d\n", FoundVehicle_i);
		    			if (FoundVehicle_i)
		    				break;
		    			/*record all the vectors that based on points j*/
		    			std::vector<Vector3f> consider_vec;
		    			for (int k = 0; k < close_points.size(); ++k)
		    			{
		    				if (k != j)
		    				{
		    					Vector3f tmp_vec;
			    				tmp_vec(0) = close_points[k](0) - close_points[j](0);
			    				tmp_vec(1) = close_points[k](1) - close_points[j](1);
			    				tmp_vec(2) = close_points[k](2) - close_points[j](2);
			    				consider_vec.push_back(tmp_vec);
		    				}
		    			}
		    			/*count the number of right pairs*/
		    			for (int p = 0; p < consider_vec.size(); ++p)
		    			{
		    				//printf("********** p : %d\n", p);
		    				std::vector<Vector3f> swarm_pos_p;
		    				int count_p = 0;
		    				float len_p;
		    				vec3f_norm(&consider_vec[p], &len_p);
		    				for (int q = 0; q < consider_vec.size(); ++q)
		    				{
		    					if (q != p)
		    					{
		    						//printf("********** q : %d\n", q);
				    				float len_q;
				    				vec3f_norm(&consider_vec[q], &len_q);
				    				float ctheta = (consider_vec[p](0)*consider_vec[q](0)+consider_vec[p](1)*consider_vec[q](1)+consider_vec[p](2)*consider_vec[q](2))/(len_p*len_q);
			    					if (ctheta < 0.2 && len_q/len_p < 1.05 && len_q/len_p > 1.05)
			    					{
			    						printf("condition 1\n");
			    						Vector3f tmp_pos;
			    						tmp_pos(0) = 0.5*(consider_vec[p](0) + consider_vec[q](0)) + close_points[j](0);
			    						tmp_pos(1) = 0.5*(consider_vec[p](1) + consider_vec[q](1)) + close_points[j](1);
			    						tmp_pos(2) = 0.5*(consider_vec[p](2) + consider_vec[q](2)) + close_points[j](2);
			    						swarm_pos_p.push_back(tmp_pos);
			    						count_p++;
			    					} else if (ctheta < 0.75 && ctheta > 0.65 && len_q/len_p < 1.45 && len_q/len_p > 1.35)
			    					{
			    						printf("condition 2\n");
			    						Vector3f tmp_pos;
			    						tmp_pos(0) = 0.5*consider_vec[q](0) + close_points[j](0);
			    						tmp_pos(1) = 0.5*consider_vec[q](1) + close_points[j](1);
			    						tmp_pos(2) = 0.5*consider_vec[q](2) + close_points[j](2);
			    						swarm_pos_p.push_back(tmp_pos);
			    						count_p++;
			    					} else if (ctheta < 0.75 && ctheta > 0.65 && len_p/len_q < 1.45 && len_p/len_q > 1.35)
			    					{
			    						printf("condition 3\n");
			    						Vector3f tmp_pos;
			    						tmp_pos(0) = 0.5*consider_vec[p](0) + close_points[j](0);
			    						tmp_pos(1) = 0.5*consider_vec[p](1) + close_points[j](1);
			    						tmp_pos(2) = 0.5*consider_vec[p](2) + close_points[j](2);
			    						swarm_pos_p.push_back(tmp_pos);
			    						count_p++;
			    					}
		    					}//if
		    				}//for q
		    				if (count_p == 2)
		    				{
		    					m_swarm_pos[i](0) = (swarm_pos_p[0](0) + swarm_pos_p[1](0))/2;
		    					m_swarm_pos[i](1) = (swarm_pos_p[0](1) + swarm_pos_p[1](1))/2;
		    					m_swarm_pos[i](2) = (swarm_pos_p[0](2) + swarm_pos_p[1](2))/2;
		    					FoundVehicle_i = true;
		    					break;
		    				} else if (count_p == 1)
		    				{
		    					m_swarm_pos[i] = swarm_pos_p[0];
		    					FoundVehicle_i = true;
		    					break;
		    				}		
		    			}//for p
		    		}//for j
		    		if (!FoundVehicle_i)
    				{
    				printf("*****condition 4 failed! failure number : 1\n");
    				}	

	    		}else if (close_points.size() == 3) //condition 3
	    		{
	    			//printf("*****condition 3\n");
	    			Vector3f tmp_vec_1;
	    			float tmp_len_1;
	    			tmp_vec_1(0) = close_points[1](0) - close_points[0](0);
	    			tmp_vec_1(1) = close_points[1](1) - close_points[0](1);
	    			tmp_vec_1(2) = close_points[1](2) - close_points[0](2);
	    			vec3f_norm(&tmp_vec_1, &tmp_len_1);

	    			Vector3f tmp_vec_2;
	    			float tmp_len_2;
	    			tmp_vec_2(0) = close_points[2](0) - close_points[0](0);
	    			tmp_vec_2(1) = close_points[2](1) - close_points[0](1);
	    			tmp_vec_2(2) = close_points[2](2) - close_points[0](2);
	    			vec3f_norm(&tmp_vec_2, &tmp_len_2);
	    			
	    			float ctheta = (tmp_vec_1(0)*tmp_vec_2(0)+tmp_vec_1(1)*tmp_vec_2(1)+tmp_vec_1(2)*tmp_vec_2(2))/(tmp_len_1*tmp_len_2);
	    			if (ctheta < 0.75 && ctheta > 0.65)
	    			{
	    				if (tmp_len_2/tmp_len_1 < 1.45 && tmp_len_2/tmp_len_1 > 1.35)
	    				{
	    					Vector3f tmp;
			    			tmp(0) = 0.5*(close_points[2](0) + close_points[0](0));
			    			tmp(1) = 0.5*(close_points[2](1) + close_points[0](1));
			    			tmp(2) = 0.5*(close_points[2](2) + close_points[0](2));	
			    			m_swarm_pos[i] = tmp;
	    				} else if (tmp_len_1/tmp_len_2 < 1.45 && tmp_len_1/tmp_len_2 > 1.35)
	    				{
	    					Vector3f tmp;
			    			tmp(0) = 0.5*(close_points[1](0) + close_points[0](0));
			    			tmp(1) = 0.5*(close_points[1](1) + close_points[0](1));
			    			tmp(2) = 0.5*(close_points[1](2) + close_points[0](2));	
			    			m_swarm_pos[i] = tmp;
	    				} else{
	    					printf("*****condition 3 failed! failure number : 1\n");
	    				}
	    			} else if (ctheta < 0.1 && fabs(tmp_len_2-tmp_len_1)<0.2)
	    			{
	    				Vector3f tmp;
			    		tmp(0) = 0.5*(close_points[2](0) + close_points[1](0));
			    		tmp(1) = 0.5*(close_points[2](1) + close_points[1](1));
			    		tmp(2) = 0.5*(close_points[2](2) + close_points[1](2));	
			    		m_swarm_pos[i] = tmp;
	    			} else {
	    				printf("*****condition 3 failed! failure number : 2\n");
	    			}
	    		} else if (close_points.size() == 2) //condition 2
	    		{
	    			//printf("*****condition 2\n");
    				Vector3f tmp_vec;
    				float tmp_len;
		    		tmp_vec(0) = close_points[1](0) - close_points[0](0);
		    		tmp_vec(1) = close_points[1](1) - close_points[0](1);
		    		tmp_vec(2) = close_points[1](2) - close_points[0](2);	
		    		vec3f_norm(&tmp_vec, &tmp_len);

		    		if (tmp_len > VEHICLE_EDGE_THRESHOLD && tmp_len < VEHICLE_EDGE_THRESHOLD+0.02)
		    		{
		    			Vector3f tmp;
			    		tmp(0) = 0.5*(close_points[1](0) + close_points[0](0));
			    		tmp(1) = 0.5*(close_points[1](1) + close_points[0](1));
			    		tmp(2) = 0.5*(close_points[1](2) + close_points[0](2));	
			    		m_swarm_pos[i] = tmp;
		    		} else if (tmp_len < VEHICLE_EDGE_THRESHOLD && tmp_len > VEHICLE_EDGE_THRESHOLD-0.02)
		    		{
		    			Vector3f center_pos;
		    			center_pos(0) = 0.5*(close_points[1](0) + close_points[0](0));
			    		center_pos(1) = 0.5*(close_points[1](1) + close_points[0](1));
			    		center_pos(2) = 0.5*(close_points[1](2) + close_points[0](2));	
			    		Vector3f predict_diff;
			    		predict_diff(0) = swarm_pos_predict[i](0) - center_pos(0);
			    		predict_diff(1) = swarm_pos_predict[i](1) - center_pos(1);
			    		predict_diff(2) = swarm_pos_predict[i](2) - center_pos(2);
			    		float tmp_dist;
			    		vec3f_norm(&predict_diff, &tmp_dist);

			    		float ratio = 0.035/tmp_dist;
			    		Vector3f tmp;
			    		tmp(0) = (1-ratio)*center_pos(0) + ratio*swarm_pos_predict[0](0);
			    		tmp(1) = (1-ratio)*center_pos(1) + ratio*swarm_pos_predict[0](1);
			    		tmp(2) = (1-ratio)*center_pos(2) + ratio*swarm_pos_predict[0](2);	
			    		m_swarm_pos[i] = tmp;
		    		} else {
		    			printf("*****condition 2 failed! failure number : 0\n");
		    		}

	    		} else if (close_points.size() == 1)
	    		{
	    			//printf("*****condition 1\n");
	    			Vector3f predict_diff;
		    		predict_diff(0) = swarm_pos_predict[i](0) - close_points[0](0);
		    		predict_diff(1) = swarm_pos_predict[i](1) - close_points[0](1);
		    		predict_diff(2) = swarm_pos_predict[i](2) - close_points[0](2);
		    		float tmp_dist;
		    		vec3f_norm(&predict_diff, &tmp_dist);

		    		float ratio = 0.035/tmp_dist;
		    		Vector3f tmp;
		    		tmp(0) = (1-ratio)*close_points[0](0) + ratio*swarm_pos_predict[0](0);
		    		tmp(1) = (1-ratio)*close_points[0](1) + ratio*swarm_pos_predict[0](1);
		    		tmp(2) = (1-ratio)*close_points[0](2) + ratio*swarm_pos_predict[0](2);	
		    		m_swarm_pos[i] = tmp;
	    		} else {
	    			printf("*****Cannot find vehicle%d\n", i);
	    			continue;
	    		}

	    		/*renew error*/
	    		swarm_pos_err[i](0) = m_swarm_pos[i](0) - swarm_pos_predict[i](0);
	    		swarm_pos_err[i](1) = m_swarm_pos[i](1) - swarm_pos_predict[i](1);
	    		swarm_pos_err[i](2) = m_swarm_pos[i](2) - swarm_pos_predict[i](2);

	    	}//i

			/*renew and publish*/
	    	for (int i = 0; i < swarm_pos.size(); ++i)
	    	{
	    		swarm_pos_step[i](0) = m_swarm_pos[i](0) - swarm_pos[i](0);
	    		swarm_pos_step[i](1) = m_swarm_pos[i](1) - swarm_pos[i](1);
	    		swarm_pos_step[i](2) = m_swarm_pos[i](2) - swarm_pos[i](2);

	    		swarm_pos[i] = m_swarm_pos[i];
	    		m_pos_estmsg.pos_est.x = swarm_pos[i](0);
				m_pos_estmsg.pos_est.y = swarm_pos[i](1);
				m_pos_estmsg.pos_est.z = swarm_pos[i](2);
				m_pos_estmsg.vehicle_index = i;
	    		m_pos_est_v[i].publish(m_pos_estmsg);

	    		m_pos_predict.pos_est.x = swarm_pos_predict[i](0);
				m_pos_predict.pos_est.y = swarm_pos_predict[i](1);
				m_pos_predict.pos_est.z = swarm_pos_predict[i](2);
				m_pos_predict.vehicle_index = i;
	    		m_predict_test[i].publish(m_pos_predict);
	    		printf("*****vehicle%d: %f  %f  %f\n", i, swarm_pos[i](0), swarm_pos[i](1), swarm_pos[i](2));
	    	}
		}//if isFirstVicon
	}

	void vec3f_norm(const Vector3f* a, float* anwser)
	{
		*anwser = sqrtf((*a)(0)*(*a)(0) + (*a)(1)*(*a)(1) + (*a)(2)*(*a)(2));
	}
};

int main(int argc, char **argv)
{
	ros::init(argc, argv, "marker_testv2");
	ros::NodeHandle n;

	MarkerTest m(n);
	//m.run(50);

	ros::spin();
	return 0;
}