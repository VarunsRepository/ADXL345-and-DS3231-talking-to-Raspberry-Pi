#pragma once
#include <gpiod.h>
#include <vector>

// This is the GPIo class that handles everthign related to the PI pin read and write operations 
class GPIO {
public:
    // default constructor
    GPIO(const char* chip_path,
         const std::vector<unsigned int>& input_pins,  //vector  taking all the pins in input Mode 
       // unsigned int output_pin);                    // In assignment 1 we just had a single pin for output here  we have 3 so we need to make the classs flexible  
        unsigned int output_pin);                      //need a vector taking all the pins in output Mode

    // following he rule of 3, we're preventing the program from creating shallow copies     
    GPIO(const GPIO&) = delete; // no copy constructor
    GPIO& operator=(const GPIO&) = delete; // no overload of the = operator  

    // destructor neessesary to free up the meory 
    ~GPIO() noexcept;  // destructor as we might need to manually clean up the resources after we're done. 
              // since GPIO is not something that's sitting in the RAM, at 

    int waitForEvents(int timeout_ms); 
    int readEvents(int max_events);

    unsigned int getEventOffset(int index);

    void setOutput(bool state);

private:
        // we're declaring nullpointers to ensure  that there's  No garbage memory or No undefined behavior 
    gpiod_chip* chip  = nullptr;;
    gpiod_line_request* input_req  = nullptr;;
    gpiod_line_request* output_req  = nullptr;;
    gpiod_edge_event_buffer* event_buffer  = nullptr;;

    std::vector<unsigned int> inputs;
    unsigned int output;
};