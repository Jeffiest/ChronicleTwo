#include "common.h"

#define CAMERA_CONTROL_USE_RETAIL_ASSIGNMENT
#include "cameracontrol.hpp"
#undef CAMERA_CONTROL_USE_RETAIL_ASSIGNMENT

CameraCtrlParam &CameraCtrlParam::operator=(const CameraCtrlParam &source) {
    min_dist = source.min_dist;
    max_dist = source.max_dist;
    near_height = source.near_height;
    far_height = source.far_height;
    height = source.height;
    max_height = source.max_height;
    min_height = source.min_height;
    rest_max_height = source.rest_max_height;
    rest_min_height = source.rest_min_height;
    ground_space = source.ground_space;
    no_check = source.no_check;
    return *this;
}
