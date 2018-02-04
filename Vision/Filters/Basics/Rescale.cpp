
#include "Filters/Basics/Rescale.hpp"

#include <opencv2/imgproc/imgproc.hpp>

namespace Vision {
namespace Filters {

Rescale::Rescale(const std::string &name, const std::string &source,
                 Frequency::type frequency)
    : Filter(name, {source}, frequency) {}

void Rescale::setParameters() {
  _ratio = ParamFloat(8, 0.0625, 64);
  params()->define<ParamFloat>("ratio", &_ratio);
}

void Rescale::process() {
  cv::Mat src = *(getDependency().getImg());
  cv::resize(src, img(), cv::Size(src.cols / _ratio, src.rows / _ratio));
}
}
}
