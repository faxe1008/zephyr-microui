/*
 * Copyright (c) 2025 Fabian Blatz <fabianblatz@gmail.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef ZEPHYR_MODULES_MICROUI_IMAGE_H_
#define ZEPHYR_MODULES_MICROUI_IMAGE_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <zephyr/drivers/display.h>
#include <microui/microui.h>

enum mu_ImageDataCompression {
    MU_IMAGE_COMPRESSION_NONE = 0,
};

struct mu_ImageDescriptor {
	uint32_t width;
    uint32_t height;
    uint32_t stride;
    uint32_t data_size;
    const uint8_t *data;
    enum display_pixel_format pixel_format;
    enum mu_ImageDataCompression compression;
};

static inline void mu_get_img_dimensions(mu_Image image, int* width, int* height)
{
    const struct mu_ImageDescriptor *img_desc = (const struct mu_ImageDescriptor *)image;
    *width = img_desc->width;
    *height = img_desc->height;
}

#define MU_IMAGE_DECLARE(image_name) extern const struct mu_ImageDescriptor image_name;

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_MODULES_MICROUI_IMAGE_H_ */
