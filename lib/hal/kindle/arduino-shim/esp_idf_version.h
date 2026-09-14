#pragma once
// Version gates in the tree compare against this. Reporting a recent IDF keeps
// them on the modern branch, which is the one whose APIs the shims model.
#define ESP_IDF_VERSION_MAJOR 5
#define ESP_IDF_VERSION_MINOR 3
#define ESP_IDF_VERSION_PATCH 0
#define ESP_IDF_VERSION_VAL(ma, mi, pa) (((ma) << 16) | ((mi) << 8) | (pa))
#define ESP_IDF_VERSION ESP_IDF_VERSION_VAL(5, 3, 0)
