#include "app_inference.h"
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include "app_config.h"

#if APP_USE_EAI_TRANSLATOR_INFERENCE
#include "Translator/Typedef.h"
#endif

#if APP_USE_EAI_TRANSLATOR_INFERENCE
TsOUT * dnn_compute_fruit(TsIN * p_input, TsInt * p_errorcode);

typedef struct st_fruit_region_component
{
    uint16_t label;
    uint32_t area;
    uint32_t min_x;
    uint32_t min_y;
    uint32_t max_x;
    uint32_t max_y;
} fruit_region_component_t;

typedef struct st_fruit_rule_features
{
    uint32_t fruit_area;
    uint32_t orange_count;
    uint32_t greenish_count;
    uint32_t strong_greenish_count;
    uint32_t defect_count;
    uint32_t max_defect_component;
    uint32_t center_defect_count;
    uint32_t max_center_defect_component;
    uint32_t speckle_defect_count;
    uint32_t speckle_defect_components;
    uint32_t speckle_min_x;
    uint32_t speckle_min_y;
    uint32_t speckle_max_x;
    uint32_t speckle_max_y;
} fruit_rule_features_t;

static const app_image_t * fruit_prepare_eai_input(const app_image_t * p_image, bool * p_region_applied);
static bool fruit_extract_region_input(const app_image_t * p_src, app_image_t * p_dst);
static bool fruit_pixel_is_region_seed(uint8_t r, uint8_t g, uint8_t b);
static void fruit_region_reset_component(fruit_region_component_t * p_component, uint16_t label);
static bool fruit_region_component_is_better(const fruit_region_component_t * p_candidate,
                                             const fruit_region_component_t * p_best);
static bool fruit_region_component_overlaps_center(const fruit_region_component_t * p_component);
static uint32_t fruit_region_component_center_distance(const fruit_region_component_t * p_component);
static void fruit_region_try_enqueue(const app_image_t * p_src, uint32_t x, uint32_t y,
                                     uint16_t label, uint32_t * p_tail);
static void fruit_region_copy_row_spans(const app_image_t * p_src, app_image_t * p_dst, uint16_t label);
static bool fruit_rule_normalize_rgb(uint8_t r, uint8_t g, uint8_t b,
                                     uint32_t * p_r_norm,
                                     uint32_t * p_g_norm,
                                     uint32_t * p_b_norm);
#if APP_RULE_CLASSIFIER_ENABLED
static bool fruit_apply_rule_classifier(const app_image_t * p_image, fruit_result_t * p_result);
#endif
static void fruit_rule_extract_features(const app_image_t * p_image, fruit_rule_features_t * p_features);
static bool fruit_rule_pixel_is_fruit(uint8_t r, uint8_t g, uint8_t b);
static bool fruit_rule_pixel_is_orange(uint8_t r, uint8_t g, uint8_t b);
static bool fruit_rule_pixel_is_greenish(uint8_t r, uint8_t g, uint8_t b);
static bool fruit_rule_pixel_is_strong_greenish(uint8_t r, uint8_t g, uint8_t b);
static bool fruit_rule_pixel_is_defect(const app_image_t * p_image, uint32_t x, uint32_t y);
static bool fruit_rule_pixel_is_center_ignored(uint32_t x, uint32_t y);
static bool fruit_rule_pixel_is_center_defect(const app_image_t * p_image, uint32_t x, uint32_t y);
static bool fruit_rule_pixel_is_speckle(const app_image_t * p_image, uint32_t x, uint32_t y);
static void fruit_apply_safe_a_filter(const app_image_t * p_image, fruit_result_t * p_result);
static void fruit_rule_try_enqueue_defect(const app_image_t * p_image, uint32_t x, uint32_t y,
                                          uint16_t label, uint32_t * p_tail);
static void fruit_rule_try_enqueue_center_defect(const app_image_t * p_image, uint32_t x, uint32_t y,
                                                 uint16_t label, uint32_t * p_tail);
static void fruit_rule_try_enqueue_speckle(const app_image_t * p_image, uint32_t x, uint32_t y,
                                           uint16_t label, uint32_t * p_tail);
static uint32_t fruit_rule_permille(uint32_t count, uint32_t total);
#if APP_RULE_CLASSIFIER_ENABLED
static uint8_t fruit_rule_score_from_permille(uint32_t permille);
#endif
static uint8_t fruit_rule_score_from_component(uint32_t component_pixels);
static fruit_grade_t fruit_grade_from_eai_index(uint32_t index);
static uint8_t fruit_score_from_eai_output(TsOUT output_value);
static void fruit_apply_eai_output_filter(fruit_result_t * p_result, uint8_t second_score);
static void fruit_apply_unknown_output_policy(const app_image_t * p_image, fruit_result_t * p_result);
#else
static app_status_t fruit_run_threshold_inference(const app_image_t * p_image, fruit_result_t * p_result);
#endif
static uint8_t fruit_get_pixel_gray(const app_image_t * p_image, uint32_t pixel_index);

#if APP_USE_EAI_TRANSLATOR_INFERENCE && APP_FRUIT_REGION_EXTRACT_ENABLED
static app_image_t g_eai_region_image;
static uint16_t g_region_labels[APP_IMAGE_PIXEL_COUNT];
static uint16_t g_region_queue[APP_IMAGE_PIXEL_COUNT];
static uint8_t g_region_row_min[APP_IMAGE_HEIGHT];
static uint8_t g_region_row_max[APP_IMAGE_HEIGHT];
#endif

app_status_t fruit_extract_features(const app_image_t * p_image, fruit_features_t * p_features)
{
    uint32_t sum = 0U;
    uint32_t area = 0U;
    uint8_t min_gray = 255U;
    uint8_t max_gray = 0U;

    if ((NULL == p_image) || (NULL == p_features))
    {
        return APP_STATUS_INVALID_ARGUMENT;
    }

    for (uint32_t i = 0U; i < APP_IMAGE_PIXEL_COUNT; i++)
    {
        uint8_t pixel = fruit_get_pixel_gray(p_image, i);

        sum += (uint32_t) pixel;

        if (pixel < min_gray)
        {
            min_gray = pixel;
        }

        if (pixel > max_gray)
        {
            max_gray = pixel;
        }

        if (pixel > APP_BACKGROUND_THRESHOLD)
        {
            area++;
        }
    }

    p_features->area = area;
    p_features->mean_gray = (uint8_t) (sum / APP_IMAGE_PIXEL_COUNT);
    p_features->contrast = (uint8_t) (max_gray - min_gray);

    return APP_STATUS_OK;
}

static uint8_t fruit_get_pixel_gray(const app_image_t * p_image, uint32_t pixel_index)
{
    uint32_t base_index = pixel_index * APP_IMAGE_CHANNELS;

    if ((NULL == p_image) || (pixel_index >= APP_IMAGE_PIXEL_COUNT))
    {
        return 0U;
    }

    if (APP_IMAGE_CHANNELS >= 3U)
    {
        uint8_t r = p_image->pixels[base_index];
        uint8_t g = p_image->pixels[base_index + 1U];
        uint8_t b = p_image->pixels[base_index + 2U];
        uint32_t gray = (77U * (uint32_t) r) + (150U * (uint32_t) g) + (29U * (uint32_t) b);

        return (uint8_t) (gray >> 8);
    }

    return p_image->pixels[base_index];
}

app_status_t fruit_run_inference(const app_image_t * p_image, fruit_result_t * p_result)
{
#if APP_USE_EAI_TRANSLATOR_INFERENCE
    TsInt errorcode = 0;
    TsOUT * p_output;
    const app_image_t * p_model_image;
    uint32_t best_index = 0U;
    TsOUT best_value = 0U;
    TsOUT second_value = 0U;
    uint8_t second_score;
    bool region_applied = false;

    if ((NULL == p_image) || (NULL == p_result))
    {
        return APP_STATUS_INVALID_ARGUMENT;
    }

    memset(p_result->model_scores, 0, sizeof(p_result->model_scores));

    p_model_image = fruit_prepare_eai_input(p_image, &region_applied);

#if APP_RULE_CLASSIFIER_ENABLED
    if (fruit_apply_rule_classifier(p_model_image, p_result))
    {
        fruit_apply_unknown_output_policy(p_model_image, p_result);
        return APP_STATUS_OK;
    }
#endif

    p_output = dnn_compute_fruit((TsIN *) p_model_image->pixels, &errorcode);

    if ((0 != errorcode) || (NULL == p_output))
    {
        p_result->grade = FRUIT_GRADE_UNKNOWN;
        p_result->score = 0U;
        p_result->status = "EAI_ERROR";
        return APP_STATUS_IO_ERROR;
    }

    best_value = p_output[0];

    /* Use all model outputs: A, B, C, UNKNOWN. */
    for (uint32_t i = 1U; i < APP_INFERENCE_OUTPUT_CLASS_COUNT; i++)
    {
        if (p_output[i] > best_value)
        {
            second_value = best_value;
            best_value = p_output[i];
            best_index = i;
        }
        else if (p_output[i] > second_value)
        {
            second_value = p_output[i];
        }
    }

    for (uint32_t i = 0U; i < APP_INFERENCE_OUTPUT_CLASS_COUNT; i++)
    {
        p_result->model_scores[i] = fruit_score_from_eai_output(p_output[i]);
    }

    p_result->grade = fruit_grade_from_eai_index(best_index);
    p_result->score = fruit_score_from_eai_output(best_value);
    p_result->status = region_applied ? "EAI_OK_ROI" : "EAI_OK_RAW";
    second_score = fruit_score_from_eai_output(second_value);

    fruit_apply_eai_output_filter(p_result, second_score);
    fruit_apply_safe_a_filter(p_model_image, p_result);
    fruit_apply_unknown_output_policy(p_model_image, p_result);

    return APP_STATUS_OK;
#else
    return fruit_run_threshold_inference(p_image, p_result);
#endif
}

#if APP_USE_EAI_TRANSLATOR_INFERENCE
static const app_image_t * fruit_prepare_eai_input(const app_image_t * p_image, bool * p_region_applied)
{
    if (NULL != p_region_applied)
    {
        *p_region_applied = false;
    }

#if APP_FRUIT_REGION_EXTRACT_ENABLED
    if ((APP_IMAGE_CHANNELS >= 3U) && fruit_extract_region_input(p_image, &g_eai_region_image))
    {
        if (NULL != p_region_applied)
        {
            *p_region_applied = true;
        }

        return &g_eai_region_image;
    }
#endif

    return p_image;
}

static bool fruit_extract_region_input(const app_image_t * p_src, app_image_t * p_dst)
{
    fruit_region_component_t best_component;
    uint16_t next_label = 1U;

    if ((NULL == p_src) || (NULL == p_dst) || (APP_IMAGE_CHANNELS < 3U))
    {
        return false;
    }

    fruit_region_reset_component(&best_component, 0U);
    memset(g_region_labels, 0, sizeof(g_region_labels));

    for (uint32_t y = 0U; y < APP_IMAGE_HEIGHT; y++)
    {
        for (uint32_t x = 0U; x < APP_IMAGE_WIDTH; x++)
        {
            uint32_t pixel_index = (y * APP_IMAGE_WIDTH) + x;
            fruit_region_component_t component;
            uint32_t head = 0U;
            uint32_t tail = 0U;

            if (0U != g_region_labels[pixel_index])
            {
                continue;
            }

            fruit_region_try_enqueue(p_src, x, y, next_label, &tail);

            if (0U == tail)
            {
                continue;
            }

            fruit_region_reset_component(&component, next_label);

            while (head < tail)
            {
                uint32_t current = (uint32_t) g_region_queue[head++];
                uint32_t current_x = current % APP_IMAGE_WIDTH;
                uint32_t current_y = current / APP_IMAGE_WIDTH;

                component.area++;

                if (current_x < component.min_x)
                {
                    component.min_x = current_x;
                }

                if (current_x > component.max_x)
                {
                    component.max_x = current_x;
                }

                if (current_y < component.min_y)
                {
                    component.min_y = current_y;
                }

                if (current_y > component.max_y)
                {
                    component.max_y = current_y;
                }

                if (current_x > 0U)
                {
                    fruit_region_try_enqueue(p_src, current_x - 1U, current_y, next_label, &tail);
                }

                if ((current_x + 1U) < APP_IMAGE_WIDTH)
                {
                    fruit_region_try_enqueue(p_src, current_x + 1U, current_y, next_label, &tail);
                }

                if (current_y > 0U)
                {
                    fruit_region_try_enqueue(p_src, current_x, current_y - 1U, next_label, &tail);
                }

                if ((current_y + 1U) < APP_IMAGE_HEIGHT)
                {
                    fruit_region_try_enqueue(p_src, current_x, current_y + 1U, next_label, &tail);
                }
            }

            if (fruit_region_component_is_better(&component, &best_component))
            {
                best_component = component;
            }

            if (next_label < UINT16_MAX)
            {
                next_label++;
            }
        }
    }

    if (0U == best_component.label)
    {
        return false;
    }

    for (uint32_t i = 0U; i < APP_IMAGE_SIZE; i++)
    {
        p_dst->pixels[i] = APP_FRUIT_REGION_BACKGROUND_VALUE;
    }

    fruit_region_copy_row_spans(p_src, p_dst, best_component.label);

    return true;
}

static void fruit_region_reset_component(fruit_region_component_t * p_component, uint16_t label)
{
    if (NULL == p_component)
    {
        return;
    }

    p_component->label = label;
    p_component->area = 0U;
    p_component->min_x = APP_IMAGE_WIDTH;
    p_component->min_y = APP_IMAGE_HEIGHT;
    p_component->max_x = 0U;
    p_component->max_y = 0U;
}

static bool fruit_region_component_is_better(const fruit_region_component_t * p_candidate,
                                             const fruit_region_component_t * p_best)
{
    bool candidate_center;
    bool best_center;

    if ((NULL == p_candidate) || (p_candidate->area < APP_FRUIT_REGION_MIN_SEED_PIXELS))
    {
        return false;
    }

    if ((NULL == p_best) || (0U == p_best->label))
    {
        return true;
    }

    candidate_center = fruit_region_component_overlaps_center(p_candidate);
    best_center = fruit_region_component_overlaps_center(p_best);

    if (candidate_center != best_center)
    {
        return candidate_center;
    }

    if (p_candidate->area != p_best->area)
    {
        return p_candidate->area > p_best->area;
    }

    return fruit_region_component_center_distance(p_candidate) < fruit_region_component_center_distance(p_best);
}

static bool fruit_region_component_overlaps_center(const fruit_region_component_t * p_component)
{
    uint32_t center_min_x = 0U;
    uint32_t center_min_y = 0U;
    uint32_t center_max_x = APP_IMAGE_WIDTH - 1U;
    uint32_t center_max_y = APP_IMAGE_HEIGHT - 1U;

    if (NULL == p_component)
    {
        return false;
    }

    if (APP_FRUIT_REGION_CENTER_WINDOW_PIXELS < APP_IMAGE_WIDTH)
    {
        center_min_x = (APP_IMAGE_WIDTH - APP_FRUIT_REGION_CENTER_WINDOW_PIXELS) / 2U;
        center_max_x = center_min_x + APP_FRUIT_REGION_CENTER_WINDOW_PIXELS - 1U;
    }

    if (APP_FRUIT_REGION_CENTER_WINDOW_PIXELS < APP_IMAGE_HEIGHT)
    {
        center_min_y = (APP_IMAGE_HEIGHT - APP_FRUIT_REGION_CENTER_WINDOW_PIXELS) / 2U;
        center_max_y = center_min_y + APP_FRUIT_REGION_CENTER_WINDOW_PIXELS - 1U;
    }

    return !((p_component->max_x < center_min_x) ||
             (p_component->min_x > center_max_x) ||
             (p_component->max_y < center_min_y) ||
             (p_component->min_y > center_max_y));
}

static uint32_t fruit_region_component_center_distance(const fruit_region_component_t * p_component)
{
    uint32_t component_center_x;
    uint32_t component_center_y;
    uint32_t image_center_x = APP_IMAGE_WIDTH / 2U;
    uint32_t image_center_y = APP_IMAGE_HEIGHT / 2U;
    uint32_t distance_x;
    uint32_t distance_y;

    if (NULL == p_component)
    {
        return UINT32_MAX;
    }

    component_center_x = (p_component->min_x + p_component->max_x) / 2U;
    component_center_y = (p_component->min_y + p_component->max_y) / 2U;
    distance_x = (component_center_x > image_center_x) ?
                 (component_center_x - image_center_x) : (image_center_x - component_center_x);
    distance_y = (component_center_y > image_center_y) ?
                 (component_center_y - image_center_y) : (image_center_y - component_center_y);

    return distance_x + distance_y;
}

static void fruit_region_try_enqueue(const app_image_t * p_src, uint32_t x, uint32_t y,
                                     uint16_t label, uint32_t * p_tail)
{
    uint32_t pixel_index;
    uint32_t byte_index;
    uint8_t r;
    uint8_t g;
    uint8_t b;

    if ((NULL == p_src) || (NULL == p_tail) || (x >= APP_IMAGE_WIDTH) || (y >= APP_IMAGE_HEIGHT))
    {
        return;
    }

    pixel_index = (y * APP_IMAGE_WIDTH) + x;

    if (0U != g_region_labels[pixel_index])
    {
        return;
    }

    byte_index = pixel_index * APP_IMAGE_CHANNELS;
    r = p_src->pixels[byte_index];
    g = p_src->pixels[byte_index + 1U];
    b = p_src->pixels[byte_index + 2U];

    if (!fruit_pixel_is_region_seed(r, g, b))
    {
        return;
    }

    g_region_labels[pixel_index] = label;
    g_region_queue[*p_tail] = (uint16_t) pixel_index;
    (*p_tail)++;
}

static void fruit_region_copy_row_spans(const app_image_t * p_src, app_image_t * p_dst, uint16_t label)
{
    for (uint32_t y = 0U; y < APP_IMAGE_HEIGHT; y++)
    {
        g_region_row_min[y] = (uint8_t) APP_IMAGE_WIDTH;
        g_region_row_max[y] = 0U;
    }

    for (uint32_t y = 0U; y < APP_IMAGE_HEIGHT; y++)
    {
        for (uint32_t x = 0U; x < APP_IMAGE_WIDTH; x++)
        {
            uint32_t pixel_index = (y * APP_IMAGE_WIDTH) + x;

            if (g_region_labels[pixel_index] == label)
            {
                if (x < (uint32_t) g_region_row_min[y])
                {
                    g_region_row_min[y] = (uint8_t) x;
                }

                if (x > (uint32_t) g_region_row_max[y])
                {
                    g_region_row_max[y] = (uint8_t) x;
                }
            }
        }
    }

    for (uint32_t y = 0U; y < APP_IMAGE_HEIGHT; y++)
    {
        uint32_t start_x;
        uint32_t end_x;

        if ((uint32_t) g_region_row_min[y] >= APP_IMAGE_WIDTH)
        {
            continue;
        }

        start_x = ((uint32_t) g_region_row_min[y] > APP_FRUIT_REGION_MARGIN_PIXELS) ?
                  ((uint32_t) g_region_row_min[y] - APP_FRUIT_REGION_MARGIN_PIXELS) : 0U;
        end_x = (((uint32_t) g_region_row_max[y] + APP_FRUIT_REGION_MARGIN_PIXELS) < APP_IMAGE_WIDTH) ?
                ((uint32_t) g_region_row_max[y] + APP_FRUIT_REGION_MARGIN_PIXELS) : (APP_IMAGE_WIDTH - 1U);

        for (uint32_t x = start_x; x <= end_x; x++)
        {
            uint32_t index = ((y * APP_IMAGE_WIDTH) + x) * APP_IMAGE_CHANNELS;

            for (uint32_t channel = 0U; channel < APP_IMAGE_CHANNELS; channel++)
            {
                p_dst->pixels[index + channel] = p_src->pixels[index + channel];
            }
        }
    }
}

static bool fruit_pixel_is_region_seed(uint8_t r, uint8_t g, uint8_t b)
{
    uint32_t r_norm;
    uint32_t g_norm;
    uint32_t b_norm;
    uint32_t rg_sum;
    uint32_t brightness = (uint32_t) r + (uint32_t) g + (uint32_t) b;

    if (brightness < 90U)
    {
        return false;
    }

    if (!fruit_rule_normalize_rgb(r, g, b, &r_norm, &g_norm, &b_norm))
    {
        return false;
    }

    rg_sum = r_norm + g_norm;

    if (b_norm > (rg_sum / 2U))
    {
        return false;
    }

    return ((r_norm > (b_norm + 10U)) || (g_norm > (b_norm + 10U))) &&
           (rg_sum > ((2U * b_norm) + 20U));
}

#if APP_RULE_CLASSIFIER_ENABLED
static bool fruit_apply_rule_classifier(const app_image_t * p_image, fruit_result_t * p_result)
{
    fruit_rule_features_t features;
    uint32_t orange_permille;
    uint32_t greenish_permille;
    uint32_t strong_greenish_permille;
    uint32_t defect_permille;
    uint32_t speckle_spread_x = 0U;
    uint32_t speckle_spread_y = 0U;
    bool large_defect;
    bool center_defect;
    bool speckle_defect;
    bool spot_defect;

    if ((NULL == p_image) || (NULL == p_result))
    {
        return false;
    }

    fruit_rule_extract_features(p_image, &features);

    if (features.fruit_area < APP_RULE_MIN_FRUIT_PIXELS)
    {
        p_result->grade = FRUIT_GRADE_UNKNOWN;
        p_result->score = 0U;
        p_result->status = "RULE_UNKNOWN_NO_FRUIT";
        return true;
    }

    orange_permille = fruit_rule_permille(features.orange_count, features.fruit_area);
    greenish_permille = fruit_rule_permille(features.greenish_count, features.fruit_area);
    strong_greenish_permille = fruit_rule_permille(features.strong_greenish_count, features.fruit_area);
    defect_permille = fruit_rule_permille(features.defect_count, features.fruit_area);

    if ((features.speckle_min_x <= features.speckle_max_x) &&
        (features.speckle_min_y <= features.speckle_max_y))
    {
        speckle_spread_x = features.speckle_max_x - features.speckle_min_x + 1U;
        speckle_spread_y = features.speckle_max_y - features.speckle_min_y + 1U;
    }

    large_defect = (((defect_permille >= APP_RULE_C_DEFECT_RATIO_PERMILLE) &&
                     (features.defect_count >= APP_RULE_C_TOTAL_DEFECT_PIXELS) &&
                     (features.max_defect_component >= APP_RULE_C_DEFECT_COMPONENT_PIXELS)) ||
                    (features.max_defect_component >= APP_RULE_C_LARGE_DEFECT_COMPONENT_PIXELS));
    center_defect = ((features.center_defect_count >= APP_RULE_CENTER_DEFECT_TOTAL_PIXELS) &&
                     (features.max_center_defect_component >= APP_RULE_CENTER_DEFECT_COMPONENT_PIXELS) &&
                     (features.max_center_defect_component <= APP_RULE_CENTER_DEFECT_MAX_COMPONENT_PIXELS));
    speckle_defect = ((features.speckle_defect_count >= APP_RULE_C_SPECKLE_TOTAL_PIXELS) &&
                      (features.speckle_defect_components >= APP_RULE_C_SPECKLE_COMPONENT_COUNT) &&
                      (speckle_spread_x >= APP_RULE_C_SPECKLE_SPREAD_X_PIXELS) &&
                      (speckle_spread_y >= APP_RULE_C_SPECKLE_SPREAD_Y_PIXELS) &&
                      ((features.defect_count >= APP_RULE_C_SPECKLE_TOTAL_PIXELS) ||
                       (features.max_defect_component >= APP_RULE_IGNORE_SMALL_SPOT_PIXELS) ||
                       ((greenish_permille < APP_RULE_B_GREEN_RATIO_PERMILLE) &&
                        (strong_greenish_permille < APP_RULE_B_STRONG_GREEN_RATIO_PERMILLE))));

    if (large_defect || center_defect || speckle_defect)
    {
        uint32_t score_source = features.max_defect_component;

        if (features.max_center_defect_component > score_source)
        {
            score_source = features.max_center_defect_component;
        }

        if (features.speckle_defect_count > score_source)
        {
            score_source = features.speckle_defect_count;
        }

        p_result->grade = FRUIT_GRADE_C;
        p_result->score = fruit_rule_score_from_component(score_source);
        p_result->status = speckle_defect ? "RULE_C_SPECKLE" : "RULE_C_DEFECT";
        return true;
    }

    if ((orange_permille >= APP_RULE_A_STRONG_ORANGE_RATIO_PERMILLE) &&
        (greenish_permille < APP_RULE_A_GREEN_SHADOW_ALLOW_PERMILLE) &&
        (strong_greenish_permille < APP_RULE_A_GREEN_SHADOW_ALLOW_PERMILLE) &&
        (features.max_defect_component < APP_RULE_A_SHADOW_DEFECT_COMPONENT_MAX) &&
        (features.defect_count < APP_RULE_A_SHADOW_DEFECT_TOTAL_MAX) &&
        (features.speckle_defect_count < APP_RULE_C_SPECKLE_TOTAL_PIXELS))
    {
        p_result->grade = FRUIT_GRADE_A;
        p_result->score = fruit_rule_score_from_permille(orange_permille);
        p_result->status = "RULE_A_ORANGE";
        return true;
    }

    if ((greenish_permille >= APP_RULE_B_GREEN_RATIO_PERMILLE) ||
        (strong_greenish_permille >= APP_RULE_B_STRONG_GREEN_RATIO_PERMILLE))
    {
        p_result->grade = FRUIT_GRADE_B;
        p_result->score = fruit_rule_score_from_permille(greenish_permille);
        p_result->status = "RULE_B_GREEN";
        return true;
    }

    spot_defect = (((defect_permille >= APP_RULE_B_DEFECT_RATIO_PERMILLE) &&
                    (features.max_defect_component >= APP_RULE_B_DEFECT_COMPONENT_PIXELS)) ||
                   (features.max_defect_component >= APP_RULE_B_LARGE_SPOT_PIXELS) ||
                   ((features.defect_count >= APP_RULE_B_SPECKLE_TOTAL_PIXELS) &&
                    (features.speckle_defect_components >= APP_RULE_B_SPECKLE_COMPONENT_COUNT)));
    if (spot_defect)
    {
        p_result->grade = FRUIT_GRADE_B;
        p_result->score = fruit_rule_score_from_component(features.max_defect_component);
        if (p_result->score < 60U)
        {
            p_result->score = 60U;
        }
        p_result->status = "RULE_B_SPOT";
        return true;
    }

    if (orange_permille >= APP_RULE_A_ORANGE_RATIO_PERMILLE)
    {
        p_result->grade = FRUIT_GRADE_A;
        p_result->score = fruit_rule_score_from_permille(orange_permille);
        p_result->status = "RULE_A_ORANGE";
        return true;
    }

    p_result->grade = FRUIT_GRADE_A;
    p_result->score = 65U;
    p_result->status = "RULE_A_DEFAULT";
    return true;
}
#endif

static void fruit_apply_safe_a_filter(const app_image_t * p_image, fruit_result_t * p_result)
{
    fruit_rule_features_t features;
    uint32_t orange_permille;
    uint32_t greenish_permille;
    uint32_t strong_greenish_permille;
    uint32_t defect_permille;
    uint32_t fruit_color_count;
    uint32_t fruit_color_permille;
    bool large_defect;
    bool center_defect;
    bool green_risk;
    bool spot_risk;

    if ((NULL == p_image) || (NULL == p_result) || (FRUIT_GRADE_A != p_result->grade))
    {
        return;
    }

    fruit_rule_extract_features(p_image, &features);

    if (features.fruit_area < APP_RULE_MIN_FRUIT_PIXELS)
    {
        p_result->grade = FRUIT_GRADE_UNKNOWN;
        p_result->score = 0U;
        p_result->status = "SAFE_A_TO_UNKNOWN";
        return;
    }

    orange_permille = fruit_rule_permille(features.orange_count, features.fruit_area);
    greenish_permille = fruit_rule_permille(features.greenish_count, features.fruit_area);
    strong_greenish_permille = fruit_rule_permille(features.strong_greenish_count, features.fruit_area);
    defect_permille = fruit_rule_permille(features.defect_count, features.fruit_area);
    fruit_color_count = features.orange_count + features.greenish_count;
    if (fruit_color_count > features.fruit_area)
    {
        fruit_color_count = features.fruit_area;
    }
    fruit_color_permille = fruit_rule_permille(fruit_color_count, features.fruit_area);

    if (fruit_color_permille < APP_RULE_UNKNOWN_COLOR_RATIO_PERMILLE)
    {
        p_result->grade = FRUIT_GRADE_UNKNOWN;
        p_result->score = 0U;
        p_result->status = "SAFE_A_TO_UNKNOWN";
        return;
    }

    large_defect = (((defect_permille >= APP_RULE_C_DEFECT_RATIO_PERMILLE) &&
                     (features.defect_count >= APP_RULE_C_TOTAL_DEFECT_PIXELS) &&
                     (features.max_defect_component >= APP_RULE_C_DEFECT_COMPONENT_PIXELS)) ||
                    (features.max_defect_component >= APP_RULE_C_LARGE_DEFECT_COMPONENT_PIXELS));
    center_defect = ((features.center_defect_count >= APP_RULE_CENTER_DEFECT_TOTAL_PIXELS) &&
                     (features.max_center_defect_component >= APP_RULE_CENTER_DEFECT_COMPONENT_PIXELS) &&
                     (features.max_center_defect_component <= APP_RULE_CENTER_DEFECT_MAX_COMPONENT_PIXELS));

    if (large_defect || center_defect)
    {
        uint32_t score_source = features.max_defect_component;

        if (features.max_center_defect_component > score_source)
        {
            score_source = features.max_center_defect_component;
        }

        p_result->grade = FRUIT_GRADE_C;
        p_result->score = fruit_rule_score_from_component(score_source);
        if (p_result->score < 70U)
        {
            p_result->score = 70U;
        }
        p_result->status = "SAFE_A_TO_C";
        return;
    }

    green_risk = ((strong_greenish_permille >= APP_RULE_B_STRONG_GREEN_RATIO_PERMILLE) ||
                  ((greenish_permille >= APP_RULE_B_GREEN_RATIO_PERMILLE) &&
                   (orange_permille < APP_RULE_A_ORANGE_RATIO_PERMILLE)));
    spot_risk = ((features.max_defect_component >= APP_RULE_B_LARGE_SPOT_PIXELS) ||
                 ((defect_permille >= APP_RULE_B_DEFECT_RATIO_PERMILLE) &&
                  (features.max_defect_component >= APP_RULE_B_DEFECT_COMPONENT_PIXELS)));

    if (green_risk || spot_risk)
    {
        p_result->grade = FRUIT_GRADE_B;
        if (p_result->score < 60U)
        {
            p_result->score = 60U;
        }
        p_result->status = "SAFE_A_TO_B";
    }
}

static void fruit_rule_extract_features(const app_image_t * p_image, fruit_rule_features_t * p_features)
{
    uint16_t next_label = 1U;

    if ((NULL == p_image) || (NULL == p_features))
    {
        return;
    }

    memset(p_features, 0, sizeof(*p_features));
    memset(g_region_labels, 0, sizeof(g_region_labels));
    p_features->speckle_min_x = APP_IMAGE_WIDTH;
    p_features->speckle_min_y = APP_IMAGE_HEIGHT;

    for (uint32_t y = 0U; y < APP_IMAGE_HEIGHT; y++)
    {
        g_region_row_min[y] = (uint8_t) APP_IMAGE_WIDTH;
        g_region_row_max[y] = 0U;
    }

    for (uint32_t y = 0U; y < APP_IMAGE_HEIGHT; y++)
    {
        for (uint32_t x = 0U; x < APP_IMAGE_WIDTH; x++)
        {
            uint32_t pixel_index = (y * APP_IMAGE_WIDTH) + x;
            uint32_t byte_index = pixel_index * APP_IMAGE_CHANNELS;
            uint8_t r = p_image->pixels[byte_index];
            uint8_t g = p_image->pixels[byte_index + 1U];
            uint8_t b = p_image->pixels[byte_index + 2U];

            if (!fruit_rule_pixel_is_fruit(r, g, b))
            {
                continue;
            }

            p_features->fruit_area++;

            if (x < (uint32_t) g_region_row_min[y])
            {
                g_region_row_min[y] = (uint8_t) x;
            }

            if (x > (uint32_t) g_region_row_max[y])
            {
                g_region_row_max[y] = (uint8_t) x;
            }

            bool is_orange = fruit_rule_pixel_is_orange(r, g, b);

            if (is_orange)
            {
                p_features->orange_count++;
            }

            if (!is_orange && fruit_rule_pixel_is_greenish(r, g, b))
            {
                p_features->greenish_count++;
            }

            if (!is_orange && fruit_rule_pixel_is_strong_greenish(r, g, b))
            {
                p_features->strong_greenish_count++;
            }
        }
    }

    for (uint32_t y = 0U; y < APP_IMAGE_HEIGHT; y++)
    {
        for (uint32_t x = 0U; x < APP_IMAGE_WIDTH; x++)
        {
            uint32_t pixel_index = (y * APP_IMAGE_WIDTH) + x;
            uint32_t head = 0U;
            uint32_t tail = 0U;
            uint32_t component_pixels = 0U;
            uint32_t component_min_x = APP_IMAGE_WIDTH;
            uint32_t component_min_y = APP_IMAGE_HEIGHT;
            uint32_t component_max_x = 0U;
            uint32_t component_max_y = 0U;

            if (0U != g_region_labels[pixel_index])
            {
                continue;
            }

            fruit_rule_try_enqueue_defect(p_image, x, y, next_label, &tail);

            if (0U == tail)
            {
                continue;
            }

            while (head < tail)
            {
                uint32_t current = (uint32_t) g_region_queue[head++];
                uint32_t current_x = current % APP_IMAGE_WIDTH;
                uint32_t current_y = current / APP_IMAGE_WIDTH;

                component_pixels++;

                if (current_x < component_min_x)
                {
                    component_min_x = current_x;
                }

                if (current_x > component_max_x)
                {
                    component_max_x = current_x;
                }

                if (current_y < component_min_y)
                {
                    component_min_y = current_y;
                }

                if (current_y > component_max_y)
                {
                    component_max_y = current_y;
                }

                if (current_x > 0U)
                {
                    fruit_rule_try_enqueue_defect(p_image, current_x - 1U, current_y, next_label, &tail);
                }

                if ((current_x + 1U) < APP_IMAGE_WIDTH)
                {
                    fruit_rule_try_enqueue_defect(p_image, current_x + 1U, current_y, next_label, &tail);
                }

                if (current_y > 0U)
                {
                    fruit_rule_try_enqueue_defect(p_image, current_x, current_y - 1U, next_label, &tail);
                }

                if ((current_y + 1U) < APP_IMAGE_HEIGHT)
                {
                    fruit_rule_try_enqueue_defect(p_image, current_x, current_y + 1U, next_label, &tail);
                }
            }

            if (component_pixels >= APP_RULE_IGNORE_SMALL_SPOT_PIXELS)
            {
                p_features->defect_count += component_pixels;
            }

            if (component_pixels > p_features->max_defect_component)
            {
                p_features->max_defect_component = component_pixels;
            }

            if (next_label < UINT16_MAX)
            {
                next_label++;
            }
        }
    }

    memset(g_region_labels, 0, sizeof(g_region_labels));
    next_label = 1U;

    for (uint32_t y = 0U; y < APP_IMAGE_HEIGHT; y++)
    {
        for (uint32_t x = 0U; x < APP_IMAGE_WIDTH; x++)
        {
            uint32_t pixel_index = (y * APP_IMAGE_WIDTH) + x;
            uint32_t head = 0U;
            uint32_t tail = 0U;
            uint32_t component_pixels = 0U;
            uint32_t component_min_x = APP_IMAGE_WIDTH;
            uint32_t component_min_y = APP_IMAGE_HEIGHT;
            uint32_t component_max_x = 0U;
            uint32_t component_max_y = 0U;

            if (0U != g_region_labels[pixel_index])
            {
                continue;
            }

            fruit_rule_try_enqueue_speckle(p_image, x, y, next_label, &tail);

            if (0U == tail)
            {
                continue;
            }

            while (head < tail)
            {
                uint32_t current = (uint32_t) g_region_queue[head++];
                uint32_t current_x = current % APP_IMAGE_WIDTH;
                uint32_t current_y = current / APP_IMAGE_WIDTH;

                component_pixels++;

                if (current_x < component_min_x)
                {
                    component_min_x = current_x;
                }

                if (current_x > component_max_x)
                {
                    component_max_x = current_x;
                }

                if (current_y < component_min_y)
                {
                    component_min_y = current_y;
                }

                if (current_y > component_max_y)
                {
                    component_max_y = current_y;
                }

                if (current_x > 0U)
                {
                    fruit_rule_try_enqueue_speckle(p_image, current_x - 1U, current_y, next_label, &tail);
                }

                if ((current_x + 1U) < APP_IMAGE_WIDTH)
                {
                    fruit_rule_try_enqueue_speckle(p_image, current_x + 1U, current_y, next_label, &tail);
                }

                if (current_y > 0U)
                {
                    fruit_rule_try_enqueue_speckle(p_image, current_x, current_y - 1U, next_label, &tail);
                }

                if ((current_y + 1U) < APP_IMAGE_HEIGHT)
                {
                    fruit_rule_try_enqueue_speckle(p_image, current_x, current_y + 1U, next_label, &tail);
                }
            }

            if (component_pixels >= APP_RULE_C_SPECKLE_COMPONENT_MIN_PIXELS)
            {
                p_features->speckle_defect_count += component_pixels;
                p_features->speckle_defect_components++;

                if (component_min_x < p_features->speckle_min_x)
                {
                    p_features->speckle_min_x = component_min_x;
                }

                if (component_max_x > p_features->speckle_max_x)
                {
                    p_features->speckle_max_x = component_max_x;
                }

                if (component_min_y < p_features->speckle_min_y)
                {
                    p_features->speckle_min_y = component_min_y;
                }

                if (component_max_y > p_features->speckle_max_y)
                {
                    p_features->speckle_max_y = component_max_y;
                }
            }

            if (next_label < UINT16_MAX)
            {
                next_label++;
            }
        }
    }

    memset(g_region_labels, 0, sizeof(g_region_labels));
    next_label = 1U;

    for (uint32_t y = 0U; y < APP_IMAGE_HEIGHT; y++)
    {
        for (uint32_t x = 0U; x < APP_IMAGE_WIDTH; x++)
        {
            uint32_t pixel_index = (y * APP_IMAGE_WIDTH) + x;
            uint32_t head = 0U;
            uint32_t tail = 0U;
            uint32_t component_pixels = 0U;

            if (0U != g_region_labels[pixel_index])
            {
                continue;
            }

            fruit_rule_try_enqueue_center_defect(p_image, x, y, next_label, &tail);

            if (0U == tail)
            {
                continue;
            }

            while (head < tail)
            {
                uint32_t current = (uint32_t) g_region_queue[head++];
                uint32_t current_x = current % APP_IMAGE_WIDTH;
                uint32_t current_y = current / APP_IMAGE_WIDTH;

                component_pixels++;

                if (current_x > 0U)
                {
                    fruit_rule_try_enqueue_center_defect(p_image, current_x - 1U, current_y, next_label, &tail);
                }

                if ((current_x + 1U) < APP_IMAGE_WIDTH)
                {
                    fruit_rule_try_enqueue_center_defect(p_image, current_x + 1U, current_y, next_label, &tail);
                }

                if (current_y > 0U)
                {
                    fruit_rule_try_enqueue_center_defect(p_image, current_x, current_y - 1U, next_label, &tail);
                }

                if ((current_y + 1U) < APP_IMAGE_HEIGHT)
                {
                    fruit_rule_try_enqueue_center_defect(p_image, current_x, current_y + 1U, next_label, &tail);
                }
            }

            p_features->center_defect_count += component_pixels;

            if (component_pixels > p_features->max_center_defect_component)
            {
                p_features->max_center_defect_component = component_pixels;
            }

            if (next_label < UINT16_MAX)
            {
                next_label++;
            }
        }
    }
}

static bool fruit_rule_normalize_rgb(uint8_t r, uint8_t g, uint8_t b,
                                     uint32_t * p_r_norm,
                                     uint32_t * p_g_norm,
                                     uint32_t * p_b_norm)
{
    uint32_t max_channel = (uint32_t) r;

    if ((NULL == p_r_norm) || (NULL == p_g_norm) || (NULL == p_b_norm))
    {
        return false;
    }

    if ((uint32_t) g > max_channel)
    {
        max_channel = (uint32_t) g;
    }

    if ((uint32_t) b > max_channel)
    {
        max_channel = (uint32_t) b;
    }

    if (0U == max_channel)
    {
        return false;
    }

    *p_r_norm = (((uint32_t) r * 255U) + (max_channel / 2U)) / max_channel;
    *p_g_norm = (((uint32_t) g * 255U) + (max_channel / 2U)) / max_channel;
    *p_b_norm = (((uint32_t) b * 255U) + (max_channel / 2U)) / max_channel;

    return true;
}

static bool fruit_rule_pixel_is_fruit(uint8_t r, uint8_t g, uint8_t b)
{
    uint32_t r_norm;
    uint32_t g_norm;
    uint32_t b_norm;
    uint32_t brightness = (uint32_t) r + (uint32_t) g + (uint32_t) b;

    if (brightness <= APP_RULE_FRUIT_BRIGHTNESS_MIN)
    {
        return false;
    }

    if (!fruit_rule_normalize_rgb(r, g, b, &r_norm, &g_norm, &b_norm))
    {
        return false;
    }

    return (b_norm <= (((r_norm + g_norm) / 2U) + APP_RULE_FRUIT_BLUE_ALLOWANCE)) &&
           ((r_norm > (b_norm + APP_RULE_FRUIT_COLOR_DELTA)) ||
            (g_norm > (b_norm + APP_RULE_FRUIT_COLOR_DELTA)));
}

static bool fruit_rule_pixel_is_orange(uint8_t r, uint8_t g, uint8_t b)
{
    uint32_t r_norm;
    uint32_t g_norm;
    uint32_t b_norm;

    if (!fruit_rule_normalize_rgb(r, g, b, &r_norm, &g_norm, &b_norm))
    {
        return false;
    }

    return (r_norm >= APP_RULE_ORANGE_R_MIN) &&
           (g_norm >= APP_RULE_ORANGE_G_MIN) &&
           (r_norm >= (g_norm + APP_RULE_ORANGE_R_OVER_G_MIN)) &&
           (g_norm >= (b_norm + APP_RULE_ORANGE_G_OVER_B_MIN));
}

static bool fruit_rule_pixel_is_greenish(uint8_t r, uint8_t g, uint8_t b)
{
    uint32_t r_norm;
    uint32_t g_norm;
    uint32_t b_norm;

    if (!fruit_rule_normalize_rgb(r, g, b, &r_norm, &g_norm, &b_norm))
    {
        return false;
    }

    return (g_norm >= APP_RULE_GREEN_G_MIN) &&
           (g_norm >= (b_norm + APP_RULE_GREEN_G_OVER_B_MIN)) &&
           (r_norm <= (g_norm + APP_RULE_GREEN_R_OVER_G_ALLOWANCE));
}

static bool fruit_rule_pixel_is_strong_greenish(uint8_t r, uint8_t g, uint8_t b)
{
    uint32_t r_norm;
    uint32_t g_norm;
    uint32_t b_norm;

    if (!fruit_rule_normalize_rgb(r, g, b, &r_norm, &g_norm, &b_norm))
    {
        return false;
    }

    return (g_norm >= APP_RULE_STRONG_GREEN_G_MIN) &&
           (g_norm >= (b_norm + APP_RULE_STRONG_GREEN_G_OVER_B_MIN)) &&
           (r_norm <= (g_norm + APP_RULE_STRONG_GREEN_R_OVER_G_ALLOWANCE));
}

static bool fruit_rule_pixel_is_defect(const app_image_t * p_image, uint32_t x, uint32_t y)
{
    uint32_t pixel_index;
    uint32_t byte_index;
    uint32_t brightness;
    uint8_t r;
    uint8_t g;
    uint8_t b;

    if ((NULL == p_image) || (x >= APP_IMAGE_WIDTH) || (y >= APP_IMAGE_HEIGHT))
    {
        return false;
    }

    if (fruit_rule_pixel_is_center_ignored(x, y))
    {
        return false;
    }

    pixel_index = (y * APP_IMAGE_WIDTH) + x;
    byte_index = pixel_index * APP_IMAGE_CHANNELS;
    r = p_image->pixels[byte_index];
    g = p_image->pixels[byte_index + 1U];
    b = p_image->pixels[byte_index + 2U];
    brightness = (uint32_t) r + (uint32_t) g + (uint32_t) b;

    if (!fruit_rule_pixel_is_fruit(r, g, b))
    {
        return false;
    }

    if ((brightness >= APP_RULE_BLACK_BRIGHTNESS_MAX) && fruit_rule_pixel_is_orange(r, g, b))
    {
        return false;
    }

    if ((brightness < APP_RULE_BLACK_BRIGHTNESS_MAX) &&
        ((uint32_t) r < APP_RULE_BLACK_RGB_MAX) &&
        ((uint32_t) g < APP_RULE_BLACK_RGB_MAX) &&
        ((uint32_t) b < APP_RULE_BLACK_RGB_MAX))
    {
        return true;
    }

    return (brightness < APP_RULE_BROWN_BRIGHTNESS_MAX) &&
           ((uint32_t) r < APP_RULE_BROWN_R_MAX) &&
           ((uint32_t) g < APP_RULE_BROWN_G_MAX) &&
           ((uint32_t) b < APP_RULE_BROWN_B_MAX) &&
           (((uint32_t) r + APP_RULE_BROWN_R_UNDER_G_ALLOWANCE) >= (uint32_t) g);
}

static bool fruit_rule_pixel_is_center_ignored(uint32_t x, uint32_t y)
{
    uint32_t center_x = APP_IMAGE_WIDTH / 2U;
    uint32_t center_y = APP_IMAGE_HEIGHT / 2U;
    uint32_t dx = (x > center_x) ? (x - center_x) : (center_x - x);
    uint32_t dy = (y > center_y) ? (y - center_y) : (center_y - y);
    uint32_t radius = APP_RULE_CENTER_IGNORE_RADIUS_PIXELS;

    return ((dx * dx) + (dy * dy)) <= (radius * radius);
}

static bool fruit_rule_pixel_is_center_defect(const app_image_t * p_image, uint32_t x, uint32_t y)
{
    uint32_t pixel_index;
    uint32_t byte_index;
    uint32_t brightness;
    uint32_t r32;
    uint32_t g32;
    uint32_t b32;
    uint8_t r;
    uint8_t g;
    uint8_t b;

    if ((NULL == p_image) || (x >= APP_IMAGE_WIDTH) || (y >= APP_IMAGE_HEIGHT))
    {
        return false;
    }

    if (!fruit_rule_pixel_is_center_ignored(x, y))
    {
        return false;
    }

    pixel_index = (y * APP_IMAGE_WIDTH) + x;
    byte_index = pixel_index * APP_IMAGE_CHANNELS;
    r = p_image->pixels[byte_index];
    g = p_image->pixels[byte_index + 1U];
    b = p_image->pixels[byte_index + 2U];
    r32 = (uint32_t) r;
    g32 = (uint32_t) g;
    b32 = (uint32_t) b;
    brightness = r32 + g32 + b32;

    if (!fruit_rule_pixel_is_fruit(r, g, b))
    {
        return false;
    }

    return (brightness < APP_RULE_CENTER_DEFECT_BRIGHTNESS_MAX) &&
           (r32 < APP_RULE_CENTER_DEFECT_R_MAX) &&
           (g32 < APP_RULE_CENTER_DEFECT_G_MAX) &&
           (b32 < APP_RULE_CENTER_DEFECT_B_MAX) &&
           (r32 >= (g32 + APP_RULE_CENTER_DEFECT_R_OVER_G_MIN)) &&
           (r32 >= (b32 + APP_RULE_CENTER_DEFECT_R_OVER_B_MIN)) &&
           (g32 >= (b32 + APP_RULE_CENTER_DEFECT_G_OVER_B_MIN));
}

static bool fruit_rule_pixel_is_speckle(const app_image_t * p_image, uint32_t x, uint32_t y)
{
    uint32_t pixel_index;
    uint32_t byte_index;
    uint32_t gray;
    uint32_t local_sum = 0U;
    uint32_t local_count = 0U;
    uint32_t radius = APP_RULE_SPECKLE_LOCAL_RADIUS_PIXELS;
    uint32_t edge_margin = APP_RULE_SPECKLE_EDGE_MARGIN_PIXELS;
    uint32_t start_x;
    uint32_t start_y;
    uint32_t end_x;
    uint32_t end_y;
    uint8_t r;
    uint8_t g;
    uint8_t b;

    if ((NULL == p_image) || (x >= APP_IMAGE_WIDTH) || (y >= APP_IMAGE_HEIGHT))
    {
        return false;
    }

    if (fruit_rule_pixel_is_center_ignored(x, y))
    {
        return false;
    }

    if (((uint32_t) g_region_row_min[y] >= APP_IMAGE_WIDTH) ||
        (x <= ((uint32_t) g_region_row_min[y] + edge_margin)) ||
        ((x + edge_margin) >= (uint32_t) g_region_row_max[y]))
    {
        return false;
    }

    pixel_index = (y * APP_IMAGE_WIDTH) + x;
    byte_index = pixel_index * APP_IMAGE_CHANNELS;
    r = p_image->pixels[byte_index];
    g = p_image->pixels[byte_index + 1U];
    b = p_image->pixels[byte_index + 2U];

    if (!fruit_rule_pixel_is_fruit(r, g, b))
    {
        return false;
    }

    gray = (uint32_t) fruit_get_pixel_gray(p_image, pixel_index);

    if (gray > APP_RULE_SPECKLE_GRAY_MAX)
    {
        return false;
    }

    start_x = (x > radius) ? (x - radius) : 0U;
    start_y = (y > radius) ? (y - radius) : 0U;
    end_x = ((x + radius) < APP_IMAGE_WIDTH) ? (x + radius) : (APP_IMAGE_WIDTH - 1U);
    end_y = ((y + radius) < APP_IMAGE_HEIGHT) ? (y + radius) : (APP_IMAGE_HEIGHT - 1U);

    for (uint32_t local_y = start_y; local_y <= end_y; local_y++)
    {
        for (uint32_t local_x = start_x; local_x <= end_x; local_x++)
        {
            uint32_t local_pixel_index = (local_y * APP_IMAGE_WIDTH) + local_x;
            uint32_t local_byte_index = local_pixel_index * APP_IMAGE_CHANNELS;
            uint8_t local_r = p_image->pixels[local_byte_index];
            uint8_t local_g = p_image->pixels[local_byte_index + 1U];
            uint8_t local_b = p_image->pixels[local_byte_index + 2U];

            if (fruit_rule_pixel_is_fruit(local_r, local_g, local_b))
            {
                local_sum += (uint32_t) fruit_get_pixel_gray(p_image, local_pixel_index);
                local_count++;
            }
        }
    }

    if (local_count < APP_RULE_SPECKLE_LOCAL_MIN_NEIGHBORS)
    {
        return false;
    }

    return (local_sum / local_count) >= (gray + APP_RULE_SPECKLE_LOCAL_DELTA_GRAY);
}

static void fruit_rule_try_enqueue_defect(const app_image_t * p_image, uint32_t x, uint32_t y,
                                          uint16_t label, uint32_t * p_tail)
{
    uint32_t pixel_index;

    if ((NULL == p_image) || (NULL == p_tail) || (x >= APP_IMAGE_WIDTH) || (y >= APP_IMAGE_HEIGHT))
    {
        return;
    }

    pixel_index = (y * APP_IMAGE_WIDTH) + x;

    if (0U != g_region_labels[pixel_index])
    {
        return;
    }

    if (!fruit_rule_pixel_is_defect(p_image, x, y))
    {
        return;
    }

    g_region_labels[pixel_index] = label;
    g_region_queue[*p_tail] = (uint16_t) pixel_index;
    (*p_tail)++;
}

static void fruit_rule_try_enqueue_speckle(const app_image_t * p_image, uint32_t x, uint32_t y,
                                           uint16_t label, uint32_t * p_tail)
{
    uint32_t pixel_index;

    if ((NULL == p_image) || (NULL == p_tail) || (x >= APP_IMAGE_WIDTH) || (y >= APP_IMAGE_HEIGHT))
    {
        return;
    }

    pixel_index = (y * APP_IMAGE_WIDTH) + x;

    if (0U != g_region_labels[pixel_index])
    {
        return;
    }

    if (!fruit_rule_pixel_is_speckle(p_image, x, y))
    {
        return;
    }

    g_region_labels[pixel_index] = label;
    g_region_queue[*p_tail] = (uint16_t) pixel_index;
    (*p_tail)++;
}

static void fruit_rule_try_enqueue_center_defect(const app_image_t * p_image, uint32_t x, uint32_t y,
                                                 uint16_t label, uint32_t * p_tail)
{
    uint32_t pixel_index;

    if ((NULL == p_image) || (NULL == p_tail) || (x >= APP_IMAGE_WIDTH) || (y >= APP_IMAGE_HEIGHT))
    {
        return;
    }

    pixel_index = (y * APP_IMAGE_WIDTH) + x;

    if (0U != g_region_labels[pixel_index])
    {
        return;
    }

    if (!fruit_rule_pixel_is_center_defect(p_image, x, y))
    {
        return;
    }

    g_region_labels[pixel_index] = label;
    g_region_queue[*p_tail] = (uint16_t) pixel_index;
    (*p_tail)++;
}

static uint32_t fruit_rule_permille(uint32_t count, uint32_t total)
{
    if (0U == total)
    {
        return 0U;
    }

    return (count * 1000U) / total;
}

#if APP_RULE_CLASSIFIER_ENABLED
static uint8_t fruit_rule_score_from_permille(uint32_t permille)
{
    uint32_t score = 50U + (permille / 5U);

    return (uint8_t) ((score > 95U) ? 95U : score);
}
#endif

static uint8_t fruit_rule_score_from_component(uint32_t component_pixels)
{
    uint32_t score = 55U + (component_pixels / 12U);

    return (uint8_t) ((score > 95U) ? 95U : score);
}

static fruit_grade_t fruit_grade_from_eai_index(uint32_t index)
{
    switch (index)
    {
        case 0U:
            return FRUIT_GRADE_A;

        case 1U:
            return FRUIT_GRADE_B;

        case 2U:
            return FRUIT_GRADE_C;

        default:
            return FRUIT_GRADE_UNKNOWN;
    }
}

static uint8_t fruit_score_from_eai_output(TsOUT output_value)
{
    return (uint8_t) (((uint32_t) output_value * 100U) / 255U);
}

static void fruit_apply_eai_output_filter(fruit_result_t * p_result, uint8_t second_score)
{
#if APP_INFERENCE_OUTPUT_FILTER_ENABLED
    uint8_t margin = 0U;
#else
    (void) second_score;
#endif

    if (NULL == p_result)
    {
        return;
    }

#if APP_INFERENCE_OUTPUT_FILTER_ENABLED
    if (FRUIT_GRADE_UNKNOWN == p_result->grade)
    {
        p_result->status = "EAI_UNKNOWN";
        return;
    }

    if (p_result->score >= second_score)
    {
        margin = (uint8_t) (p_result->score - second_score);
    }

    if ((APP_INFERENCE_MIN_SCORE > 0U) && (p_result->score < APP_INFERENCE_MIN_SCORE))
    {
        p_result->grade = FRUIT_GRADE_UNKNOWN;
        p_result->status = "EAI_LOW_SCORE";
    }
    else if ((APP_INFERENCE_MIN_MARGIN > 0U) && (margin < APP_INFERENCE_MIN_MARGIN))
    {
        p_result->grade = FRUIT_GRADE_UNKNOWN;
        p_result->status = "EAI_LOW_MARGIN";
    }
#endif
}

static void fruit_apply_unknown_output_policy(const app_image_t * p_image, fruit_result_t * p_result)
{
#if APP_INFERENCE_UNKNOWN_OUTPUT_ENABLED
    (void) p_image;
    (void) p_result;
#else
    if ((NULL != p_result) && (FRUIT_GRADE_UNKNOWN == p_result->grade))
    {
        fruit_rule_features_t features;
        uint32_t orange_permille;
        uint32_t greenish_permille;
        uint32_t strong_greenish_permille;
        uint32_t defect_permille;
        uint32_t fruit_color_count;
        uint32_t fruit_color_permille;
        uint32_t speckle_spread_x = 0U;
        uint32_t speckle_spread_y = 0U;
        bool large_defect;
        bool center_defect;
        bool speckle_defect;
        bool green_risk;
        bool spot_risk;

        if (NULL != p_image)
        {
            fruit_rule_extract_features(p_image, &features);
            if (features.fruit_area < APP_RULE_MIN_FRUIT_PIXELS)
            {
                p_result->grade = FRUIT_GRADE_C;
                if (p_result->score < 60U)
                {
                    p_result->score = 60U;
                }
                p_result->status = "UNKNOWN_NO_FRUIT_TO_C";
                return;
            }

            orange_permille = fruit_rule_permille(features.orange_count, features.fruit_area);
            greenish_permille = fruit_rule_permille(features.greenish_count, features.fruit_area);
            strong_greenish_permille = fruit_rule_permille(features.strong_greenish_count, features.fruit_area);
            defect_permille = fruit_rule_permille(features.defect_count, features.fruit_area);
            fruit_color_count = features.orange_count + features.greenish_count;
            if (fruit_color_count > features.fruit_area)
            {
                fruit_color_count = features.fruit_area;
            }

            fruit_color_permille = fruit_rule_permille(fruit_color_count, features.fruit_area);

            if ((features.speckle_min_x <= features.speckle_max_x) &&
                (features.speckle_min_y <= features.speckle_max_y))
            {
                speckle_spread_x = features.speckle_max_x - features.speckle_min_x + 1U;
                speckle_spread_y = features.speckle_max_y - features.speckle_min_y + 1U;
            }

            large_defect = (((defect_permille >= APP_RULE_C_DEFECT_RATIO_PERMILLE) &&
                             (features.defect_count >= APP_RULE_C_TOTAL_DEFECT_PIXELS) &&
                             (features.max_defect_component >= APP_RULE_C_DEFECT_COMPONENT_PIXELS)) ||
                            (features.max_defect_component >= APP_RULE_C_LARGE_DEFECT_COMPONENT_PIXELS));
            center_defect = ((features.center_defect_count >= APP_RULE_CENTER_DEFECT_TOTAL_PIXELS) &&
                             (features.max_center_defect_component >= APP_RULE_CENTER_DEFECT_COMPONENT_PIXELS) &&
                             (features.max_center_defect_component <= APP_RULE_CENTER_DEFECT_MAX_COMPONENT_PIXELS));
            speckle_defect = ((features.speckle_defect_count >= APP_RULE_C_SPECKLE_TOTAL_PIXELS) &&
                              (features.speckle_defect_components >= APP_RULE_C_SPECKLE_COMPONENT_COUNT) &&
                              (speckle_spread_x >= APP_RULE_C_SPECKLE_SPREAD_X_PIXELS) &&
                              (speckle_spread_y >= APP_RULE_C_SPECKLE_SPREAD_Y_PIXELS) &&
                              ((features.defect_count >= APP_RULE_C_SPECKLE_TOTAL_PIXELS) ||
                               (features.max_defect_component >= APP_RULE_IGNORE_SMALL_SPOT_PIXELS) ||
                               ((greenish_permille < APP_RULE_B_GREEN_RATIO_PERMILLE) &&
                                (strong_greenish_permille < APP_RULE_B_STRONG_GREEN_RATIO_PERMILLE))));

            if (large_defect || center_defect || speckle_defect)
            {
                p_result->grade = FRUIT_GRADE_C;
                if (p_result->score < 70U)
                {
                    p_result->score = 70U;
                }
                p_result->status = speckle_defect ? "UNKNOWN_RULE_C_SPECKLE" : "UNKNOWN_RULE_C_DEFECT";
                return;
            }

            green_risk = ((strong_greenish_permille >= APP_RULE_B_STRONG_GREEN_RATIO_PERMILLE) ||
                          (greenish_permille >= APP_RULE_B_GREEN_RATIO_PERMILLE));

            if (green_risk)
            {
                p_result->grade = FRUIT_GRADE_B;
                if (p_result->score < 65U)
                {
                    p_result->score = 65U;
                }
                p_result->status = "UNKNOWN_RULE_B_GREEN";
                return;
            }

            spot_risk = (((defect_permille >= APP_RULE_B_DEFECT_RATIO_PERMILLE) &&
                          (features.max_defect_component >= APP_RULE_B_DEFECT_COMPONENT_PIXELS)) ||
                         (features.max_defect_component >= APP_RULE_B_LARGE_SPOT_PIXELS) ||
                         ((features.defect_count >= APP_RULE_B_SPECKLE_TOTAL_PIXELS) &&
                          (features.speckle_defect_components >= APP_RULE_B_SPECKLE_COMPONENT_COUNT)));

            if (spot_risk)
            {
                p_result->grade = FRUIT_GRADE_B;
                if (p_result->score < 65U)
                {
                    p_result->score = 65U;
                }
                p_result->status = "UNKNOWN_RULE_B_SPOT";
                return;
            }

            if ((orange_permille >= 500U) && (fruit_color_permille >= 300U))
            {
                p_result->grade = FRUIT_GRADE_A;
                if (p_result->score < 65U)
                {
                    p_result->score = 65U;
                }
                p_result->status = "UNKNOWN_RULE_A_ORANGE";
                return;
            }

            p_result->grade = FRUIT_GRADE_A;
            if (p_result->score < 60U)
            {
                p_result->score = 60U;
            }
            p_result->status = "UNKNOWN_RULE_A_DEFAULT";
            return;
        }

        p_result->grade = FRUIT_GRADE_C;
        if (p_result->score < 60U)
        {
            p_result->score = 60U;
        }
        p_result->status = "UNKNOWN_TO_C";
    }
#endif
}
#else
static app_status_t fruit_run_threshold_inference(const app_image_t * p_image, fruit_result_t * p_result)
{
    fruit_features_t features;
    app_status_t status = APP_STATUS_INVALID_ARGUMENT;

    if ((NULL == p_image) || (NULL == p_result))
    {
        return status;
    }

    status = fruit_extract_features(p_image, &features);

    if (APP_STATUS_OK != status)
    {
        return status;
    }

    p_result->status = "OK";

    if (features.area < APP_MIN_FRUIT_AREA)
    {
        p_result->grade = FRUIT_GRADE_UNKNOWN;
        p_result->score = 0U;
    }
    else if ((features.mean_gray > 120U) && (features.contrast > 60U))
    {
        p_result->grade = FRUIT_GRADE_A;
        p_result->score = 85U;
    }
    else if (features.mean_gray > 80U)
    {
        p_result->grade = FRUIT_GRADE_B;
        p_result->score = 70U;
    }
    else
    {
        p_result->grade = FRUIT_GRADE_C;
        p_result->score = 55U;
    }

    return APP_STATUS_OK;
}
#endif
