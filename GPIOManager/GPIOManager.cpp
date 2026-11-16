#include "GPIOManager.h"
#include <stdio.h>
#include <cstdlib>
#include <algorithm> // to use find
#include <unordered_map>

using namespace std;

GPIOManager::GPIOManager(const char* chipPath)
{
    chip_path = chipPath;

    chip = gpiod_chip_open(chip_path);

    if (!chip)
    {
        perror("Open chip");
        exit(1);
    }

    event_buffer = gpiod_edge_event_buffer_new(16);
}


// ~~~~~~~~~~~~  Input pins (input configuration to be set here). This is boiler plate code to initiate the READ on a GPIO pin.  
void GPIOManager::configureInputs(const std::vector<unsigned int>& pins)
{
    input_pins = pins;

    //create a new condigration for the Pin 
    gpiod_line_settings* settings = gpiod_line_settings_new();
 
    //GPIOD_LINE_DIRECTION_INPUT means input Pin
    gpiod_line_settings_set_direction(settings, GPIOD_LINE_DIRECTION_INPUT);
    
    //GPIOD_LINE_DIRECTION_INPUT means input Pin
    gpiod_line_settings_set_edge_detection(settings, GPIOD_LINE_EDGE_FALLING);

    //default : input high or  GPIOD_LINE_BIAS_PULL_UP
    gpiod_line_settings_set_bias(settings, GPIOD_LINE_BIAS_PULL_UP);

    gpiod_line_config* cfg = gpiod_line_config_new();

    gpiod_line_config_add_line_settings(
        cfg,
        pins.data(),
        pins.size(),
        settings
    );

    gpiod_request_config* req = gpiod_request_config_new();

    input_request = gpiod_chip_request_lines(chip, req, cfg);
}

// ~~~~~~~~~~~~ output Pin(for eg. LED) initialization start  here. This is boiler plate code to initiate pin in output Mode  
void GPIOManager::configureOutputs(const std::vector<unsigned int>& pins)
{
    output_pins = pins;

    //create a new condigration for the Pin 
    gpiod_line_settings* settings = gpiod_line_settings_new();

    
    gpiod_line_settings_set_direction(settings, GPIOD_LINE_DIRECTION_OUTPUT);
    gpiod_line_settings_set_output_value(settings, GPIOD_LINE_VALUE_INACTIVE); // start LOW

    gpiod_line_config* cfg = gpiod_line_config_new();

    gpiod_line_config_add_line_settings(
        cfg,
        pins.data(),
        pins.size(),
        settings
    );

    gpiod_request_config* req = gpiod_request_config_new();

    output_request = gpiod_chip_request_lines(chip, req, cfg);
}


//void GPIOManager::configureOutputs(const std::vector<unsigned int>& pins)
//{
//    output_pins = pins;
//
//    // Build pin → index mapping
//    pinIndex.clear();
//    for (size_t i = 0; i < pins.size(); i++)
//    {
//        pinIndex[pins[i]] = i;
//    }
//
//    // Create GPIO configuration
//    gpiod_line_settings* settings = gpiod_line_settings_new();
//    gpiod_line_settings_set_direction(settings, GPIOD_LINE_DIRECTION_OUTPUT);
//    gpiod_line_settings_set_output_value(settings, GPIOD_LINE_VALUE_INACTIVE);
//
//    gpiod_line_config* cfg = gpiod_line_config_new();
//
//    gpiod_line_config_add_line_settings(
//        cfg,
//        pins.data(),
//        pins.size(),
//        settings
//    );
//
//    gpiod_request_config* req = gpiod_request_config_new();
//
//    output_request = gpiod_chip_request_lines(chip, req, cfg);
//
//    // cleanup
//    gpiod_line_settings_free(settings);
//    gpiod_line_config_free(cfg);
//    gpiod_request_config_free(req);
//}


void GPIOManager::writePin(unsigned int pin, bool value)
{
    gpiod_line_value val[1];

    val[0] = value ? GPIOD_LINE_VALUE_ACTIVE : GPIOD_LINE_VALUE_INACTIVE; //feeling challengy so did a shorthand if-else aka ternary operator 

    gpiod_line_request_set_values(output_request, val); //finally writing to the pins 
}

//void GPIOManager::writePin(unsigned int pin, bool value)
//{
//    auto it = pinIndex.find(pin);
//    if (it == pinIndex.end()) return; // invalid pin safety check
//
//    size_t index = it->second;
//
//    gpiod_line_value val = value ? GPIOD_LINE_VALUE_ACTIVE
//                                 : GPIOD_LINE_VALUE_INACTIVE;
//
//    gpiod_line_request_set_value(output_request, index, val);
//}


int GPIOManager::waitForEvent(int timeout_ns) // waits for timeout_ns nanoseconds for an event - newer libgpiod API (v2
{
    return gpiod_line_request_wait_edge_events( // Edge event occured! 
        input_request,
        timeout_ns
    );
}

int GPIOManager::readEvents()
{
    return gpiod_line_request_read_edge_events(
        input_request,
        event_buffer,
        8
    );
}

unsigned int GPIOManager::getEventPin(int index)
{
    gpiod_edge_event* event =
        gpiod_edge_event_buffer_get_event(event_buffer, index);

    return gpiod_edge_event_get_line_offset(event);
}

//destructor
GPIOManager::~GPIOManager()
{
    if (event_buffer)
        gpiod_edge_event_buffer_free(event_buffer);

    if (input_request)
        gpiod_line_request_release(input_request);

    if (output_request)
        gpiod_line_request_release(output_request);

    if (chip)
        gpiod_chip_close(chip);
}
