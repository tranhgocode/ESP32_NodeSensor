#ifndef SCALERS_H
#define SCALERS_H

/* Sinh từ scalers.json bằng scripts/export.py. */
/* Thứ tự đầu vào: PM2.5, nhiệt độ, độ ẩm. */
static const float pm25_input_mean[3] = {3.582711501e+01f, 2.424738364e+01f, 7.796772689e+01f};
static const float pm25_input_scale[3] = {2.226482441e+01f, 5.496672464e+00f, 1.524892431e+01f};
static const float pm25_output_mean = 3.582711501e+01f;
static const float pm25_output_scale = 2.226482441e+01f;

#endif
