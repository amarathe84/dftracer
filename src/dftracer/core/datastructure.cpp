#include <dftracer/core/datastructure.h>
#include <dftracer/utils/utils.h>
namespace dftracer {

void BaseAggregatedValue::update(BaseAggregatedValue* value) {
  auto id = _id;
  DFTRACER_FOR_EACH_NUMERIC_TYPE(DFTRACER_ANY_ID_MACRO, value, {
    DFTRACER_NUM_AGGREGATE(_child, NumberAggregationValue, double);
    return;
  });
  DFTRACER_FOR_EACH_STRING_TYPE(DFTRACER_ANY_ID_MACRO, value, {
    DFTRACER_NUM_AGGREGATE(_child, AggregatedValue, double);
    return;
  });
}

BaseAggregatedValue* BaseAggregatedValue::get_value() { return _child; }
std::string Metadata::getTagValue(const std::string& tagKey) const {
  auto it = data.find(tagKey);
  if (it != data.end()) {
    DFTRACER_FOR_EACH_NUMERIC_TYPE(DFTRACER_ANY_CAST_MACRO, it->second.second,
                                   { return std::to_string(res.value()); });
    DFTRACER_FOR_EACH_STRING_TYPE(DFTRACER_ANY_CAST_MACRO, it->second.second,
                                  { return res.value(); });
  }
  return "";
}
}  // namespace dftracer