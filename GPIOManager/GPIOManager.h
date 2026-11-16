#ifndef GPIOManager_H
#define GPIOManager_H

#include <gpiod.h>
#include <vector>
#include <unordered_map>


class GPIOManager {
private:

    const char* chip_path;   //string to store the chip that manages GPIO, P4b has 2 chips
    gpiod_chip* chip=nullptr;

    gpiod_line_request* input_request=nullptr;     //inputs requests handler 
    gpiod_line_request* output_request=nullptr;    //Output requests handler

    gpiod_edge_event_buffer* event_buffer=nullptr; // buffer to store events (we will later program our Pi to generate negatie dge requests) 

    std::unordered_map<unsigned int, size_t> pinIndex;
 
    
    std::vector<unsigned int> input_pins;  //vector(not array!) that holds input pins 
    std::vector<unsigned int> output_pins; //vector(not array!) that holds output pins

    
public:

    GPIOManager(const char* chipPath);  //good 'ol constructor 
    ~GPIOManager();                     // destructor(we would need it here to free the GPIOs)

    // configure pins - like Arduino's PinMode
    void configureInputs(const std::vector<unsigned int>& pins);
    void configureOutputs(const std::vector<unsigned int>& pins);

    // like Adruino's digitalWrite
    void writePin(unsigned int pin, bool value);

    // we're still trying to work it out, making the Pi do stuff when there's a negtive edge  
    int waitForEvent(int timeout_ns = -1); //this method simply waits forever(-1) untl an even trggers  

    // read events
    int readEvents(); //recall that we had gpiod_edge_event_buffer above, this function reads the events from that buffer       

    // get event pin
    unsigned int getEventPin(int index);

};


#endif