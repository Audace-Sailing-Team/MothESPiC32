#include "us_acquisition.h"

void app_main(void)
{
    ESP_ERROR_CHECK(us_acquisition_start());
}