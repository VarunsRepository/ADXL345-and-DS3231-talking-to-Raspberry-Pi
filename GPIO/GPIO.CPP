#include "GPIO.h"
#include <stdexcept>
#include <unistd.h>

//GPIO class methods implementation 
// we have tried to Separate the GPIO and the DS3231 so that the 2 elements can be used separately
// had we created a GPIO within the DS3231, that would have been easier to code but difficult to maintain

GPIO::GPIO(const char* chip_path,
           const std::vector<unsigned int>& input_pins,
           unsigned int output_pin)
    : inputs(input_pins), output(output_pin)
{
    chip = gpiod_chip_open(chip_path);
    if (!chip)
        throw std::runtime_error("Failed to open chip");

    // -------- INPUT CONFIG --------
    gpiod_line_settings* in_settings = gpiod_line_settings_new();
    gpiod_line_settings_set_direction(in_settings, GPIOD_LINE_DIRECTION_INPUT);
    gpiod_line_settings_set_edge_detection(in_settings, GPIOD_LINE_EDGE_FALLING);
    gpiod_line_settings_set_bias(in_settings, GPIOD_LINE_BIAS_PULL_UP);

    gpiod_line_config* in_cfg = gpiod_line_config_new();
    gpiod_line_config_add_line_settings(
        in_cfg,
        inputs.data(),
        inputs.size(),
        in_settings
    );

    gpiod_request_config* in_req_cfg = gpiod_request_config_new();
    input_req = gpiod_chip_request_lines(chip, in_req_cfg, in_cfg);
    if (!input_req)
        throw std::runtime_error("Failed to request input lines");

    // -------- OUTPUT CONFIG --------
    gpiod_line_settings* out_settings = gpiod_line_settings_new();
    gpiod_line_settings_set_direction(out_settings, GPIOD_LINE_DIRECTION_OUTPUT);
    gpiod_line_settings_set_output_value(out_settings, GPIOD_LINE_VALUE_INACTIVE);

    gpiod_line_config* out_cfg = gpiod_line_config_new();
    gpiod_line_config_add_line_settings(out_cfg, &output, 1, out_settings);

    gpiod_request_config* out_req_cfg = gpiod_request_config_new();
    output_req = gpiod_chip_request_lines(chip, out_req_cfg, out_cfg);
    if (!output_req)
        throw std::runtime_error("Failed to request output line");

    event_buffer = gpiod_edge_event_buffer_new(16);
    if (!event_buffer)
        throw std::runtime_error("Failed to create event buffer");
}

int GPIO::waitForEvents(int timeout_ms)
{
    return gpiod_line_request_wait_edge_events(input_req, timeout_ms);
}

int GPIO::readEvents(int max_events)
{
    return gpiod_line_request_read_edge_events(
        input_req,
        event_buffer,
        max_events
    );
}

unsigned int GPIO::getEventOffset(int index)
{
    auto event = gpiod_edge_event_buffer_get_event(event_buffer, index);
    return gpiod_edge_event_get_line_offset(event);
}

//void GPIO::setOutput(bool state)
//{
//    gpiod_line_value value[1];
//    value[0] = state ? GPIOD_LINE_VALUE_ACTIVE
//                     : GPIOD_LINE_VALUE_INACTIVE;
//
//    gpiod_line_request_set_values(output_req, value);
//}


void GPIO::setOutput(bool state)
{
    gpiod_line_request_set_value(
        output_req,
        0,
        state ? GPIOD_LINE_VALUE_ACTIVE : GPIOD_LINE_VALUE_INACTIVE
    );
}


GPIO::~GPIO() noexcept  // we declare the destructor as noexcept cause we don't want it to throw exception during object destruction 
{
 /*
    gpiod_edge_event_buffer_free(event_buffer);
    gpiod_line_request_release(input_req);
    gpiod_line_request_release(output_req);
    gpiod_chip_close(chip);
 */
// the above code just frees up the memory from irrespective of the fact that if that block of memory was owned by GPIo class or not.
// we need to first check if we owned that memory and then clear it 

if (event_buffer)
        gpiod_edge_event_buffer_free(event_buffer);

    if (input_req)
        gpiod_line_request_release(input_req);

    if (output_req)
        gpiod_line_request_release(output_req);

    if (chip)
        gpiod_chip_close(chip);

}
