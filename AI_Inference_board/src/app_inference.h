#ifndef APP_INFERENCE_H_
#define APP_INFERENCE_H_

#include "app_types.h"

app_status_t fruit_extract_features(const app_image_t * p_image, fruit_features_t * p_features);
app_status_t fruit_run_inference(const app_image_t * p_image, fruit_result_t * p_result);

#endif /* APP_INFERENCE_H_ */
